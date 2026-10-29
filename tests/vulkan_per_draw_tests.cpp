// Vulkan per-draw optimizations (patches/rexglue-sdk/0117, 0119, 0120, 0122, 0125, 0128, 0129): each faster path is checked
// against the code it replaced on random inputs (same output, same state), and the patch text is checked to still
// hold the code mirrored here. The Vulkan calls themselves need a device and are not run.
// Run: vulkan_per_draw_tests

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "hle_frame_abi.h"

namespace {

std::string ReadPatch(const char* prefix) {
  for (const auto& entry : std::filesystem::directory_iterator(CRACKDOWN_PATCH_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) == 0) return check::ReadText(entry.path());
  }
  return {};
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// --- 0125: float constants copied a run per memcpy -------------------------------------------------------------

// The loop it replaced: one constant (4 registers) per memcpy, in bitmap order.
uint8_t* CopyFloatConstantsOneByOne(uint8_t* dest, const uint32_t* first_register, const uint64_t* bitmap) {
  for (uint32_t i = 0; i < 4; ++i) {
    uint64_t entry = bitmap[i];
    while (entry) {
      uint32_t index = uint32_t(std::countr_zero(entry));
      entry &= ~(uint64_t(1) << index);
      std::memcpy(dest, first_register + (i << 8) + (index << 2), sizeof(float) * 4);
      dest += sizeof(float) * 4;
    }
  }
  return dest;
}

// Mirror of CopyUsedFloatConstants in 0125.
uint8_t* CopyUsedFloatConstants(uint8_t* dest, const uint32_t* first_register, const uint64_t* bitmap) {
  for (uint32_t i = 0; i < 4; ++i) {
    uint64_t bits = bitmap[i];
    while (bits) {
      uint32_t first = uint32_t(std::countr_zero(bits));
      uint32_t run = uint32_t(std::countr_one(bits >> first));
      std::memcpy(dest, first_register + (i << 8) + (first << 2), run * 4 * sizeof(float));
      dest += run * 4 * sizeof(float);
      bits = first + run >= 64 ? 0 : bits & (~uint64_t(0) << (first + run));
    }
  }
  return dest;
}

void FloatConstantCopies(std::mt19937_64& rng) {
  std::vector<uint32_t> registers(256 * 4);
  for (uint32_t& value : registers) value = uint32_t(rng());
  std::vector<uint64_t> cases = {0, ~uint64_t(0), uint64_t(1), uint64_t(1) << 63, 0x8000000000000001ull,
                                 0x00FF00FF00FF00FFull, 0x7FFFFFFFFFFFFFFEull};
  for (int trial = 0; trial < 20000; ++trial) {
    uint64_t bitmap[4];
    for (uint64_t& word : bitmap) {
      switch (rng() % 4) {
        case 0: word = cases[rng() % cases.size()]; break;
        case 1: word = rng() & rng(); break;  // sparse
        case 2: word = rng() | rng(); break;  // dense, long runs
        default: word = rng(); break;
      }
    }
    std::array<uint8_t, 256 * 16 + 64> a{}, b{};
    uint8_t* end_a = CopyFloatConstantsOneByOne(a.data(), registers.data(), bitmap);
    uint8_t* end_b = CopyUsedFloatConstants(b.data(), registers.data(), bitmap);
    CHECK(end_a - a.data() == end_b - b.data());
    CHECK(a == b);
    size_t used = 0;
    for (uint64_t word : bitmap) used += size_t(std::popcount(word));
    CHECK(size_t(end_b - b.data()) == used * 16);
  }
}

// --- 0125: vertex buffer residency cleared a 64-bit mask at a time --------------------------------------------

struct Residency {
  uint64_t in_sync[2] = {};
  static constexpr uint32_t kCount = 96;
};

void InvalidateOne(Residency& r, uint32_t index) {
  if (index >= Residency::kCount) return;
  r.in_sync[index >> 6] &= ~(uint64_t(1) << (index & 63));
}

void InvalidateRangeOneByOne(Residency& r, uint32_t first, uint32_t last) {
  if (first > last) std::swap(first, last);
  if (first >= Residency::kCount) return;
  last = std::min(last, Residency::kCount - 1);
  for (uint32_t i = first; i <= last; ++i) InvalidateOne(r, i);
}

// Mirror of InvalidateVertexBufferResidencyRange in 0125.
void InvalidateRangeByMasks(Residency& r, uint32_t first_vfetch, uint32_t last_vfetch) {
  if (first_vfetch > last_vfetch) std::swap(first_vfetch, last_vfetch);
  if (first_vfetch >= Residency::kCount) return;
  last_vfetch = std::min(last_vfetch, Residency::kCount - 1);
  for (uint32_t word = first_vfetch >> 6; word <= (last_vfetch >> 6); ++word) {
    uint32_t first_bit = word == (first_vfetch >> 6) ? (first_vfetch & 63) : 0;
    uint32_t last_bit = word == (last_vfetch >> 6) ? (last_vfetch & 63) : 63;
    uint64_t mask = (last_bit == 63 ? ~uint64_t(0) : ((uint64_t(1) << (last_bit + 1)) - 1)) &
                    ~((uint64_t(1) << first_bit) - 1);
    r.in_sync[word] &= ~mask;
  }
}

void ResidencyRanges(std::mt19937_64& rng) {
  for (int trial = 0; trial < 50000; ++trial) {
    Residency a, b;
    a.in_sync[0] = b.in_sync[0] = rng();
    a.in_sync[1] = b.in_sync[1] = rng();
    uint32_t first = uint32_t(rng() % 110), last = uint32_t(rng() % 110);
    InvalidateRangeOneByOne(a, first, last);
    InvalidateRangeByMasks(b, first, last);
    CHECK(a.in_sync[0] == b.in_sync[0] && a.in_sync[1] == b.in_sync[1]);
  }
  for (uint32_t first = 0; first < 100; ++first) {
    for (uint32_t last = 0; last < 100; ++last) {
      Residency a, b;
      a.in_sync[0] = b.in_sync[0] = ~uint64_t(0);
      a.in_sync[1] = b.in_sync[1] = ~uint64_t(0);
      InvalidateRangeOneByOne(a, first, last);
      InvalidateRangeByMasks(b, first, last);
      CHECK(a.in_sync[0] == b.in_sync[0] && a.in_sync[1] == b.in_sync[1]);
    }
  }
}

// --- 0117: sampler parameter table ------------------------------------------------------------------------------

// Stands for GetSamplerParametersUncached: any pure function of the fetch dwords, the filters and the override.
uint32_t SamplerParametersOf(const uint32_t fetch[6], uint32_t filters, int32_t aniso_override) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; ++i) h = (h ^ fetch[i]) * 16777619u;
  h = (h ^ filters) * 16777619u;
  return (h ^ uint32_t(aniso_override)) & 0x3FFFFF;  // 22 bits, like SamplerParameters
}

// Mirror of the 0117 table (direct-mapped, 256 entries, keyed by everything the result depends on).
struct SamplerTable {
  struct Entry {
    uint32_t fetch[6];
    uint32_t binding_filters;
    int32_t anisotropic_override;
    uint32_t parameters;
    bool valid = false;
  };
  std::array<Entry, 256> entries{};
  uint64_t hits = 0;
  uint32_t Get(const uint32_t fetch_dwords[6], uint32_t binding_filters, int32_t anisotropic_override) {
    uint32_t hash = binding_filters * 0x9E3779B1u;
    for (int i = 0; i < 6; ++i) hash = (hash ^ fetch_dwords[i]) * 0x9E3779B1u;
    Entry& entry = entries[(hash >> 24) & 255];
    if (entry.valid && entry.binding_filters == binding_filters &&
        entry.anisotropic_override == anisotropic_override &&
        !std::memcmp(entry.fetch, fetch_dwords, sizeof(entry.fetch))) {
      ++hits;
      return entry.parameters;
    }
    uint32_t parameters = SamplerParametersOf(fetch_dwords, binding_filters, anisotropic_override);
    std::memcpy(entry.fetch, fetch_dwords, sizeof(entry.fetch));
    entry.binding_filters = binding_filters;
    entry.anisotropic_override = anisotropic_override;
    entry.parameters = parameters;
    entry.valid = true;
    return parameters;
  }
};

void SamplerTableMatchesUncached(std::mt19937_64& rng) {
  SamplerTable table;
  // A working set like a frame's: a few hundred fetch constants reused, some rewritten, the override changed.
  std::vector<std::array<uint32_t, 6>> fetches(600);
  for (auto& fetch : fetches) {
    for (uint32_t& dword : fetch) dword = uint32_t(rng());
  }
  int32_t aniso_override = 3;
  for (int draw = 0; draw < 200000; ++draw) {
    if (draw % 50000 == 49999) aniso_override = int32_t(rng() % 7) - 1;
    const size_t index = rng() % (draw % 3 ? 40 : fetches.size());
    auto& fetch = fetches[index];
    if (rng() % 1000 == 0) fetch[rng() % 6] ^= uint32_t(1) << (rng() % 32);  // a rewritten fetch constant
    // A shader's sampler binding keeps its filter overrides; now and then another shader samples the same texture.
    uint32_t filters = rng() % 20 ? uint32_t(index % 4) | (uint32_t(index % 3) << 8)
                                  : uint32_t(rng() % 4) | (uint32_t(rng() % 4) << 8) |
                                        (uint32_t(rng() % 3) << 16) | (uint32_t(rng() % 6) << 24);
    CHECK(table.Get(fetch.data(), filters, aniso_override) ==
          SamplerParametersOf(fetch.data(), filters, aniso_override));
  }
  CHECK(table.hits > 100000);  // the table actually hits on such a working set
}

// --- 0119: texture descriptor set reuse -------------------------------------------------------------------------

// A stage's set is kept while its key (counts, views, samplers) is the one last written in this submission. Model:
// what the GPU would read through the bound set must always be what the draw asked for.
void TextureDescriptorReuse(std::mt19937_64& rng) {
  std::vector<uint64_t> last_key;
  bool up_to_date = false;   // the set-values bit (cleared by a new submission)
  std::vector<uint64_t> set_contents;  // what the current set holds
  uint64_t writes = 0, draws = 0;
  std::vector<uint64_t> views = {11, 12, 13, 14, 15, 16};
  for (int draw = 0; draw < 100000; ++draw) {
    if (rng() % 300 == 0) up_to_date = false;  // EndSubmission + BeginSubmission
    uint32_t texture_count = uint32_t(rng() % 4), sampler_count = uint32_t(rng() % 3);
    std::vector<uint64_t> key = {uint64_t(texture_count) | (uint64_t(sampler_count) << 32)};
    bool repeat = rng() % 3 != 0;  // consecutive draws of a material repeat their textures
    for (uint32_t i = 0; i < texture_count; ++i) key.push_back(repeat ? views[i] : views[rng() % views.size()]);
    for (uint32_t i = 0; i < sampler_count; ++i) key.push_back(100 + (repeat ? i : rng() % 5));
    if (key != last_key) {
      up_to_date = false;
      last_key = key;
    }
    if (!up_to_date) {
      set_contents = key;  // allocate + vkUpdateDescriptorSets
      up_to_date = true;
      ++writes;
    }
    ++draws;
    CHECK(set_contents == key);
  }
  CHECK(writes < draws);
}

// --- 0120: constants rewritten with their own values -----------------------------------------------------------

// A write leaves the derived constant buffer valid only when the value is unchanged; the uploaded buffer must
// always equal the registers when a draw reads it.
void UnchangedConstantWrites(std::mt19937_64& rng) {
  std::array<uint32_t, 64> registers{};
  std::array<uint32_t, 64> uploaded{};
  bool up_to_date = false;
  uint64_t skipped = 0;
  for (int step = 0; step < 200000; ++step) {
    if (rng() % 8 == 0) {  // a draw
      if (!up_to_date) {
        uploaded = registers;
        up_to_date = true;
      }
      CHECK(uploaded == registers);
      continue;
    }
    uint32_t index = uint32_t(rng() % registers.size());
    uint32_t value = rng() % 2 ? registers[index] : uint32_t(rng() % 4);  // the game rewrites a lot unchanged
    const bool unchanged = registers[index] == value;  // compared before the write, as in 0120
    registers[index] = value;
    if (unchanged) {
      ++skipped;
      continue;
    }
    up_to_date = false;
  }
  CHECK(skipped > 0);
}

// --- 0122: dynamic uniform buffer ranges stay within the page buffer -------------------------------------------

// Mirror of GraphicsUploadBufferPool::Request's offset choice (page_size is the usable size, the reserved tail
// follows it in the buffer).
struct UploadPool {
  size_t page_size;
  size_t used = 0;
  bool open = false;
  uint64_t pages = 0;
  size_t Request(size_t size, size_t alignment) {
    size = (size + alignment - 1) & ~(alignment - 1);
    size_t aligned = (used + alignment - 1) & ~(alignment - 1);
    if (aligned + size > page_size || !open) {
      open = true;
      ++pages;
      used = 0;
      aligned = 0;
    }
    used = aligned + size;
    return aligned;
  }
};

void DynamicUniformRanges(std::mt19937_64& rng) {
  const uint32_t ranges[5] = {512, 4096, 4096, 160, 768};  // system constants (<= 4096), float VS/PS, bool/loop, fetch
  const size_t range_max = 4096;
  for (size_t alignment : {size_t(16), size_t(64), size_t(256)}) {
    const size_t tail = (range_max + alignment - 1) & ~(alignment - 1);
    const size_t buffer_size = 2 * 1024 * 1024;  // rounded page
    UploadPool pool{buffer_size - tail};
    for (int draw = 0; draw < 100000; ++draw) {
      for (uint32_t binding = 0; binding < 5; ++binding) {
        if (rng() % 3) continue;  // only the changed buffers are uploaded again
        size_t size = binding == 1 || binding == 2 ? 16 * std::max<size_t>(1, rng() % 257) : ranges[binding];
        size_t offset = pool.Request(size, alignment);
        CHECK(offset % alignment == 0);
        CHECK(offset + ranges[binding] <= buffer_size);  // the descriptor's fixed range at this offset
        CHECK(offset + size <= buffer_size - tail);       // the data itself never touches the tail
      }
    }
    CHECK(pool.pages > 1);
  }
}

// --- 0128: occlusion queries in segments inside the guest's render passes ---------------------------------------

// Model of the segmented mode against what the guest asks: the samples of its draws between a query's begin and end.
// Events: guest/transfer pass begin, pass end, a draw (only in a pass), guest query begin, guest query end.
void SegmentedOcclusionQueries(std::mt19937_64& rng) {
  for (int sequence = 0; sequence < 2000; ++sequence) {
    bool in_pass = false, guest_pass = false;
    bool active = false, segment_open = false;
    uint64_t segment_samples = 0, segments_sum = 0, expected = 0;
    for (int step = 0; step < 200; ++step) {
      switch (rng() % 6) {
        case 0:  // a pass begins (another target ends the current one first)
          if (in_pass && segment_open) { segments_sum += segment_samples; segment_open = false; }
          in_pass = true;
          guest_pass = rng() % 4 != 0;  // some passes are render target cache transfers
          if (active && guest_pass) { segment_open = true; segment_samples = 0; }
          break;
        case 1:  // the pass ends
          if (in_pass && segment_open) { segments_sum += segment_samples; segment_open = false; }
          in_pass = false;
          break;
        case 2:
        case 3:  // a draw
          if (in_pass) {
            uint64_t samples = rng() % 1000;
            if (segment_open) segment_samples += samples;
            if (active && guest_pass) expected += samples;
          }
          break;
        case 4:  // guest begin (a begin while active replaces the query)
          if (segment_open) { segment_open = false; }
          active = true;
          segments_sum = expected = 0;
          if (in_pass && guest_pass) { segment_open = true; segment_samples = 0; }
          break;
        default:  // guest end
          if (active) {
            if (segment_open) { segments_sum += segment_samples; segment_open = false; }
            CHECK(segments_sum == expected);
            active = false;
          }
          break;
      }
    }
  }
}

// --- 0129: the game and the plugin agree on HleFrameAbi ------------------------------------------------------------

// The lines of `text` from the one starting with `first` to the next "};" (the struct's body), a leading '+' (patch
// text) and any trailing comment dropped, so the two copies compare line for line.
std::vector<std::string> StructLines(const std::string& text, const std::string& first, bool patch) {
  std::vector<std::string> lines;
  size_t pos = 0;
  bool inside = false;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (patch) {
      if (line.empty() || line[0] != '+') continue;
      line.erase(0, 1);
    }
    if (const size_t comment = line.find("//"); comment != std::string::npos) line.erase(comment);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (!inside && line == first) inside = true;
    if (!inside) continue;
    lines.push_back(line);
    if (line == "};") break;
  }
  return lines;
}

void HleFrameAbiMatches() {
  const std::string p0129 = ReadPatch("0129-");
  const std::string game = check::ReadText(std::filesystem::path(CRACKDOWN_SRC_DIR) / "hle_frame_abi.h");
  const auto plugin_lines = StructLines(p0129, "struct HleFrameAbi {", true);
  const auto game_lines = StructLines(game, "struct HleFrameAbi {", false);
  CHECK(plugin_lines.size() > 2);
  CHECK(plugin_lines == game_lines);
  CHECK(Has(p0129, "+inline constexpr uint32_t kHleFrameAbiVersion = 3;"));
  CHECK(crackdown::kHleFrameAbiVersion == 3);
  // A fixed layout on both 64-bit targets (Windows and the Steam Deck): no padding the two compilers could disagree on.
  static_assert(sizeof(crackdown::HleFrameAbi) == 80);
  static_assert(offsetof(crackdown::HleFrameAbi, groups) == 56);
  static_assert(offsetof(crackdown::HleFrameAbi, pass_names) == 64);
  static_assert(offsetof(crackdown::HleFrameAbi, frame) == 8);
  static_assert(offsetof(crackdown::HleFrameAbi, records) == 16);
  static_assert(offsetof(crackdown::HleFrameAbi, arena) == 32);
  // The exported entry point the game looks up, and the plugin refusing what it cannot read.
  CHECK(Has(p0129, "+extern \"C\" REX_GPU_PLUGIN_EXPORT int rex_gpu_hle_submit_frame("));
  CHECK(Has(p0129, "+  if (frame.version != kHleFrameAbiVersion || frame.struct_size < sizeof(HleFrameAbi) ||"));
}

// --- the patch text still holds what is mirrored here ----------------------------------------------------------

void PatchText() {
  const std::string p0117 = ReadPatch("0117-");
  CHECK(Has(p0117, "+  if (!memexport_ranges_.empty() &&"));
  CHECK(Has(p0117, "IsReadbackMemexportEnabled(REXCVAR_GET(vulkan_readback_memexport))) {"));
  CHECK(Has(p0117, "sampler_parameters_cache_[(hash >> 24) & (kSamplerParametersCacheSize - 1)]"));
  CHECK(Has(p0117, "entry.anisotropic_override == anisotropic_override"));

  const std::string p0119 = ReadPatch("0119-");
  CHECK(Has(p0119, "if (!reuse_texture_descriptors || key != texture_descriptor_keys_[stage]) {"));

  const std::string p0120 = ReadPatch("0120-");
  CHECK(Has(p0120, "register_file_->values[index] == value &&"));
  CHECK(Has(p0120, "InvalidateVertexBufferResidency((index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 2);"));

  const std::string p0122 = ReadPatch("0122-");
  CHECK(Has(p0122, "constants_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;"));
  CHECK(Has(p0122, "buffer_create_info.size = VkDeviceSize(page_size_ + reserved_tail_);"));
  CHECK(Has(p0122, "page_size_ = size_t(allocation_size_) - reserved_tail_;"));
  CHECK(Has(p0122, "buffer_infos[i].range = kConstantBufferDynamicRanges[i];"));
  CHECK(Has(p0122, "binds_constants ? SpirvShaderTranslator::kConstantBufferCount : 0,"));
  // The constants set is rebound with new offsets whenever any constant buffer moved.
  CHECK(Has(p0122, "write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants;"));

  const std::string p0124 = ReadPatch("0124-");
  CHECK(Has(p0124, "+#include <x86intrin.h>"));

  const std::string p0128 = ReadPatch("0128-");
  CHECK(Has(p0128, "+  OcclusionQueryAfterGuestRenderPassBegin();"));
  CHECK(Has(p0128, "+  OcclusionQueryBeforeRenderPassEnd();"));
  CHECK(Has(p0128, "+        samples += results[segment];"));
  CHECK(Has(p0128, "+    FlushQueryCopies();"));

  const std::string p0125 = ReadPatch("0125-");
  CHECK(Has(p0125, "uint32_t run = uint32_t(std::countr_one(bits >> first));"));
  CHECK(Has(p0125, "bits = first + run >= 64 ? 0 : bits & (~uint64_t(0) << (first + run));"));
  CHECK(Has(p0125, "vertex_buffers_in_sync_[word] &= ~mask;"));
}

}  // namespace

int main() {
  std::mt19937_64 rng(0x5EED);
  FloatConstantCopies(rng);
  ResidencyRanges(rng);
  SamplerTableMatchesUncached(rng);
  TextureDescriptorReuse(rng);
  UnchangedConstantWrites(rng);
  DynamicUniformRanges(rng);
  SegmentedOcclusionQueries(rng);
  HleFrameAbiMatches();
  PatchText();
  return check::Finish("vulkan_per_draw_tests");
}
