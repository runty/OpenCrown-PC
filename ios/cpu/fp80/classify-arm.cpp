#include <array>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" uint32_t oracle144(const void *);
extern "C" uint32_t oracle144_strict(const void *);
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  uint64_t checked = 0, failed = 0, cases = 0;
  fenv_t original;
  std::fegetenv(&original);
  for (int rounding : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    for (bool sticky : {false, true}) {
      FILE *vectors = std::fopen(argv[1], "rb");
      if (!vectors)
        return 2;
      std::fesetround(rounding);
      std::feclearexcept(FE_ALL_EXCEPT);
      if (sticky)
        std::feraiseexcept(FE_INVALID | FE_UNDERFLOW | FE_INEXACT);
      std::array<uint8_t, 18> raw;
      raw.fill(0x5a);
      while (std::fread(raw.data() + 1, 16, 1, vectors)) {
        uint32_t expected[2];
        if (std::fread(expected, sizeof(expected), 1, vectors) != 1)
          return 2;
        const auto before = raw;
        const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
        const auto actual = oracle144(raw.data() + 1);
        const auto strict = oracle144_strict(raw.data() + 1);
        failed += actual != expected[0];
        failed += strict != expected[1];
        failed += raw != before;
        failed += std::fegetround() != rounding ||
                  std::fetestexcept(FE_ALL_EXCEPT) != flags;
        checked += 4;
        ++cases;
      }
      if (std::ferror(vectors) || std::fclose(vectors))
        return 2;
    }
  std::fesetenv(&original);
  std::printf("{\"checks\":%llu,\"failed\":%llu,\"cases\":%llu,\"mask\":144}\n",
              (unsigned long long)checked, (unsigned long long)failed,
              (unsigned long long)cases);
  return failed ? 1 : 0;
}
