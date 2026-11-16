.intel_syntax noprefix
.text
.globl execute_original
.type execute_original,@function
execute_original:
  push rbx
  mov rbx,rdi
  mov rdi,[rbx]
  mov rsi,[rbx+8]
  mov rdx,[rbx+16]
  vmovdqu ymm0,[rbx+72]
  vmovdqu ymm1,[rbx+104]
  vmovdqu ymm2,[rbx+136]
  call original_bytes
  pushfq
  pop r8
  mov [rbx+32],rax
  mov [rbx+40],rdi
  mov [rbx+48],rsi
  mov [rbx+56],rdx
  mov [rbx+64],r8
  vmovdqu [rbx+72],ymm0
  vmovdqu [rbx+104],ymm1
  vmovdqu [rbx+136],ymm2
  vzeroupper
  pop rbx
  ret
original_bytes:
#include "original-bytes.inc"
.section .note.GNU-stack,"",@progbits
