#include "orb_map.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ui/flags.h>
#include <rex/ui/imgui_dialog.h>

#include "aspect_ratio.h"
#include "cheat_hooks.h"
#include "cheats.h"
#include "guest_hooks.h"
#include "guest_layout.h"
#include "guest_memory.h"
#include "orb_map_rules.h"
#include "settings.h"

namespace crackdown::orb_map {
namespace {

using guest::Load32;
using guest::LoadFloat;
using guest::Plausible;
using guest::StoreFloat;
using Clock = std::chrono::steady_clock;
using hooks::GuestFunction;

// cPickup: every agility and hidden orb in the city, all created when the city loads. The vtable address, and every
// other guest address this feature reads, differs between the base game and Title Update 6 (src/guest_layout.h); the
// field offsets below are the same in both, verified against each build's generated code.
constexpr uint32_t kOrbTypeOffset = 264;  // 0..3 agility, 4 hidden
constexpr uint32_t kHiddenOrbType = 4;
constexpr uint32_t kOrbPositionOffset = 272;  // x, y, z
constexpr uint32_t kOrbStateOffset = 288;
constexpr uint8_t kOrbAvailableBit = 0x01;  // cleared once collected

// Supply points: a table of 64-byte entries whose (x, z) are at +64 and +72, with the count at +8.
constexpr uint32_t kSupplyPointCountOffset = 8;
constexpr uint32_t kSupplyPointStride = 64;
constexpr uint32_t kSupplyPointPositionOffset = 64;
constexpr uint32_t kMaxSupplyPoints = 64;

// --- Minimap: HUD_cMap marker ---------------------------------------------------------------------------------------
// The projection writes the marker's position relative to the minimap's centre at +4/+8, a shown flag at +12 and an
// inside-the-radius flag at +856; the target descriptor at +872 gives the world position.
constexpr uint32_t kMarkerX = 4;
constexpr uint32_t kMarkerY = 8;
constexpr uint32_t kMarkerShown = 12;
constexpr uint32_t kMarkerInside = 856;
constexpr uint32_t kMarkerDescriptor = 872;
// The marker's icon image: +140/+144 is where the HUD last drew it, so taking away the marker's position gives the
// minimap's centre.
constexpr uint32_t kMarkerIconImage = 884;
constexpr uint32_t kImageDrawnX = 140;
constexpr uint32_t kImageDrawnY = 144;
// The projection takes away half the size of the image at +144 so that the icon's corner lands there. The game reads
// the size through the image's vtable (slot 9); these are the fields that call returns for the HUD's images.
constexpr uint32_t kMarkerSizingImage = 144;
constexpr uint32_t kImageWidth = 44;
constexpr uint32_t kImageHeight = 48;
// Orbs farther than this from the player are not projected; the minimap shows much less than this.
constexpr float kProjectRange = 300.0f;
// The game projects every marker each frame; the orbs are projected once per frame, on one marker. Another marker
// takes over once that one has not been seen for this long.
constexpr auto kAnchorTimeout = std::chrono::milliseconds(100);
// The centre is taken only while the anchor marker stands still, since the icon was drawn a frame ago.
constexpr float kAnchorStill = 0.05f;

// --- Pause menu map: XUIFE_S_M_cMap view ------------------------------------------------------------------------------
// One 336-byte view per view of the pause map, the city or a district. Its world-to-layout conversion reads these
// fields.
constexpr uint32_t kViewOffsetX = 48;     // + 52
constexpr uint32_t kViewOriginX = 112;    // + 116: the world point at the layout origin
constexpr uint32_t kViewPanStartX = 160;  // + 164
constexpr uint32_t kViewAngle = 256;
constexpr uint32_t kViewCornerX = 272;  // + 276
constexpr uint32_t kViewScale = 288;
constexpr uint32_t kViewPanX = 304;  // + 308
constexpr uint32_t kViewPanned = 324;  // set on district views
// The factor (about 0.5) a pan is applied with. The menu also slides the view's icon layer by the other half of the
// pan, so on screen a point moves by the whole pan: the dots get the conversion's half and that slide.
constexpr float kIconLayerPanFactor = 0.5f;
// An orb this close to a supply point is left out, so the supply point's icon stays readable. Layout units.
constexpr float kSupplyPointClearance = 7.0f;

// Frontend screens (pause menu, city map, district map, fades, supply point menu...) all derive from one base. A
// view belongs to the screen on top when it was built, and its dots show only while that screen is still on top, so
// a menu or fade over it hides them. Screens are only built and torn down around menus and fades, never during play,
// so the minimap's dots wait out the fades for a moment after each change. A screen built with none left is a load,
// such as leaving a supply point: the game updates the minimap under the Agency logo that plays then, so the dots
// wait for the logo and the fade after it.
constexpr auto kScreenChangeQuiet = std::chrono::milliseconds(1500);
constexpr auto kLoadQuiet = std::chrono::milliseconds(6000);

constexpr auto kStaleAfter = std::chrono::milliseconds(150);

// The minimap already uses grey, yellow and red for enemies and white for the player, on a blue map: agility orbs
// keep their own green and hidden orbs take magenta.
constexpr ImU32 kAgilityColor = IM_COL32(110, 230, 90, 255);
constexpr ImU32 kHiddenColor = IM_COL32(255, 80, 220, 255);
constexpr ImU32 kOutlineColor = IM_COL32(0, 0, 0, 200);
constexpr float kMinimapDotRadius = 3.0f;  // layout units
constexpr float kMapDotRadius = 2.0f;

struct Dot {
  float x, y;  // layout units, from `Frame::origin`
  bool hidden;
};

struct Frame {
  std::vector<Dot> dots;
  float origin_x = 0.0f, origin_y = 0.0f;  // where the dots are measured from: the minimap's centre, or (0, 0)
  Clock::time_point time;
};

std::atomic<uint8_t*> g_base{nullptr};  // the guest memory base, for the UI thread

// Lock order: g_views_mutex before g_orbs_mutex. The destructor hooks take their lock before the object is freed, so
// a reader holding it sees only live objects.
std::mutex g_views_mutex;
std::mutex g_orbs_mutex;
std::vector<uint32_t> g_orbs;  // guarded by g_orbs_mutex

std::mutex g_minimap_mutex;
Frame g_minimap;  // guarded by g_minimap_mutex

struct ViewInfo {
  uint32_t screen = 0;  // the screen on top when the view was built
  bool district_known = false;
  District district{};
};
std::vector<uint32_t> g_views;            // live views, oldest first, guarded by g_views_mutex
std::map<uint32_t, ViewInfo> g_view_info;  // guarded by g_views_mutex
std::vector<uint32_t> g_screens;          // live screens, oldest first, guarded by g_views_mutex
std::atomic<Clock::rep> g_minimap_quiet_until{0};  // after a screen change, as Clock ticks

// Hides the minimap's dots for `quiet` from now, unless they are already hidden for longer.
void QuietMinimap(Clock::duration quiet) {
  const Clock::rep until = (Clock::now() + quiet).time_since_epoch().count();
  Clock::rep current = g_minimap_quiet_until.load();
  while (current < until && !g_minimap_quiet_until.compare_exchange_weak(current, until)) {
  }
}

// The projection override; set and read on the thread that runs the projection.
thread_local bool t_override = false;
thread_local uint32_t t_override_descriptor = 0;
thread_local float t_override_x = 0.0f, t_override_z = 0.0f;
thread_local uint32_t t_anchor = 0;
thread_local bool t_anchor_shown = false;
thread_local Clock::time_point t_anchor_seen;
thread_local float t_anchor_x = NAN, t_anchor_y = NAN;
// The minimap's centre in the layout, measured in game; the HUD never moves it. Kept up to date from a shown marker.
thread_local float t_centre_x = 211.5f, t_centre_y = 577.5f;

bool ShowOnMinimap(cheats::Cheat cheat) { return cheats::Value(cheat) != cheats::kMapOff; }
bool ShowOnMap(cheats::Cheat cheat) { return cheats::Value(cheat) == cheats::kMapMinimapAndWorld; }

// Whether `orb` is a live, uncollected orb of a type the player asked to see; `hidden` says which type.
bool Wanted(uint8_t* base, uint32_t orb, bool show_agility, bool show_hidden, bool& hidden) {
  if (Load32(base, orb) != Layout().orb_pickup_vtable || !(guest::Load8(base, orb + kOrbStateOffset) & kOrbAvailableBit)) {
    return false;
  }
  hidden = Load32(base, orb + kOrbTypeOffset) == kHiddenOrbType;
  return hidden ? show_hidden : show_agility;
}

// --- Minimap -------------------------------------------------------------------------------------------------------

struct SavedMarker {
  float x, y;
  uint8_t shown, inside;
};

SavedMarker Save(uint8_t* base, uint32_t marker) {
  return {LoadFloat(base, marker + kMarkerX), LoadFloat(base, marker + kMarkerY),
          guest::Load8(base, marker + kMarkerShown), guest::Load8(base, marker + kMarkerInside)};
}

void Restore(uint8_t* base, uint32_t marker, const SavedMarker& saved) {
  StoreFloat(base, marker + kMarkerX, saved.x);
  StoreFloat(base, marker + kMarkerY, saved.y);
  guest::Store8(base, marker + kMarkerShown, saved.shown);
  guest::Store8(base, marker + kMarkerInside, saved.inside);
}

// Takes the minimap's centre from where the shown marker's icon was last drawn.
void UpdateCentre(uint8_t* base, uint32_t marker, const SavedMarker& saved) {
  const float drawn_x = LoadFloat(base, marker + kMarkerIconImage + kImageDrawnX);
  const float drawn_y = LoadFloat(base, marker + kMarkerIconImage + kImageDrawnY);
  if (std::isfinite(drawn_x) && std::isfinite(drawn_y) && (drawn_x != 0.0f || drawn_y != 0.0f) &&
      std::abs(saved.x - t_anchor_x) < kAnchorStill && std::abs(saved.y - t_anchor_y) < kAnchorStill) {
    // The icon's corner is the minimap's centre plus the marker's position; the projected points sit half an icon
    // further on.
    t_centre_x = drawn_x - saved.x + 0.5f * LoadFloat(base, marker + kMarkerSizingImage + kImageWidth);
    t_centre_y = drawn_y - saved.y + 0.5f * LoadFloat(base, marker + kMarkerSizingImage + kImageHeight);
  }
  t_anchor_x = saved.x;
  t_anchor_y = saved.y;
}

// Re-runs the game's own projection for every nearby uncollected orb, using `marker` as a stand-in whose position
// getter returns the orb's position, and restores the marker afterwards. `entry` is the context the projection was
// called with and `after` the one it returned with; `shown` is whether the game showed the marker itself.
void ProjectOrbs(const PPCContext& entry, const PPCContext& after, uint8_t* base, uint32_t marker, bool shown,
                 bool show_agility, bool show_hidden, GuestFunction original) {
  const uint32_t character = cheats::LocalPlayerCharacter(base);
  if (!character) {
    return;
  }
  const float px = LoadFloat(base, character + kCharacterPositionOffset);
  const float pz = LoadFloat(base, character + kCharacterPositionOffset + 8);
  const SavedMarker saved = Save(base, marker);
  Frame frame;
  t_override = true;
  t_override_descriptor = marker + kMarkerDescriptor;
  {
    // Only the game thread destroys orbs, and this is the game thread, so the orbs stay live while it reads them.
    std::lock_guard lock(g_orbs_mutex);
    for (const uint32_t orb : g_orbs) {
      bool hidden = false;
      if (!Wanted(base, orb, show_agility, show_hidden, hidden)) {
        continue;
      }
      const float x = LoadFloat(base, orb + kOrbPositionOffset);
      const float z = LoadFloat(base, orb + kOrbPositionOffset + 8);
      if (!(std::abs(x - px) <= kProjectRange && std::abs(z - pz) <= kProjectRange)) {
        continue;
      }
      t_override_x = x;
      t_override_z = z;
      // The projection saves and restores the registers it uses, writes only the four marker fields restored below,
      // and puts the float mode back on every path, so the entry registers and the current float state replay it.
      PPCContext call = entry;
      call.fpscr = after.fpscr;
      call.r5.u64 = 0;  // no clamping to the minimap's edge: orbs outside it are not shown
      original(call, base);
      if (call.r3.u32 & 0xFF) {
        frame.dots.push_back({LoadFloat(base, marker + kMarkerX), LoadFloat(base, marker + kMarkerY), hidden});
      }
    }
  }
  t_override = false;
  Restore(base, marker, saved);

  // The minimap's centre comes from where the marker's icon was drawn, so only a shown marker can update it. The
  // centre does not move, so the last one stays good while no supply point is shown.
  if (!shown) {
    t_anchor_x = t_anchor_y = NAN;
  } else {
    UpdateCentre(base, marker, saved);
  }
  frame.origin_x = t_centre_x;
  frame.origin_y = t_centre_y;
  frame.time = Clock::now();
  std::lock_guard lock(g_minimap_mutex);
  g_minimap = std::move(frame);
}

// --- Pause menu map ------------------------------------------------------------------------------------------------

// The view's conversion; false when its fields cannot make one.
bool TransformOf(uint8_t* base, uint32_t view, ViewTransform& t) {
  t.origin_x = LoadFloat(base, view + kViewOriginX);
  t.origin_z = LoadFloat(base, view + kViewOriginX + 4);
  t.scale = LoadFloat(base, view + kViewScale);
  const float angle = LoadFloat(base, view + kViewAngle);
  if (!(t.scale > 0.0f) || !std::isfinite(t.scale) || !std::isfinite(angle)) {
    return false;
  }
  t.cos = std::cos(angle);
  t.sin = std::sin(angle);
  t.shift_x = LoadFloat(base, view + kViewOffsetX) - LoadFloat(base, view + kViewCornerX);
  t.shift_y = LoadFloat(base, view + kViewOffsetX + 4) - LoadFloat(base, view + kViewCornerX + 4);
  if (guest::Load8(base, view + kViewPanned)) {
    const float unset = LoadFloat(base, Layout().orb_unset_pan);
    const float pan_x = LoadFloat(base, view + kViewPanX), pan_y = LoadFloat(base, view + kViewPanX + 4);
    if (pan_x != unset || pan_y != unset) {
      const float factor = LoadFloat(base, Layout().orb_pan_factor) + kIconLayerPanFactor;
      t.shift_x += (pan_x - LoadFloat(base, view + kViewPanStartX)) * factor;
      t.shift_y += (pan_y - LoadFloat(base, view + kViewPanStartX + 4)) * factor;
    }
  }
  return std::isfinite(t.origin_x) && std::isfinite(t.origin_z) && std::isfinite(t.shift_x) &&
         std::isfinite(t.shift_y);
}

// The pause menu map's dots for this display frame, worked out on the UI thread from the shown view's fields.
Frame MapFrame(uint8_t* base) {
  Frame frame;
  const bool show_agility = ShowOnMap(cheats::Cheat::AgilityOrbsOnMap);
  const bool show_hidden = ShowOnMap(cheats::Cheat::HiddenOrbsOnMap);
  if ((!show_agility && !show_hidden) || !cheats::Available()) {
    return frame;
  }
  // Held to the end, so neither the view nor any orb can be torn down while they are read.
  std::lock_guard views_lock(g_views_mutex);
  if (g_views.empty() || g_screens.empty()) {
    return frame;
  }
  // The newest view is the one on screen: the city view is built first, and each district view over it.
  const uint32_t view = g_views.back();
  const auto info = g_view_info.find(view);
  ViewTransform t;
  if (info == g_view_info.end() || info->second.screen != g_screens.back() || !TransformOf(base, view, t)) {
    return frame;
  }
  // A district view draws only its own island; the city view draws them all. The district is the one at the middle
  // of the panel when the view is first shown, so panning does not change it.
  const bool district_view = guest::Load8(base, view + kViewPanned);
  if (district_view && !info->second.district_known) {
    const auto [x, z] = ViewToWorld(t, (kPanelLeft + kPanelRight) * 0.5f, (kPanelTop + kPanelBottom) * 0.5f);
    info->second.district = DistrictAt(x, z);
    info->second.district_known = true;
  }

  std::vector<std::pair<float, float>> supply_points;
  if (const uint32_t table = Load32(base, Layout().orb_supply_points); Plausible(table)) {
    const uint32_t count = std::min(Load32(base, table + kSupplyPointCountOffset), kMaxSupplyPoints);
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t entry = table + i * kSupplyPointStride + kSupplyPointPositionOffset;
      supply_points.push_back(WorldToView(t, LoadFloat(base, entry), LoadFloat(base, entry + 8)));
    }
  }
  const auto near_supply_point = [&](float mx, float my) {
    return std::any_of(supply_points.begin(), supply_points.end(), [&](const std::pair<float, float>& point) {
      return std::abs(point.first - mx) < kSupplyPointClearance && std::abs(point.second - my) < kSupplyPointClearance;
    });
  };

  std::lock_guard orbs_lock(g_orbs_mutex);
  for (const uint32_t orb : g_orbs) {
    bool hidden = false;
    if (!Wanted(base, orb, show_agility, show_hidden, hidden)) {
      continue;
    }
    const float x = LoadFloat(base, orb + kOrbPositionOffset);
    const float z = LoadFloat(base, orb + kOrbPositionOffset + 8);
    if (district_view && DistrictAt(x, z) != info->second.district) {
      continue;
    }
    const auto [mx, my] = WorldToView(t, x, z);
    if (!InsidePanel(mx, my) || near_supply_point(mx, my)) {
      continue;
    }
    frame.dots.push_back({mx, my, hidden});
  }
  frame.time = Clock::now();
  return frame;
}

// --- Overlay -------------------------------------------------------------------------------------------------------

class OrbMapOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit OrbMapOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    uint8_t* base = g_base;
    if (!base) {
      return;
    }
    const AspectRatio aspect = ParseAspectRatio(REXCVAR_GET(crackdown_aspect_ratio));
    const LayoutToWindow window = MapLayoutToWindow(io.DisplaySize.x, io.DisplaySize.y, float(CanvasWidth(aspect)),
                                                     float(kCanvasHeight), REXCVAR_GET(present_letterbox));
    if (window.scale_x <= 0.0f || window.scale_y <= 0.0f) {
      return;
    }
    // A minimap not refreshed lately is not being drawn (menus, loading, cutscenes); a screen just changed means a
    // fade or the logo is over it.
    const Clock::time_point now = Clock::now();
    const Clock::time_point quiet_until{Clock::duration(g_minimap_quiet_until.load())};
    Frame minimap;
    {
      std::lock_guard lock(g_minimap_mutex);
      if (now - g_minimap.time <= kStaleAfter && now > quiet_until && cheats::Available()) {
        minimap = g_minimap;
      }
    }
    DrawDots(window, minimap, kMinimapDotRadius);
    DrawDots(window, MapFrame(base), kMapDotRadius);
  }

 private:
  static void DrawDots(const LayoutToWindow& window, const Frame& frame, float layout_radius) {
    const float radius = std::max(1.5f, layout_radius * window.scale_y);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const Dot& dot : frame.dots) {
      const ImVec2 at(window.left + (frame.origin_x + dot.x) * window.scale_x,
                      window.top + (frame.origin_y + dot.y) * window.scale_y);
      draw->AddCircleFilled(at, radius + 1.0f, kOutlineColor);
      draw->AddCircleFilled(at, radius, dot.hidden ? kHiddenColor : kAgilityColor);
    }
  }
};

}  // namespace

void CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  new OrbMapOverlay(drawer);  // retained by the drawer for the session
}

}  // namespace crackdown::orb_map

// cPickup constructor: r3 = the new object.
void crackdown::hooks::OrbConstructed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  const uint32_t object = ctx.r3.u32;
  original(ctx, base);
  g_base = base;
  std::lock_guard lock(g_orbs_mutex);
  g_orbs.push_back(object);
}

// cPickup destructor: r3 = the object.
void crackdown::hooks::OrbDestroyed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  {
    std::lock_guard lock(g_orbs_mutex);
    g_orbs.erase(std::remove(g_orbs.begin(), g_orbs.end(), ctx.r3.u32), g_orbs.end());
  }
  original(ctx, base);
}

// Pause menu map view constructor: r3 = the new view.
void crackdown::hooks::OrbMapViewConstructed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  const uint32_t view = ctx.r3.u32;
  original(ctx, base);
  std::lock_guard lock(g_views_mutex);
  g_views.push_back(view);
  g_view_info[view] = {g_screens.empty() ? 0 : g_screens.back()};
}

// Pause menu map view destructor: r3 = the view.
void crackdown::hooks::OrbMapViewDestroyed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  {
    std::lock_guard lock(g_views_mutex);
    g_views.erase(std::remove(g_views.begin(), g_views.end(), ctx.r3.u32), g_views.end());
    g_view_info.erase(ctx.r3.u32);
  }
  original(ctx, base);
}

// Frontend screen base constructor: r3 = the screen.
void crackdown::hooks::OrbFrontendScreenConstructed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  const uint32_t screen = ctx.r3.u32;
  original(ctx, base);
  std::lock_guard lock(g_views_mutex);
  QuietMinimap(g_screens.empty() ? kLoadQuiet : kScreenChangeQuiet);
  g_screens.push_back(screen);
}

// Frontend screen base destructor. The caller has already adjusted r3 to this base's own sub-object, by whatever
// fixed amount its own class layout needs; this hook only ever has to compare that pointer for equality, never
// reinterpret it, so it does not need to know the amount.
void crackdown::hooks::OrbFrontendScreenDestroyed(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  {
    const uint32_t screen = ctx.r3.u32;
    std::lock_guard lock(g_views_mutex);
    g_screens.erase(std::remove(g_screens.begin(), g_screens.end(), screen), g_screens.end());
    QuietMinimap(kScreenChangeQuiet);
  }
  original(ctx, base);
}

// Supply point descriptor's "shown" check: r3 = the descriptor. Returns whether the marker shows.
void crackdown::hooks::OrbListDescriptorShown(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  if (t_override && ctx.r3.u32 == t_override_descriptor) {
    ctx.r3.u64 = 1;
    return;
  }
  original(ctx, base);
}

// List-entry descriptor position getter: r3 = out (x, z), r4 = the descriptor.
void crackdown::hooks::OrbListDescriptorPosition(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  if (t_override && ctx.r4.u32 == t_override_descriptor) {
    StoreFloat(base, ctx.r3.u32, t_override_x);
    StoreFloat(base, ctx.r3.u32 + 4, t_override_z);
    return;
  }
  original(ctx, base);
}

// Minimap marker projection: r3 = marker, r4 = the map's viewport, r5 = clamp to the edge. Returns whether shown.
void crackdown::hooks::OrbMinimapProjection(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  using namespace crackdown::orb_map;
  const bool show_agility = ShowOnMinimap(crackdown::cheats::Cheat::AgilityOrbsOnMap);
  const bool show_hidden = ShowOnMinimap(crackdown::cheats::Cheat::HiddenOrbsOnMap);
  if (t_override || (!show_agility && !show_hidden) || !crackdown::cheats::Available()) {
    original(ctx, base);
    return;
  }
  const PPCContext entry = ctx;
  const uint32_t marker = ctx.r3.u32;
  original(ctx, base);
  if (Load32(base, marker + kMarkerDescriptor) != Layout().orb_list_vtable) {
    return;
  }
  const Clock::time_point now = Clock::now();
  const bool shown = ctx.r3.u32 & 0xFF;
  // One pass per frame, on the anchor marker. A shown marker takes over from a hidden one, since only a shown marker
  // can measure the minimap's centre.
  if (marker != t_anchor && now - t_anchor_seen < kAnchorTimeout && (t_anchor_shown || !shown)) {
    return;
  }
  t_anchor = marker;
  t_anchor_seen = now;
  t_anchor_shown = shown;
  ProjectOrbs(entry, ctx, base, marker, shown, show_agility, show_hidden, original);
}
