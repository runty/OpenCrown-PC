#include "status.h"
#include <cstdint>
#include <stdexcept>
extern "C" void __dcp_experimental_mxcsr_load(uint32_t value) {
  // Supported fixture scope: all exceptions masked, DAZ/FTZ off, no reserved
  // bits.
  if ((value & 0xffff0000u) || (value & 0x8040u) ||
      (value & 0x1f80u) != 0x1f80u)
    throw std::runtime_error("unsupported experimental MXCSR control");
  experimental_mxcsr::write_fpsr(
      experimental_mxcsr::load_fpsr(value, experimental_mxcsr::read_fpsr()));
}
extern "C" uint32_t __dcp_experimental_mxcsr_store(uint32_t guest) {
  return experimental_mxcsr::store(guest, experimental_mxcsr::read_fpsr());
}
