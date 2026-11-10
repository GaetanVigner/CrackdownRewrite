#include "any360_graphics.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "any360/host/rexglue.h"

REXCVAR_DEFINE_BOOL(any360_gpu, false, "GPU",
                    "Run the GPU through Any360 (any360::sys + any360::gfx, its own window) instead of the runtime's");
REXCVAR_DEFINE_STRING(any360_gpu_device, "", "GPU", "Any360: a substring of the Vulkan device name to prefer");
REXCVAR_DEFINE_STRING(any360_gpu_profile, "", "GPU",
                      "Any360: a file for samples of the command processor's thread (empty: none)");
REXCVAR_DEFINE_BOOL(any360_gpu_writes_after_draws, false, "GPU",
                    "Any360: memory writes and interrupts once the draws before them have run (off: as the runtime)");
REXCVAR_DEFINE_BOOL(any360_gpu_fences_after_draws, true, "GPU",
                    "Any360: EVENT_WRITE_SHD fences once the draws before them have run (as the console)");
REXCVAR_DEFINE_STRING(any360_gpu_pipeline_cache, "cache/any360_pipelines.bin", "GPU",
                      "Any360: the file pipelines are kept in across runs (empty: none)");
REXCVAR_DEFINE_INT32(any360_gpu_msaa, 1, "GPU",
                     "Any360: host MSAA, samples a pixel (1, 2, 4, 8), apart from the render resolution (supersampling)");
REXCVAR_DEFINE_BOOL(any360_gpu_async_pipelines, true, "GPU",
                    "Any360: pipelines compiled on threads of their own (a draw waits for none; off: the frame stalls)");
REXCVAR_DEFINE_INT32(any360_gpu_vblank_hz, 0, "GPU",
                     "Any360: vertical blank interrupts a second (0: 60 with vsync, 1000 without)");

// The GPU system calls, overridden in the executable: the runtime's until StartGraphics succeeds, Any360's after.
ANY360_REXGLUE_GRAPHICS_EXPORTS()

namespace crackdown {

void StartAny360GraphicsIfAsked() {
  if (!REXCVAR_GET(any360_gpu)) {
#ifdef ANY360_REXGLUE_START_SAMPLING
    any360::host::rexglue::StartSampling(REXCVAR_GET(any360_gpu_profile));  // the runtime's GPU, profiled the same way
#endif
    return;
  }
  any360::host::rexglue::GraphicsOptions options;
  options.gpu = REXCVAR_GET(any360_gpu_device);
  options.writes_after_draws = REXCVAR_GET(any360_gpu_writes_after_draws);
  options.fences_after_draws = REXCVAR_GET(any360_gpu_fences_after_draws);
  options.profile_path = REXCVAR_GET(any360_gpu_profile);
#ifdef ANY360_REXGLUE_PIPELINE_CACHE  // Any360 with the pipeline cache (the gpu-opt branch until it is merged)
  options.pipeline_cache_path = REXCVAR_GET(any360_gpu_pipeline_cache);
  options.async_pipelines = REXCVAR_GET(any360_gpu_async_pipelines);
#endif
#ifdef ANY360_REXGLUE_MSAA
  options.msaa_samples = uint32_t(std::clamp(REXCVAR_GET(any360_gpu_msaa), 1, 8));
#endif
  options.vblank_hz = double(REXCVAR_GET(any360_gpu_vblank_hz));
  // The launcher's render resolution (the runtime's own setting): host pixels a guest pixel.
  options.resolution_scale =
      uint32_t(std::clamp(std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()), 1, 4));
  options.log = [](const std::string& line) { REXLOG_INFO("{}", line); };
  std::string error;
  if (any360::host::rexglue::StartGraphics(options, error)) {
    REXLOG_INFO("Any360: the GPU runs through any360::sys and any360::gfx");
  } else {
    REXLOG_ERROR("Any360: GPU binding failed, the runtime's GPU stays: {}", error);
  }
}

}  // namespace crackdown
