#pragma once

#include <cstdint>

#include <rex/ppc.h>

namespace crackdown::hooks {

using GuestFunction = void (*)(PPCContext&, uint8_t*);

// Replacements for guest functions. The host code behind them is the same in every recompiled guest; the bindings in
// guest_bindings_base.cpp and guest_bindings_tu6.cpp say which guest function each one replaces, and `original` is that
// function's own code.
void FilterPadState(PPCContext&, uint8_t*, GuestFunction original);
void CameraConstructed(PPCContext&, uint8_t*, GuestFunction original);
void AudioBarrier(PPCContext&, uint8_t*, GuestFunction original);
void AudioResetEvent(PPCContext&, uint8_t*);
void AudioMixerCallback(PPCContext&, uint8_t*, GuestFunction original);
void WaveInstanceSetup(PPCContext&, uint8_t*, GuestFunction original);
void AudioReleaseHandle(PPCContext&, uint8_t*, GuestFunction original);

void DamageEntry(PPCContext&, uint8_t*, GuestFunction original);
void FireWeaponTracked(PPCContext&, uint8_t*, GuestFunction original);
void VehicleDamage(PPCContext&, uint8_t*, GuestFunction original);
void EngineTorque(PPCContext&, uint8_t*, GuestFunction original);
void WalkVelocity(PPCContext&, uint8_t*, GuestFunction original);
void JumpLaunch(PPCContext&, uint8_t*, GuestFunction original);

void CrtMemcpy(PPCContext&, uint8_t*, GuestFunction original);
void CrtMemmove(PPCContext&, uint8_t*, GuestFunction original);
void CrtMemset(PPCContext&, uint8_t*, GuestFunction original);

void EntityDestroyed(PPCContext&, uint8_t*, GuestFunction original);

void FrameLoop(PPCContext&, uint8_t*, GuestFunction original);
void SimulationUpdate(PPCContext&, uint8_t*, GuestFunction original);
void FrameDelta(PPCContext&, uint8_t*);
void TimingUpdate(PPCContext&, uint8_t*, GuestFunction original);
void FrameTimer(PPCContext&, uint8_t*, GuestFunction original);
void PresentInterval(PPCContext&, uint8_t*, GuestFunction original);
void SwapPacket(PPCContext&, uint8_t*, GuestFunction original);
void SwapCallback(PPCContext&, uint8_t*, GuestFunction original);
void SwapBuilder(PPCContext&, uint8_t*, GuestFunction original);

void GpuFence(PPCContext&, uint8_t*, GuestFunction original);
void GpuPoll(PPCContext&, uint8_t*, GuestFunction original);

void MenuPopupOpen(PPCContext&, uint8_t*, GuestFunction original);
void MenuPopupYes(PPCContext&, uint8_t*, GuestFunction original);
void MenuPopupNo(PPCContext&, uint8_t*, GuestFunction original);
void MenuPopupDestroyed(PPCContext&, uint8_t*, GuestFunction original);
void MenuStringById(PPCContext&, uint8_t*, GuestFunction original);
void MenuBlockDestroyed(PPCContext&, uint8_t*, GuestFunction original);

void AimUpdate(PPCContext&, uint8_t*, GuestFunction original);
void CameraUpdate(PPCContext&, uint8_t*, GuestFunction original);

void EngineMovie(PPCContext&, uint8_t*, GuestFunction original);
void PlayVideo(PPCContext&, uint8_t*, GuestFunction original);
void SequenceFrame(PPCContext&, uint8_t*, GuestFunction original);

void ApplyLanguage(PPCContext&, uint8_t*, GuestFunction original);
void VehicleAction(PPCContext&, uint8_t*, GuestFunction original);

void OrbConstructed(PPCContext&, uint8_t*, GuestFunction original);
void OrbDestroyed(PPCContext&, uint8_t*, GuestFunction original);
void OrbMapViewConstructed(PPCContext&, uint8_t*, GuestFunction original);
void OrbMapViewDestroyed(PPCContext&, uint8_t*, GuestFunction original);
void OrbFrontendScreenConstructed(PPCContext&, uint8_t*, GuestFunction original);
void OrbFrontendScreenDestroyed(PPCContext&, uint8_t*, GuestFunction original);
void OrbListDescriptorShown(PPCContext&, uint8_t*, GuestFunction original);
void OrbListDescriptorPosition(PPCContext&, uint8_t*, GuestFunction original);
void OrbMinimapProjection(PPCContext&, uint8_t*, GuestFunction original);

// HLE renderer capture (src/hle_capture.cpp) and the single-tile scene (src/single_tile_scene.cpp): D3D library
// draw, clear, resolve, GPR allocation, command buffer, render target, tiling, predication and shader literal calls,
// and the title's scene targets, post-process object and entry point. Bound for the base guest only.
void D3dDrawIndexedVertices(PPCContext&, uint8_t*, GuestFunction original);
void D3dDrawVertices(PPCContext&, uint8_t*, GuestFunction original);
void D3dBeginVertices(PPCContext&, uint8_t*, GuestFunction original);
void D3dBeginIndexedVertices(PPCContext&, uint8_t*, GuestFunction original);
void D3dClear(PPCContext&, uint8_t*, GuestFunction original);
void D3dResolve(PPCContext&, uint8_t*, GuestFunction original);
void D3dSetShaderGprAllocation(PPCContext&, uint8_t*, GuestFunction original);
void D3dRunCommandBuffer(PPCContext&, uint8_t*, GuestFunction original);
void SceneTargetsCreate(PPCContext&, uint8_t*, GuestFunction original);
void PostProcessConstructed(PPCContext&, uint8_t*, GuestFunction original);
void D3dCreateRenderTarget(PPCContext&, uint8_t*, GuestFunction original);
void D3dBeginTiling(PPCContext&, uint8_t*, GuestFunction original);
void D3dEndTiling(PPCContext&, uint8_t*, GuestFunction original);
void D3dSetPredication(PPCContext&, uint8_t*, GuestFunction original);
void CommandBufferFixup(PPCContext&, uint8_t*, GuestFunction original);
void D3dBindShaderLiterals(PPCContext&, uint8_t*, GuestFunction original);
void TitleEntry(PPCContext&, uint8_t*, GuestFunction original);

// Guest functions the hooks call that no hook replaces; each build's bindings supply their own (GuestImage::calls).
struct GuestCalls {
  GuestFunction movie_singleton;
  GuestFunction sequence_advance;
};

}  // namespace crackdown::hooks
