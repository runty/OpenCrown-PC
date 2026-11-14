#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" uint64_t real_strlen(const char *);
int main() {
  unsigned passed = 0;
  for (size_t length : {0, 1, 7, 8, 15, 16, 31, 127}) {
    for (size_t offset = 0; offset < 8; ++offset) {
      std::array<char, 256> data;
      data.fill(char(0xa5));
      data[offset + length] = 0;
      if (real_strlen(data.data() + offset) != length)
        return 1;
      ++passed;
    }
  }
  std::printf("{\"native_libc_strlen_passed\":%u}\n", passed);
}
