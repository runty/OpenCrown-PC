#include "offline-services.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
std::array<uint8_t, 128> output;
uint64_t frame = 0;
bool script_active = true;
unsigned checks = 0;
void require(bool okay) {
  ++checks;
  if (!okay)
    throw std::runtime_error("scripted input check " + std::to_string(checks));
}
void *pointer(void *, uint64_t address, size_t size, bool) {
  return address == 0x10001 && size <= 120 ? output.data() + 1 : nullptr;
}
uint64_t service(const char *name, uint64_t first, uint64_t second = 0) {
  uint64_t arguments[6] = {first, second}, result = UINT64_MAX;
  require(dcp_offline_service(name, arguments, &result));
  return result;
}
void read(uint64_t when, uint32_t buttons, uint8_t lx = 128, uint8_t ly = 128,
          uint64_t handle = 1) {
  frame = when;
  output.fill(0xa5);
  require(service("scePadReadState", handle, 0x10001) == 0);
  uint32_t actual;
  std::memcpy(&actual, output.data() + 1, 4);
  require(actual == buttons && output[5] == lx && output[6] == ly);
  require(output[0] == 0xa5 && output[121] == 0xa5 && output[1 + 0x4c] == 1);
}
} // namespace
int main(int argc, char **argv) {
  require(argc == 2);
  DcpOfflineHooks hooks;
  hooks.pointer = pointer;
  hooks.vblank_count = [](void *) { return frame; };
  hooks.scripted_input = true;
  hooks.scripted_input_active = [](void *) { return script_active; };
  hooks.save_root = argv[1];
  hooks.input_script = "# Same desktop syntax, deliberately out of order\n"
                       "30 0\n10 CROSS+LS_LEFT\n20 OPTIONS|LS_DOWN\n"
                       "40 LS_LEFT+LS_UP+RS_RIGHT+RS_DOWN+SQUARE\n"
                       "50 LS_RIGHT+LS_DOWN+RS_LEFT+RS_UP+TRIANGLE\n60 0\n"
                       "65 MAP+SQUARE\n75 0\n80 TOUCHPAD\n90 0\n";
  dcp_offline_init(hooks);
  require(service("scePadOpen", 1000) == 1);
  require(service("scePadOpen", 1001) == 2);
  read(0, 0);
  read(9, 0);
  read(10, 0x4000, 0, 128);
  read(19, 0x4000, 0, 128);
  read(20, 8, 128, 255);
  script_active = false;
  read(20, 0); // A held button/stick releases without disconnecting the pad.
  script_active = true;
  read(20, 8, 128, 255);
  read(29, 8, 128, 255);
  read(30, 0);
  read(40, 0x8000, 0, 0);
  require(output[7] == 255 && output[8] == 255);
  read(50, 0x1000, 255, 255);
  require(output[7] == 0 && output[8] == 0);
  script_active = false;
  read(50, 0);
  require(output[7] == 128 && output[8] == 128);
  script_active = true;
  read(50, 0, 128, 128, 2);
  require(output[7] == 128 && output[8] == 128);
  read(60, 0);
  require(output[7] == 128 && output[8] == 128);
  require(output[1 + 0x34] == 0);
  read(65, 0x108000);
  uint16_t map_x = 0, map_y = 0;
  std::memcpy(&map_x, output.data() + 1 + 0x3c, 2);
  std::memcpy(&map_y, output.data() + 1 + 0x3e, 2);
  require(output[1 + 0x34] == 1 && map_x == 300 && map_y == 471 &&
          output[1 + 0x40] == 0);
  read(74, 0x108000);
  require(output[1 + 0x34] == 1);
  read(75, 0);
  require(output[1 + 0x34] == 0);
  read(80, 0x100000); // The old raw touchpad press remains touch-free.
  require(output[1 + 0x34] == 0);
  read(10000, 0);
  read(10, 0, 128, 128, 2); // Secondary slots are not scripted.
  hooks.input_script = "\n# Explicit idle; never falls back to boot presses\n";
  dcp_offline_init(hooks);
  require(service("scePadOpen", 1000) == 1);
  read(300, 0);
  read(600, 0);
  hooks.input_script.clear();
  dcp_offline_init(hooks);
  require(service("scePadOpen", 1000) == 1);
  read(300, 8);
  read(310, 0);
  read(600, 0x4000);
  read(608, 0);
  hooks.input_script = "10 CROSS\n20 MYSTERY_BUTTON\n";
  bool rejected = false;
  try { dcp_offline_init(hooks); }
  catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("line 2") != std::string::npos;
  }
  require(rejected);
  read(600, 0x4000); // A rejected script does not partially replace the source.
  hooks.input_script = "0 CROSS\n";
  hooks.scene_input_script = "0 LS_RIGHT+RS_LEFT\n5 MAP\n8 0\n";
  hooks.scene_elapsed_frames = [](void *) { return frame - 100; };
  dcp_offline_init(hooks);
  require(service("scePadOpen", 1000) == 1);
  read(99, 0x4000);
  script_active = false;
  read(100, 0, 255);
  require(output[7] == 0 && output[8] == 128);
  read(104, 0, 255);
  read(105, 0x100000);
  require(output[7] == 128 && output[8] == 128 && output[1 + 0x34] == 1);
  read(108, 0);
  require(output[1 + 0x34] == 0);
  script_active = false; // The checkpoint driver must not suppress real input.
  hooks.input_read = [](void *, unsigned, DcpPlatformPad *pad) {
    pad->connected = 1;
    pad->buttons = 0x8000;
    pad->rx = 200;
    pad->ry = 201;
  };
  dcp_offline_init(hooks);
  require(service("scePadOpen", 1000) == 1);
  read(600, 0x8000); // A real producer takes precedence over scripted input.
  require(output[7] == 200 && output[8] == 201);
  uint64_t fatal_arguments[6]{}, fatal_result = 0xa5a5;
  bool fatal_threw = false;
  try {
    dcp_offline_service("__stack_chk_fail", fatal_arguments, &fatal_result);
  } catch (const std::runtime_error &error) {
    fatal_threw = std::string(error.what()) == "guest stack protector check failed";
  }
  require(fatal_threw);
  require(fatal_result == 0xa5a5); // No success result is returned to the guest.
  const auto before_clear = output;
  require(service("0fPn7oELg8E", UINT64_MAX, UINT64_MAX) == 0);
  require(service("0fPn7oELg8E", 0, 0) == 0);
  require(output == before_clear); // Opaque request/future arguments are unused.
  std::cout << "{\"checks\":" << checks << ",\"failed\":0}\n";
}
