// Synthetic full-ten-byte x87 oracle / concurrent portable helper checker.
#include "fp80.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using Bytes = std::array<uint8_t, 16>;
static Bytes decode(const std::string &text) {
  Bytes out{};
  for (size_t i = 0; i < text.size() / 2 && i < out.size(); ++i)
    out[i] = uint8_t(std::stoul(text.substr(i * 2, 2), nullptr, 16));
  return out;
}
static std::string encode(const Bytes &value) {
  char result[21];
  for (unsigned i = 0; i < 10; ++i)
    std::snprintf(result + i * 2, 3, "%02x", value[i]);
  return result;
}
#ifdef NATIVE_ORACLE
static Bytes operate(unsigned op, uint16_t cw, const Bytes &a, const Bytes &b) {
  Bytes out{};
  uint16_t old;
  asm volatile("fnstcw %0" : "=m"(old));
  asm volatile("fnclex; fldcw %0" ::"m"(cw));
  switch (op) {
  case 0:
    asm volatile("fldt %2; fldt %1; fadd %%st(1),%%st; fstpt %0; fstp %%st(0)"
                 : "=m"(out)
                 : "m"(a), "m"(b)
                 : "st", "st(1)");
    break;
  case 1:
    asm volatile("fldt %2; fldt %1; fsub %%st(1),%%st; fstpt %0; fstp %%st(0)"
                 : "=m"(out)
                 : "m"(a), "m"(b)
                 : "st", "st(1)");
    break;
  case 2:
    asm volatile("fldt %2; fldt %1; fmul %%st(1),%%st; fstpt %0; fstp %%st(0)"
                 : "=m"(out)
                 : "m"(a), "m"(b)
                 : "st", "st(1)");
    break;
  case 3:
    asm volatile("fldt %2; fldt %1; fdiv %%st(1),%%st; fstpt %0; fstp %%st(0)"
                 : "=m"(out)
                 : "m"(a), "m"(b)
                 : "st", "st(1)");
    break;
  case 4:
    asm volatile("fldt %2; fldt %1; fscale; fstpt %0; fstp %%st(0)"
                 : "=m"(out)
                 : "m"(a), "m"(b)
                 : "st", "st(1)");
    break;
  case 5:
    asm volatile("fldt %1; fsqrt; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 6:
    asm volatile("fildq %1; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 7: {
    uint16_t full = uint16_t(cw | 0x300);
    asm volatile("fldcw %0" ::"m"(full));
    uint64_t value;
    memcpy(&value, a.data(), 8);
    long double result = (long double)value;
    memcpy(out.data(), &result, 10);
    break;
  }
  case 8:
    asm volatile("flds %1; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 9:
    asm volatile("fldl %1; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 10:
    asm volatile("fldt %1; fstps %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 11:
    asm volatile("fldt %1; fstpl %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 12:
    asm volatile("fldt %1; fisttpq %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 14:
  case 15:
  case 16:
  case 17: {
    uint16_t rounding = uint16_t((cw & ~0xc00) | ((op == 14   ? 3
                                                   : op == 15 ? 1
                                                   : op == 16 ? 2
                                                              : 0)
                                                  << 10));
    asm volatile("fldcw %2; fldt %1; frndint; fstpt %0"
                 : "=m"(out)
                 : "m"(a), "m"(rounding)
                 : "st");
    break;
  }
  case 18:
    asm volatile("fldt %1; fchs; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  case 19:
    asm volatile("fldt %1; fabs; fstpt %0" : "=m"(out) : "m"(a) : "st");
    break;
  default: {
    if (op < 20 || op > 35)
      std::abort();
    long double x, y;
    memcpy(&x, a.data(), 10);
    memcpy(&y, b.data(), 10);
    bool u = std::isunordered(x, y), eq = x == y, lt = x<y, gt = x> y;
    const bool results[] = {false,     eq,
                            gt,        gt || eq,
                            lt,        lt || eq,
                            !u && !eq, !u,
                            u,         u || eq,
                            u || gt,   u || gt || eq,
                            u || lt,   u || lt || eq,
                            !eq,       true};
    out[0] = results[op - 20];
    break;
  }
  }
  asm volatile("fnclex; fldcw %0" ::"m"(old));
  return out;
}
#else
#ifdef LOWERED_FIXTURES
extern "C" void dcp_lower_fixture(void *, const void *, const void *, unsigned);
#endif
static Bytes operate(unsigned op, uint16_t cw, const Bytes &a, const Bytes &b) {
  Bytes out{};
  uint64_t value = 0;
  std::memcpy(&value, a.data(), 8);
  dcp_fp80_set_control(cw);
#ifdef LOWERED_FIXTURES
  dcp_lower_fixture(out.data(), a.data(), b.data(), op);
#else
  if (op < 5)
    dcp_fp80_binary(out.data(), a.data(), b.data(), op);
  else
    switch (op) {
    case 5:
      dcp_fp80_unary(out.data(), a.data(), 2);
      break;
    case 6:
      dcp_fp80_from_i64(out.data(), int64_t(value));
      break;
    case 7:
      dcp_fp80_from_u64(out.data(), value);
      break;
    case 8:
      dcp_fp80_from_f32(out.data(), uint32_t(value));
      break;
    case 9:
      dcp_fp80_from_f64(out.data(), value);
      break;
    case 10: {
      auto result = dcp_fp80_to_f32(a.data());
      std::memcpy(out.data(), &result, 4);
      break;
    }
    case 11: {
      auto result = dcp_fp80_to_f64(a.data());
      std::memcpy(out.data(), &result, 8);
      break;
    }
    case 12: {
      auto result = dcp_fp80_to_int(a.data(), 0);
      std::memcpy(out.data(), &result, 8);
      break;
    }
    case 14:
    case 15:
    case 16:
    case 17:
      dcp_fp80_unary(out.data(), a.data(), op - 11);
      break;
    case 18:
      dcp_fp80_unary(out.data(), a.data(), 0);
      break;
    case 19:
      dcp_fp80_unary(out.data(), a.data(), 1);
      break;
    default:
      if (op < 20 || op > 35)
        std::abort();
      out[0] = uint8_t(dcp_fp80_compare(a.data(), b.data(), op - 20));
      break;
    }
#endif
  return out;
}
#endif
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream file(argv[1]);
  if (!file)
    return 2;
  struct Case {
    unsigned op;
    uint16_t cw;
    Bytes a, b, expected;
  };
  std::vector<Case> cases;
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream row(line);
    unsigned op, cw;
    std::string a, b, expected;
    row >> op >> cw >> a >> b;
#ifdef NATIVE_ORACLE
    std::cout << line << ' '
              << encode(operate(op, uint16_t(cw), decode(a), decode(b)))
              << '\n';
#else
    row >> expected;
    cases.push_back({op, uint16_t(cw), decode(a), decode(b), decode(expected)});
#endif
  }
#ifndef NATIVE_ORACLE
  std::atomic<unsigned> failed{0};
  std::atomic<unsigned> byOperation[36]{};
  std::vector<std::thread> threads;
  for (unsigned thread = 0; thread < 8; ++thread)
    threads.emplace_back([&, thread] {
      for (size_t i = thread; i < cases.size(); i += 8) {
        auto &c = cases[i];
        auto actual = operate(c.op, c.cw, c.a, c.b);
        if (std::memcmp(actual.data(), c.expected.data(), 10)) {
          ++byOperation[c.op];
          if (failed.fetch_add(1) < 20)
            std::fprintf(stderr,
                         "row%zu op%u cw%04x a%s b%s expected%s actual%s\n", i,
                         c.op, c.cw, encode(c.a).c_str(), encode(c.b).c_str(),
                         encode(c.expected).c_str(), encode(actual).c_str());
        }
      }
    });
  for (auto &thread : threads)
    thread.join();
  for (unsigned op = 0; op < 36; ++op)
    if (byOperation[op])
      std::fprintf(stderr, "op%u mismatches%u\n", op, byOperation[op].load());
  std::printf(
      "{\"checks\":%zu,\"failed\":%u,\"threads\":8,\"comparison_bits\":80}\n",
      cases.size(), failed.load());
  return failed ? 1 : 0;
#endif
}
