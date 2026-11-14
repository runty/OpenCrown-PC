// Headless CPU-side GNM behavior from linux-port/src/hle/gnm.cpp.
// Optional capture callbacks preserve real draw/submission ordering.
#include "gnm-services.h"
#include <atomic>
#include <cstring>
#include <ctime>

namespace {
DcpGnmMemory memory;
DcpGnmCapture capture;
std::atomic<uint64_t> submits{0}, flips{0}, labels{0}, rejected{0};

bool read(uint64_t address, void *destination, size_t bytes) {
  if (address && memory.read &&
      memory.read(memory.context, address, destination, bytes))
    return true;
  ++rejected;
  return false;
}
void write(uint64_t address, const void *source, size_t bytes) {
  if (address && memory.write &&
      memory.write(memory.context, address, source, bytes))
    ++labels;
  else
    ++rejected;
}
bool word(uint64_t base, uint64_t index, uint32_t *value) {
  if (index > (UINT64_MAX - base) / sizeof(uint32_t)) {
    ++rejected;
    return false;
  }
  return read(base + index * sizeof(uint32_t), value, sizeof(*value));
}
void eop(uint32_t selection, uint64_t address, uint32_t lo, uint32_t hi) {
  if (selection == 1)
    write(address, &lo, sizeof(lo));
  else if (selection >= 2 && selection <= 4) {
    uint64_t value = (uint64_t(hi) << 32) | lo;
    if (selection >= 3) {
      timespec time{};
      clock_gettime(CLOCK_MONOTONIC, &time);
      value = (uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec) / 10;
    }
    write(address, &value, sizeof(value));
  }
}
void run_labels(uint64_t base, uint64_t count) {
  if (!base || count > (64u << 20))
    return;
  for (uint64_t index = 0; index < count;) {
    uint32_t header;
    if (!word(base, index, &header))
      return;
    const uint32_t type = header >> 30;
    if (type == 1 || type == 2) {
      ++index;
      continue;
    }
    const uint64_t length = ((header >> 16) & 0x3fff) + 2;
    if (length > count - index)
      return;
    const uint32_t opcode = (header >> 8) & 0xff;
    if (type == 3 && opcode == 0x10 && length >= 4 && capture.marker) {
      uint32_t payload[3];
      if (!read(base + (index + 1) * 4, payload, sizeof(payload)))
        return;
      capture.marker(capture.context, payload, 3);
    }
    if (type == 3 &&
        ((opcode == 0x47 && length >= 6) || (opcode == 0x49 && length >= 7))) {
      uint32_t packet[7]{};
      const size_t words = opcode == 0x47 ? 6 : 7;
      for (size_t i = 1; i < words; ++i)
        if (!word(base, index + i, &packet[i]))
          return;
      if (opcode == 0x47)
        eop(packet[3] >> 29, (uint64_t(packet[3] & 0xffff) << 32) | packet[2],
            packet[4], packet[5]);
      else
        eop(packet[2] >> 29, (uint64_t(packet[4]) << 32) | packet[3], packet[5],
            packet[6]);
    } else if (type == 3 && opcode == 0x37 && length >= 5) {
      uint32_t control, lo, hi;
      if (!word(base, index + 1, &control) || !word(base, index + 2, &lo) ||
          !word(base, index + 3, &hi))
        return;
      const uint32_t destination = (control >> 8) & 0xf;
      if (destination == 1 || destination == 2 || destination == 5) {
        uint64_t address = (uint64_t(hi) << 32) | lo;
        for (uint64_t i = 4; i < length; ++i) {
          uint32_t value;
          if (!word(base, index + i, &value))
            return;
          write(address, &value, sizeof(value));
          if (address > UINT64_MAX - 4) {
            ++rejected;
            return;
          }
          address += 4;
        }
      }
    }
    index += length;
  }
}
void run_submit(uint64_t count, uint64_t dcb, uint64_t sizes) {
  for (uint64_t i = 0; dcb && sizes && i < count && i < 64; ++i) {
    uint64_t command;
    uint32_t bytes;
    if (dcb > UINT64_MAX - i * 8 || sizes > UINT64_MAX - i * 4) {
      ++rejected;
      return;
    }
    if (!read(dcb + i * 8, &command, sizeof(command)) ||
        !read(sizes + i * 4, &bytes, sizeof(bytes)))
      return;
    run_labels(command, bytes / 4);
  }
}
void fill_nop(uint64_t command, uint64_t words) {
  if (!command || words == 0 || words > 0x4000)
    return;
  static thread_local uint32_t packet[0x4000]{};
  packet[0] =
      words == 1 ? 0x80000000u : 0xc0001000u | (uint32_t(words - 2) << 16);
  // Command construction is not a GPU label write, so do not increment that
  // statistic.
  if (!memory.write ||
      !memory.write(memory.context, command, packet, size_t(words * 4)))
    ++rejected;
}
bool named(const char *name, const char *expected) {
  return std::strcmp(name, expected) == 0;
}
} // namespace

void dcp_gnm_init(DcpGnmMemory callbacks, DcpGnmCapture observers) {
  memory = callbacks;
  capture = observers;
  submits = flips = labels = rejected = 0;
}
DcpGnmStats dcp_gnm_stats() {
  return {submits.load(), flips.load(), labels.load(), rejected.load()};
}
bool dcp_gnm_service(const char *name, const uint64_t args[9],
                     uint64_t *result) {
  if (!name || !args || !result)
    return false;
  static const char *packet_calls[] = {
      "sceGnmDrawIndex",      "sceGnmDrawIndexAuto",
      "sceGnmDispatchDirect", "sceGnmSetVsShader",
      "sceGnmSetPsShader350", "sceGnmSetCsShaderWithModifier",
      "sceGnmSetHsShader",    "sceGnmSetLsShader",
      "sceGnmUpdateVsShader", "sceGnmUpdatePsShader350",
      "sceGnmUpdateHsShader", "sceGnmInsertWaitFlipDone"};
  for (const char *packet : packet_calls) {
    if (named(name, packet)) {
      fill_nop(args[0], uint32_t(args[1]));
      if (capture.draw && (named(name, "sceGnmDrawIndex") ||
                           named(name, "sceGnmDrawIndexAuto")))
        capture.draw(capture.context, args[0], uint32_t(args[1]),
                     uint32_t(args[2]),
                     named(name, "sceGnmDrawIndex") ? args[3] : 0,
                     named(name, "sceGnmDrawIndex"));
      *result = 0;
      return true;
    }
  }
  const bool ordinary = named(name, "sceGnmSubmitCommandBuffers") ||
                        named(name, "sceGnmSubmitAndFlipCommandBuffers");
  const bool workload =
      named(name, "sceGnmSubmitCommandBuffersForWorkload") ||
      named(name, "sceGnmSubmitAndFlipCommandBuffersForWorkload");
  if (ordinary || workload) {
    ++submits;
    const unsigned offset = workload ? 1 : 0;
    run_submit(uint32_t(args[offset]), args[offset + 1], args[offset + 2]);
    if (named(name, "sceGnmSubmitAndFlipCommandBuffers") ||
        named(name, "sceGnmSubmitAndFlipCommandBuffersForWorkload")) {
      ++flips;
      if (capture.flip)
        capture.flip(capture.context, uint32_t(args[offset + 6]));
    }
    *result = 0;
    return true;
  }
  if (named(name, "sceGnmDrawInitDefaultHardwareState350") ||
      named(name, "sceGnmDispatchInitDefaultHardwareState") ||
      named(name, "sceGnmDingDong") || named(name, "sceGnmSubmitDone")) {
    *result = 0;
    return true;
  }
  return false;
}
