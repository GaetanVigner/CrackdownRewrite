#pragma once

namespace crackdown {

// Test branch any360-graphics-test: with any360_gpu set, the GPU goes through Any360 (any360::sys's GraphicsKernel,
// any360::gfx's command processor and Vulkan renderer, frames in a window of its own) instead of the runtime's.
// Call before the title's first Vd* call (OnPostSetup).
void StartAny360GraphicsIfAsked();

}  // namespace crackdown
