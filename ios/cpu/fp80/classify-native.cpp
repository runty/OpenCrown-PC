#include <array>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
extern "C" uint32_t oracle144(const void *);
extern "C" uint32_t oracle144_strict(const void *);
extern "C" uint32_t integer144(const void *);
struct Environment {
  uint16_t cw, sw;
  uint32_t mxcsr;
};
static Environment environment() {
  Environment e;
  asm volatile("fnstcw %0; fnstsw %1; stmxcsr %2"
               : "=m"(e.cw), "=m"(e.sw), "=m"(e.mxcsr)::"memory");
  return e;
}
static bool same(Environment a, Environment b) {
  return a.cw == b.cw && a.sw == b.sw && a.mxcsr == b.mxcsr;
}
static uint64_t tests = 0, failed = 0, environmentChecks = 0, inputChecks = 0;
static FILE *vectors = nullptr;
static void check(bool ok, const char *what, uint16_t se = 0,
                  uint64_t sig = 0) {
  ++tests;
  if (!ok && failed++ < 16)
    std::fprintf(stderr, "FAIL %s se%04x sig%016llx\n", what, se,
                 (unsigned long long)sig);
}
static std::array<uint8_t, 16> value(uint16_t se, uint64_t sig) {
  std::array<uint8_t, 16> out;
  out.fill(0xa5);
  memcpy(out.data(), &sig, 8);
  memcpy(out.data() + 8, &se, 2);
  return out;
}
static void one(uint16_t se, uint64_t sig, bool inspectEnvironment) {
  auto raw = value(se, sig), before = raw;
  const unsigned expected = (se & 0x7fff) == 0 && sig && !(sig >> 63);
  Environment env = environment();
  auto ordinary = oracle144(raw.data());
  auto strict = oracle144_strict(raw.data());
  auto integer = integer144(raw.data());
  check(ordinary == expected, "ordinary native LLVM classification", se, sig);
  check(strict == expected, "strict native LLVM classification", se, sig);
  check(integer == expected, "portable integer classification", se, sig);
  check(raw == before, "all80 bits and padding preserved", se, sig);
  ++inputChecks;
  if (inspectEnvironment) {
    check(same(env, environment()), "complete x87 CW/SW and MXCSR unchanged",
          se, sig);
    ++environmentChecks;
  }
  if (vectors && (fwrite(raw.data(), 16, 1, vectors) != 1 ||
                  fwrite(&ordinary, 4, 1, vectors) != 1 ||
                  fwrite(&strict, 4, 1, vectors) != 1))
    std::abort();
}
int main(int argc, char **argv) {
  if (argc != 2 || !(vectors = fopen(argv[1], "wb")))
    return 2;
  fenv_t original;
  std::fegetenv(&original);
  std::feclearexcept(FE_ALL_EXCEPT);
  constexpr uint64_t fractions[] = {0,
                                    1,
                                    2,
                                    0x000fffffffffffffULL,
                                    0x3fffffffffffffffULL,
                                    0x4000000000000000ULL,
                                    0x4000000000000001ULL,
                                    0x7ffffffffffffffeULL,
                                    0x7fffffffffffffffULL};
  auto clean = environment();
  for (unsigned exponent = 0; exponent < 32768; ++exponent)
    for (unsigned sign = 0; sign < 2; ++sign)
      for (unsigned j = 0; j < 2; ++j)
        for (uint64_t fraction : fractions)
          one(uint16_t(exponent | (sign << 15)), fraction | (uint64_t(j) << 63),
              false);
  check(
      same(clean, environment()),
      "exhaustive classification raises no exceptions or mode/status changes");
  ++environmentChecks;
  std::mt19937_64 random(14480);
  for (unsigned i = 0; i < 100000; ++i)
    one(uint16_t(random()), random(), false);
  check(same(clean, environment()),
        "random classification raises no exceptions or mode/status changes");
  ++environmentChecks;
  // Independently vary x87 PC/RC, MXCSR RC, FTZ/DAZ and pre-existing flags.
  for (unsigned pc : {0u, 2u, 3u})
    for (unsigned rc = 0; rc < 4; ++rc)
      for (unsigned sseRC = 0; sseRC < 4; ++sseRC)
        for (bool flush : {false, true})
          for (bool sticky : {false, true}) {
            std::feclearexcept(FE_ALL_EXCEPT);
            if (sticky)
              std::feraiseexcept(FE_INVALID | FE_INEXACT | FE_UNDERFLOW);
            auto old = environment();
            uint16_t cw = uint16_t((old.cw & ~0xf00) | (pc << 8) | (rc << 10));
            uint32_t csr =
                (old.mxcsr & ~0xe040) | (sseRC << 13) | (flush ? 0x8040 : 0);
            asm volatile("fldcw %0; ldmxcsr %1" ::"m"(cw), "m"(csr) : "memory");
            for (unsigned e : {0u, 1u, 2u, 0x3fffu, 0x7ffeu, 0x7fffu})
              for (unsigned sign = 0; sign < 2; ++sign)
                for (unsigned j = 0; j < 2; ++j)
                  for (uint64_t f : fractions)
                    one(uint16_t(e | (sign << 15)), f | (uint64_t(j) << 63),
                        true);
          }
  if (fclose(vectors))
    return 2;
  std::fesetenv(&original);
  std::printf("{\"checks\":%llu,\"failed\":%llu,\"input_cases\":%llu,"
              "\"environment_checks\":%llu,\"mask\":144}\n",
              (unsigned long long)tests, (unsigned long long)failed,
              (unsigned long long)inputChecks,
              (unsigned long long)environmentChecks);
  return failed ? 1 : 0;
}
