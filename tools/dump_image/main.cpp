// Writes the decrypted, decompressed guest image of an Xbox 360 XEX to a file,
// loading it the same way `rexglue codegen` does (runtime in tool mode).
//   dump_image <game_root> <xex name> <out.bin> [<update_root>]
// With <update_root> the image is patched with <update_root>/Default.xexp, applied the way the Title Update 6
// executable applies it: --update_data_root and --xex_patch=update:\Default.xexp.
// Prints "base size" so analysis scripts know where the image starts.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

using rex::X_STATUS;

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::fprintf(stderr, "usage: dump_image <game_root> <xex name> <out.bin> [<update_root>]\n");
    return 2;
  }
  std::filesystem::path update_root;
  if (argc == 5) {
    update_root = argv[4];
    rex::cvar::SetFlagByName("xex_patch", "update:\\Default.xexp");
  }
  auto runtime = std::make_unique<rex::Runtime>(argv[1], std::filesystem::path(), update_root);
  if (runtime->Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel, .tool_mode = true}) !=
      X_STATUS_SUCCESS) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  if (runtime->LoadXexImage(std::string("game:\\") + argv[2]) != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "XEX load failed\n");
    return 1;
  }
  auto* xex = runtime->kernel_state()->GetExecutableModule()->xex_module();
  const uint32_t base = xex->base_address(), size = xex->image_size();
  std::ofstream out(argv[3], std::ios::binary);
  out.write(reinterpret_cast<const char*>(runtime->virtual_membase() + base), size);
  std::printf("0x%08X 0x%08X\n", base, size);
  return out ? 0 : 1;
}
