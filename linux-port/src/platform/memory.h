#pragma once

#include <cerrno>
#include <cstddef>
#include <sys/mman.h>

namespace dcp::platform {

// Reserve exactly this address without replacing any existing mapping.
inline void* map_exact(void* address, size_t size, int protection) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif
    void* mapped = mmap(address, size, protection, flags, -1, 0);
    if (mapped != MAP_FAILED && mapped != address) {
        munmap(mapped, size);
        errno = EEXIST;
        return MAP_FAILED;
    }
    return mapped;
}

}  // namespace dcp::platform
