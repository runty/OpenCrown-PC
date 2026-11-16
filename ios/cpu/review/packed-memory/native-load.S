.intel_syntax noprefix
.text
.globl native_load
.type native_load,@function
native_load:
  push r12
  mov rcx,rdi
  sub rcx,4
  mov r12d,1
  // Exact six bytes at original game ELF offset 0x6373d0.
  .byte 0xc4,0xa1,0x7c,0x10,0x04,0xa1
  vmovdqu [rsi],ymm0
  vzeroupper
  pop r12
  ret
.section .note.GNU-stack,"",@progbits
