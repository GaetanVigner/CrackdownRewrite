// Which guest function of the Title Update 6 guest each host replacement takes over.
// The jump height cheat has no binding: its launch velocity is no longer a field the host can scale.
#include "guest_binding.h"

CRACKDOWN_BIND(827CCB18, FilterPadState)
CRACKDOWN_BIND(822AB4D0, CameraConstructed)
CRACKDOWN_BIND(82AD8EB8, AudioBarrier)
CRACKDOWN_BIND_REPLACEMENT(821512C0, AudioResetEvent)
CRACKDOWN_BIND(82AD4E80, AudioMixerCallback)
CRACKDOWN_BIND(8227F8A0, WaveInstanceSetup)
CRACKDOWN_BIND(82AD2860, AudioReleaseHandle)
CRACKDOWN_BIND(822CC508, DamageEntry)
CRACKDOWN_BIND(82390340, FireWeaponTracked)
CRACKDOWN_BIND(82394040, FireWeaponTracked)
CRACKDOWN_BIND(82527AC8, VehicleDamage)
CRACKDOWN_BIND(8250EBC8, VehicleDamage)
CRACKDOWN_BIND(82514B78, VehicleDamage)
CRACKDOWN_BIND(82533750, VehicleDamage)
CRACKDOWN_BIND(824F1660, EngineTorque)
CRACKDOWN_BIND(8255ED88, WalkVelocity)
CRACKDOWN_BIND(82B475B0, CrtMemcpy)
CRACKDOWN_BIND(82B3F930, CrtMemmove)
CRACKDOWN_BIND(82B47460, CrtMemset)
CRACKDOWN_BIND(8254B008, EntityDestroyed)
CRACKDOWN_BIND(8272F7C0, FrameLoop)
CRACKDOWN_BIND(8272ED00, SimulationUpdate)
CRACKDOWN_BIND_REPLACEMENT(825B7020, FrameDelta)
CRACKDOWN_BIND(82358AD0, TimingUpdate)
CRACKDOWN_BIND(8235A750, FrameTimer)
CRACKDOWN_BIND(8273A4E8, PresentInterval)
CRACKDOWN_BIND(82745900, SwapPacket)
CRACKDOWN_BIND(82745768, SwapCallback)
CRACKDOWN_BIND(82745E30, SwapBuilder)
CRACKDOWN_BIND(82744888, GpuFence)
CRACKDOWN_BIND(82745278, GpuPoll)
CRACKDOWN_BIND(826F5250, MenuPopupOpen)
CRACKDOWN_BIND(826D1A70, MenuPopupYes)
CRACKDOWN_BIND(826D1928, MenuPopupNo)
CRACKDOWN_BIND(826D17D8, MenuPopupDestroyed)
CRACKDOWN_BIND(823C1B80, MenuStringById)
CRACKDOWN_BIND(826F4180, MenuBlockDestroyed)
CRACKDOWN_BIND(82560740, AimUpdate)
CRACKDOWN_BIND(822AE1E0, CameraUpdate)
CRACKDOWN_BIND(82730A70, EngineMovie)
CRACKDOWN_BIND(82696DA0, PlayVideo)
CRACKDOWN_BIND(826CD158, SequenceFrame)
CRACKDOWN_BIND(823C1188, ApplyLanguage)
CRACKDOWN_BIND(824F4600, VehicleAction)

CRACKDOWN_BIND(8254EF28, OrbConstructed)
CRACKDOWN_BIND(8254D8F8, OrbDestroyed)
CRACKDOWN_BIND(826B3E98, OrbMapViewConstructed)
CRACKDOWN_BIND(826B4848, OrbMapViewDestroyed)
CRACKDOWN_BIND(8269A8C0, OrbFrontendScreenConstructed)
CRACKDOWN_BIND(8269A9D8, OrbFrontendScreenDestroyed)
CRACKDOWN_BIND(824324E0, OrbListDescriptorShown)
CRACKDOWN_BIND(82432728, OrbListDescriptorPosition)
CRACKDOWN_BIND(82432CD8, OrbMinimapProjection)

CRACKDOWN_GUEST_CALL(82363DE8)
CRACKDOWN_GUEST_CALL(826CD5E0)

namespace {
const crackdown::hooks::GuestCalls kCalls = {
    CRACKDOWN_GUEST_ORIGINAL(82363DE8),
    CRACKDOWN_GUEST_ORIGINAL(826CD5E0),
};
}  // namespace

const crackdown::GuestImage crackdown::guests::kTitleUpdate6 = {
    crackdown::content::GuestBuild::kTitleUpdate6,
    "Title Update 6",
    &PPCImageConfig,
    &kCalls,
};
