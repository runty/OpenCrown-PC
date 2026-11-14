#include "headless-config.h"
#include <iostream>
#include <stdexcept>

int main() {
  unsigned checks = 0;
  auto require = [&](bool okay) {
    ++checks;
    if (!okay)
      throw std::runtime_error("headless configuration fixture failed");
  };
  const auto standard = dcp::game::headless_config(nullptr, nullptr, nullptr);
  require(standard.scene == "TITLE" && standard.trace_budget == 100000000 &&
          standard.vblank_budget == 1800);
  const auto inn = dcp::game::headless_config("INN", "10000000000", "216000");
  require(inn.scene == "INN" && inn.trace_budget == 10000000000 &&
          inn.vblank_budget == 216000);
  for (const char *bad : {"", "0", "-1", "+1", "1 ", " 1", "1x", "1.5",
                          "18446744073709551616", "10000000001"}) {
    bool rejected = false;
    try { dcp::game::headless_config(nullptr, bad, nullptr); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
  }
  for (const char *bad : {"", "0", "-1", "216001", "999999999999999999999"}) {
    bool rejected = false;
    try { dcp::game::headless_config(nullptr, nullptr, bad); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
  }
  for (const char *bad : {"", "title", "INN ", "A/B", "TITLE\n", "ABCDEFGHIJKLMNOP"}) {
    bool rejected = false;
    try { dcp::game::headless_config(bad, nullptr, nullptr); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
  }
  const auto tiny = dcp::game::headless_config("A_1", "1", "1");
  require(tiny.scene == "A_1" && tiny.trace_budget == 1 && tiny.vblank_budget == 1);
  std::cout << "{\"checks\":" << checks << ",\"failed\":0}\n";
}
