#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#pragma pack(push, 1)
struct Row {
  uint16_t cw;
  uint8_t op, pad;
  unsigned char a[10], b[10];
};
struct Result {
  unsigned char out[10];
  uint16_t flags;
  uint32_t iterations;
  uint16_t cw;
};
#pragma pack(pop)
#ifdef NATIVE
static Result calc(const Row &r) {
  Result out = {};
  unsigned short sw = 0, cw = 0;
  unsigned count = 0;
  asm volatile("fninit; fldcw %0; fldt %1; fldt %2" ::"m"(r.cw), "m"(r.b),
               "m"(r.a)
               : "st", "st(1)");
  do {
    if (r.op)
      asm volatile("fprem; fnstsw %%ax" : "=a"(sw)::"st");
    else
      asm volatile("fprem1; fnstsw %%ax" : "=a"(sw)::"st");
    ++count;
    if (count > 2048) {
      fprintf(stderr, "loop exhausted\n");
      __builtin_trap();
    }
  } while (sw & 0x400);
  asm volatile("fstpt %0; fstp %%st(0); fnstcw %1"
               : "=m"(out.out), "=m"(cw)::"st", "st(1)");
  out.flags = sw & 63;
  out.iterations = count;
  out.cw = cw;
  return out;
}
#else
extern "C" void review_begin(unsigned);
extern "C" uint32_t review_remainder(unsigned char *, const unsigned char *,
                                     const unsigned char *, uint16_t, unsigned);
#endif
int main(int argc, char **argv) {
  FILE *in = fopen(argv[1], "rb");
  if (!in)
    return 2;
#ifdef NATIVE
  FILE *dest = fopen(argv[2], "wb");
  Row r;
  while (fread(&r, sizeof(r), 1, in)) {
    auto o = calc(r);
    fwrite(&o, sizeof(o), 1, dest);
  }
  fclose(dest);
#else
  FILE *ref = fopen(argv[2], "rb");
  Row r;
  Result want;
  uint64_t n = 0, bad = 0, flagbad = 0, denormal = 0, cwbad = 0, maxiter = 0;
  FILE *actual = fopen(argv[3], "wb");
  unsigned char got[16];
  while (fread(&r, sizeof(r), 1, in)) {
    if (!fread(&want, sizeof(want), 1, ref))
      return 3;
    review_begin(0);
    auto flags = review_remainder(got, r.a, r.b, r.cw, r.op);
    fwrite(got, 16, 1, actual);
    ++n;
    maxiter = std::max<uint64_t>(maxiter, want.iterations);
    denormal += !!(want.flags & 2);
    cwbad += want.cw != r.cw;
    if (memcmp(got, want.out, 10)) {
      if (bad++ < 24) {
        fprintf(stderr, "bits row=%llu op=%u cw=%x a=", n, r.op, r.cw);
        for (int i = 9; i >= 0; --i)
          fprintf(stderr, "%02x", r.a[i]);
        fprintf(stderr, " b=");
        for (int i = 9; i >= 0; --i)
          fprintf(stderr, "%02x", r.b[i]);
        fprintf(stderr, " want=");
        for (int i = 9; i >= 0; --i)
          fprintf(stderr, "%02x", want.out[i]);
        fprintf(stderr, " got=");
        for (int i = 9; i >= 0; --i)
          fprintf(stderr, "%02x", got[i]);
        fprintf(stderr, "\n");
      }
    }
    if (flags != want.flags) {
      if (flagbad++ < 8)
        fprintf(stderr, "flags row=%llu native=%x soft=%x\n", n, want.flags,
                flags);
    }
  }
  printf("{\"checks\":%llu,\"bit_failures\":%llu,\"flag_failures_including_"
         "DE\":%llu,\"native_DE_rows\":%llu,\"native_control_failures\":%llu,"
         "\"max_native_iterations\":%llu}\n",
         n, bad, flagbad, denormal, cwbad, maxiter);
  return !!(bad || flagbad || cwbad);
#endif
}
