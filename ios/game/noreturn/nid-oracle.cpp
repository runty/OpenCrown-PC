#include "loader/nid.h"
#include <cstdio>
int main() {
  // Semantic policy by ABI symbol identity, never a game address.
  constexpr const char *name = "__stack_chk_fail";
  std::printf("%s %s\n", name, dcp::loader::nid_from_name(name).c_str());
}
