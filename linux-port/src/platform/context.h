#pragma once
#ifdef __APPLE__
#include <sys/ucontext.h>
#include <cstdint>
enum {
    REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_RBP, REG_RSP,
    REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
    REG_RIP, REG_EFL, REG_ERR, DCP_REGISTER_COUNT
};
using greg_t = uint64_t;

// Snapshot the Darwin context and write back only the registers changed by probes.
class DcpRegisters {
    ucontext_t* context_;
    uint64_t values_[DCP_REGISTER_COUNT];
public:
    explicit DcpRegisters(void* context) : context_(static_cast<ucontext_t*>(context)) {
        values_[REG_RAX] = context_->uc_mcontext->__ss.__rax;
        values_[REG_RBX] = context_->uc_mcontext->__ss.__rbx;
        values_[REG_RCX] = context_->uc_mcontext->__ss.__rcx;
        values_[REG_RDX] = context_->uc_mcontext->__ss.__rdx;
        values_[REG_RSI] = context_->uc_mcontext->__ss.__rsi;
        values_[REG_RDI] = context_->uc_mcontext->__ss.__rdi;
        values_[REG_RBP] = context_->uc_mcontext->__ss.__rbp;
        values_[REG_RSP] = context_->uc_mcontext->__ss.__rsp;
        values_[REG_R8] = context_->uc_mcontext->__ss.__r8;
        values_[REG_R9] = context_->uc_mcontext->__ss.__r9;
        values_[REG_R10] = context_->uc_mcontext->__ss.__r10;
        values_[REG_R11] = context_->uc_mcontext->__ss.__r11;
        values_[REG_R12] = context_->uc_mcontext->__ss.__r12;
        values_[REG_R13] = context_->uc_mcontext->__ss.__r13;
        values_[REG_R14] = context_->uc_mcontext->__ss.__r14;
        values_[REG_R15] = context_->uc_mcontext->__ss.__r15;
        values_[REG_RIP] = context_->uc_mcontext->__ss.__rip;
        values_[REG_EFL] = context_->uc_mcontext->__ss.__rflags;
        values_[REG_ERR] = context_->uc_mcontext->__es.__err;
    }
    DcpRegisters(const DcpRegisters&) = delete;
    DcpRegisters& operator=(const DcpRegisters&) = delete;
    ~DcpRegisters() {
        context_->uc_mcontext->__ss.__rip = values_[REG_RIP];
        context_->uc_mcontext->__ss.__rflags = values_[REG_EFL];
    }
    uint64_t& operator[](int reg) { return values_[reg]; }
};
#else
#include <ucontext.h>
#endif
