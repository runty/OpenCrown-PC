#include "storage-core.h"
#include <cstdio>
#include <exception>
int main(int argc, char **argv) {
  if (argc != 2) {
    std::fputs("Usage: storage-validate GAME_DIRECTORY\n", stderr);
    return 2;
  }
  try {
    dcp::storage::validate_game(argv[1]);
    std::puts("{\"module_hashes\":\"match\",\"archive_sizes\":\"match\","
              "\"archive_toc_bounds\":\"valid\"}");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
