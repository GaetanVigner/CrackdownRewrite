#pragma once

#include <cstdint>

#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace crackdown {

// HLE renderer milestone 1: with crackdown_hle_capture on, crackdown_hle_auto_capture records the next frames twice, from the
// two ends of the same command stream. The game side (hooks on the D3D draw functions) writes, for
// every DRAW packet the game's D3D device emits, the device's shadow state right after the call to
// logs/hle_game_<n>.bin; the GPU side (gpu_draw_trace, SDK patch 0108) writes the register state the
// command processor sees for every DRAW it executes to logs/hle_gpu_<n>.bin. Both use the same record
// layout and are matched by the DRAW packet's guest physical address (tools/hle-trace-compare.ps1).

// Whether crackdown_hle_capture is on.
bool HleCaptureEnabled();

// Arms a capture starting at the next game frame. False when one is already running.
bool StartHleCapture();

// Called at every main-loop frame of the title.
void HleCaptureFrame();

// H1a records: decodes the LOAD_ALU_CONSTANT packets a shader bind (sub_826C6A50, r3 = device) wrote to the ring
// since `pre` (the write pointer before it; `segment_end` the ring segment's end then) into literal loads.
void HleNoteLiteralPackets(uint8_t* base, uint32_t device, uint32_t pre, uint32_t segment_end);


}  // namespace crackdown
