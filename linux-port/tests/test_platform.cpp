// Regressions for speculative guest memory access and Darwin socket adapters.
#include "platform/io.h"
#include "platform/context.h"
#include <csignal>
#include "platform/socket.h"

#include <cstdio>
#include <cstring>
#include <sys/mman.h>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static volatile sig_atomic_t traps = 0;
static void on_trap(int, siginfo_t*, void* context) {
#ifdef __APPLE__
    DcpRegisters registers(context);
#else
    auto registers = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
#endif
    ++traps;
    if (traps & 1) registers[REG_EFL] |= 0x100;
    else registers[REG_EFL] &= ~greg_t(0x100);
}

static void test_single_step() {
    struct sigaction handler{}, previous{};
    handler.sa_sigaction = on_trap;
    handler.sa_flags = SA_SIGINFO;
    sigemptyset(&handler.sa_mask);
    CHECK(sigaction(SIGTRAP, &handler, &previous) == 0);
    const size_t size = size_t(sysconf(_SC_PAGESIZE));
    auto* code = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    CHECK(code != MAP_FAILED);
    if (code != MAP_FAILED) {
        code[0] = 0xcc; // int3, followed by a single stepped nop and ret
        code[1] = 0x90;
        code[2] = 0xc3;
        CHECK(mprotect(code, size, PROT_READ | PROT_EXEC) == 0);
        for (int i = 0; i < 16; ++i) reinterpret_cast<void (*)()>(code)();
        CHECK(traps == 32);
        munmap(code, size);
    }
    CHECK(sigaction(SIGTRAP, &previous, nullptr) == 0);
}

int main() {
    test_single_step();
    const size_t size = size_t(sysconf(_SC_PAGESIZE));
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED);
    if (memory == MAP_FAILED) return 1;
    const uint64_t address = reinterpret_cast<uint64_t>(memory);
    uint64_t input = 0x12345678, output = 0;
    CHECK(dcp::platform::write_memory(address, &input, sizeof input));
    CHECK(dcp::platform::read_memory(address, &output, sizeof output) && output == input);
    CHECK(mprotect(memory, size, PROT_READ) == 0);
    CHECK(!dcp::platform::write_memory(address, &input, sizeof input));
    CHECK(mprotect(memory, size, PROT_NONE) == 0);
    CHECK(!dcp::platform::read_memory(address, &output, sizeof output));
    CHECK(munmap(memory, size) == 0);
    CHECK(!dcp::platform::read_memory(address, &output, sizeof output));
    CHECK(!dcp::platform::read_memory(1, &output, sizeof output));

    int socket = dcp::platform::socket_cloexec(AF_INET, SOCK_STREAM, 0);
    CHECK(socket >= 0);
    if (socket >= 0) {
        CHECK((fcntl(socket, F_GETFD) & FD_CLOEXEC) != 0);
        close(socket);
    }
#ifdef __APPLE__
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    CHECK(dcp::platform::prepare_socket(pair[0]) == pair[0]);
    close(pair[1]);
    // The default SIGPIPE disposition would terminate this test if suppression failed.
    CHECK(send(pair[0], &input, sizeof input, 0) == -1 && errno == EPIPE);
    close(pair[0]);
#endif
    std::printf("platform tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
