// CRILAYLA: CRI's backwards LZ compression used for packed CPK entries.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace dcp::formats {

bool is_crilayla(std::span<const uint8_t> in);

// Returns the 0x100-byte raw prefix followed by the decompressed body. Throws on malformed input.
std::vector<uint8_t> crilayla_decompress(std::span<const uint8_t> in);
// Optional runtime checkpoint. It runs on private decode buffers, outside any
// guest memory lock, and may throw to cancel the operation before publication.
std::vector<uint8_t> crilayla_decompress(std::span<const uint8_t> in,
                                      void (*checkpoint)(void*), void* context);
std::vector<uint8_t> crilayla_decompress(std::span<const uint8_t> in,
                                      void (*checkpoint)(void*), void* context,
                                      void (*token)(void*, bool match, size_t length));

}  // namespace dcp::formats
