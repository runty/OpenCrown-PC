#include "crilayla-native.h"
#include "formats/crilayla.h"
#include <atomic>
#include <cstring>
#include <exception>

extern "C" void dcp_trace_enter(State &, uint64_t);
namespace {
std::atomic<uint64_t> fast{0}, fallback{0}, decodedBytes{0}, inplaceCalls{0};
constexpr uint64_t cap = 32 * 1024 * 1024;
struct Span {
  uint64_t address, size;
};
bool valid(Span s) {
  return s.address && s.size && s.size <= UINT64_MAX - s.address;
}
bool overlaps(Span a, Span b) {
  return a.address < b.address + b.size && b.address < a.address + a.size;
}
uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
struct DecodeInterrupted {};
struct OriginalCollision {};
struct DecodeContext {
  Memory *memory;
  std::exception_ptr error;
  uint64_t compressed = 0, remaining = 0, bits = 0;
};
// Original eager-prefetch collision rule for exactly O=S, D=S+256.
// A strict successful source decode alone is insufficient: original AOT can
// return zero after partial output when its unread compressed bytes collide.
void inplaceToken(void *opaque, bool match, size_t length) {
  auto &c = *static_cast<DecodeContext *>(opaque);
  if (!length || length > c.remaining)
    throw OriginalCollision{};
  const uint64_t extensions =
      match && length >= 44 ? (uint64_t(length) - 44) / 255 + 1 : 0;
  const uint64_t fetched = (c.bits + 32) / 8 + extensions;
  if (fetched > c.compressed ||
      256 + c.remaining - length < c.compressed - fetched)
    throw OriginalCollision{};
  c.remaining -= length;
  c.bits += !match         ? 9
            : length <= 5  ? 16
            : length <= 12 ? 19
            : length <= 43 ? 24
                           : 24 + 8 * extensions;
}
void checkpoint(void *opaque) {
  auto &c = *static_cast<DecodeContext *>(opaque);
  if (c.memory->stopping && c.memory->stopping->load())
    throw DecodeInterrupted{};
  try {
    if (c.memory->cancelled && c.memory->cancelled())
      throw DecodeInterrupted{};
  } catch (const DecodeInterrupted &) {
    throw;
  } catch (...) {
    c.error = std::current_exception();
    throw DecodeInterrupted{};
  }
}
bool tryDecode(State &state, uint64_t pc, Memory &memory) {
  const uint64_t source = state.gpr.rsi.qword,
                 destination = state.gpr.rcx.qword, sp = state.gpr.rsp.qword;
  const uint64_t compressed = uint32_t(state.gpr.rdx.qword),
                 count = uint32_t(state.gpr.r8.qword);
  if (memory.atomicLock.owns_lock() || pc < 0x121 || sp < 48 ||
      sp > UINT64_MAX - 8 || !compressed || compressed > cap || !count ||
      count > cap)
    return false;
  const Span output{destination, count}, stack{sp - 48, 56};
  const Span cpu{reinterpret_cast<uint64_t>(&state), sizeof(state)};
  if (!valid(output) || !valid(stack) || overlaps(output, stack) ||
      overlaps(cpu, output) || overlaps(cpu, stack))
    return false;
  Span input{};
  uint64_t recordEnd = 0;
  bool inplace = false;
  uint64_t ret = 0;
  std::vector<uint8_t> raw;
  try {
    // Admit only the proven original caller, whose next instruction TESTs EAX.
    // Volatile integer registers/RFLAGS are ABI-adapted, not fully emulated.
    guestRead(memory, sp, &ret, 8);
    if (ret != pc - 0x121)
      return false;
    guestValidate(memory, stack.address, stack.size);
    guestValidate(memory, destination, size_t(count));
    std::array<uint8_t, 16> header{};
    // Either the whole record or its byte16 stream can be passed to the raw
    // original leaf. In both cases the checked encoded stream ends at S+C.
    for (uint64_t skip : {uint64_t(0), uint64_t(16)}) {
      if (source < skip)
        continue;
      const uint64_t record = source - skip;
      try {
        guestRead(memory, record, header.data(), header.size());
      } catch (const std::runtime_error &) {
        continue;
      }
      const uint64_t end = uint64_t(le32(header.data() + 12)) + 16;
      if (std::memcmp(header.data(), "CRILAYLA", 8) ||
          le32(header.data() + 8) != count || !le32(header.data() + 12) ||
          end < skip || end - skip != compressed)
        continue;
      input = {record, end + 256};
      recordEnd = end;
      break;
    }
    inplace = valid(input) && destination == input.address + 256;
    if (!valid(input) || (overlaps(input, output) && !inplace) ||
        overlaps(input, stack) || overlaps(cpu, input))
      return false;
    guestValidate(memory, input.address, size_t(input.size));
    raw.resize(size_t(input.size));
    // One synchronized input snapshot; decoding thereafter owns private bytes.
    // This has the same coarser publication policy as native bulk adapters.
    guestRead(memory, input.address, raw.data(), raw.size());
    if (std::memcmp(raw.data(), header.data(), header.size()))
      return false;
  } catch (const std::runtime_error &) {
    return false;
  } catch (const std::bad_alloc &) {
    return false;
  }
  std::vector<uint8_t> decoded;
  DecodeContext context{&memory, {}, recordEnd, count, 0};
  try {
    decoded = dcp::formats::crilayla_decompress(
        raw, checkpoint, &context, inplace ? inplaceToken : nullptr);
    checkpoint(&context);
  } catch (const DecodeInterrupted &) {
    if (context.error)
      std::rethrow_exception(context.error);
    throw std::runtime_error("bootstrap stopped");
  } catch (const OriginalCollision &) {
    return false;
  } catch (const std::runtime_error &) {
    return false;
  } catch (const std::bad_alloc &) {
    return false;
  }
  if (decoded.size() != count + 256 || (inplace && context.remaining))
    return false;
  dcp_trace_enter(state, pc);
  // Recheck all publication spans before any write. Mapping lifetimes are
  // stable until joined unload; return stack is disjoint from source/output.
  guestValidate(memory, stack.address, stack.size);
  guestValidate(memory, destination, size_t(count));
  const uint64_t pushed[] = {state.gpr.rbx.qword, state.gpr.r12.qword,
                             state.gpr.r13.qword, state.gpr.r14.qword,
                             state.gpr.r15.qword, state.gpr.rbp.qword};
  guestWrite(memory, sp - 48, pushed, sizeof(pushed));
  guestWrite(memory, destination, decoded.data() + 256, size_t(count));
  state.gpr.rax.qword = count;
  state.gpr.rsp.qword = sp + 8;
  state.gpr.rip.qword = ret;
  ++memory.returns;
  ++fast;
  decodedBytes.fetch_add(count, std::memory_order_relaxed);
  if (inplace)
    ++inplaceCalls;
  return true;
}
} // namespace
bool dcp_try_native_crilayla(State &state, uint64_t pc, Memory &memory) {
  const bool result = tryDecode(state, pc, memory);
  if (!result)
    ++fallback;
  return result;
}
void dcp_crilayla_reset() {
  fast = 0;
  fallback = 0;
  decodedBytes = 0;
  inplaceCalls = 0;
}
DcpCrilaylaStats dcp_crilayla_stats() {
  return {fast.load(), fallback.load(), decodedBytes.load(),
          inplaceCalls.load()};
}

#if DCP_NATIVE_CRILAYLA
extern "C" Memory *dcp_crilayla_original_2062ce30(State &, uint64_t, Memory *);
extern "C" Memory *sub_2062ce30(State &state, uint64_t pc, Memory *memory) {
  if (dcp_try_native_crilayla(state, pc, *memory))
    return memory;
  return dcp_crilayla_original_2062ce30(state, pc, memory);
}
#endif
