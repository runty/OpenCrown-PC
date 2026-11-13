#pragma once
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#else
#include <sys/uio.h>
#endif

namespace dcp::platform {
inline bool read_memory(uint64_t address, void* output, size_t size) {
#ifdef __APPLE__
    mach_vm_size_t copied = 0;
    return mach_vm_read_overwrite(mach_task_self(), address, size,
        reinterpret_cast<mach_vm_address_t>(output), &copied) == KERN_SUCCESS && copied == size;
#else
    iovec local{output, size}, remote{reinterpret_cast<void*>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
#endif
}
inline bool write_memory(uint64_t address, const void* input, size_t size) {
#ifdef __APPLE__
    if (size > UINT32_MAX) return false;
    return mach_vm_write(mach_task_self(), address,
        reinterpret_cast<vm_offset_t>(input), mach_msg_type_number_t(size)) == KERN_SUCCESS;
#else
    iovec local{const_cast<void*>(input), size}, remote{reinterpret_cast<void*>(address), size};
    return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
#endif
}
inline uint64_t thread_id() {
#ifdef __APPLE__
    uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return id;
#else
    return uint64_t(gettid());
#endif
}
}
