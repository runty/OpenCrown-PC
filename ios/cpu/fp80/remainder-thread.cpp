#include <atomic>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
extern "C" uint32_t review_remainder(unsigned char *, const unsigned char *,
                                     const unsigned char *, uint16_t, unsigned);
extern "C" void review_begin(unsigned);
extern "C" unsigned review_modes(void), review_flags(void);
#pragma pack(push, 1)
struct Row {
  uint16_t cw;
  uint8_t op, pad;
  unsigned char a[10], b[10];
};
#pragma pack(pop)
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END);
  size_t n = ftell(f) / 24;
  rewind(f);
  std::vector<Row> rows(n);
  fread(rows.data(), 24, n, f);
  fclose(f);
  f = fopen(argv[2], "rb");
  std::vector<unsigned char> want(n * 16);
  fread(want.data(), 16, n, f);
  fclose(f);
  std::atomic<unsigned> bad{0}, checks{0};
  std::vector<std::thread> workers;
  for (unsigned t = 0; t < 8; ++t)
    workers.emplace_back([&, t] {
      unsigned char out[16];
      int rounds[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
      unsigned pcs[] = {32, 80, 64, 80}, rms[] = {0, 2, 3, 1};
      for (size_t i = t; i < n; i += 8) {
        auto &r = rows[i];
        fesetround(rounds[t % 4]);
        feclearexcept(FE_ALL_EXCEPT);
        feraiseexcept(FE_INVALID | FE_DIVBYZERO);
        auto round = fegetround(), flags = fetestexcept(FE_ALL_EXCEPT);
        review_begin(31);
        review_remainder(out, r.a, r.b, r.cw, r.op);
        if (memcmp(out, want.data() + 16 * i, 16) || review_flags() != 31 ||
            review_modes() !=
                (pcs[(r.cw >> 8) & 3] | rms[(r.cw >> 10) & 3] << 8) ||
            fegetround() != round || fetestexcept(FE_ALL_EXCEPT) != flags)
          ++bad;
        ++checks;
      }
    });
  for (auto &t : workers)
    t.join();
  printf("{\"threads\":8,\"checks\":%u,\"failures\":%u}\n", checks.load(),
         bad.load());
  return !!bad;
}
