# MOVBE load/store coverage

The first post-adventure reward run stops at guest `20491ec3`, whose original
instruction is `movbe qword ptr [rcx],rdx`. Remill decodes it but emits unsupported
hypercall 257. Its existing byte-swap semantics are registered only for loads.
`ios/remill-movbe-stores.patch` adds the three missing memory-store registrations
for 16, 32 and 64 bits, using the same checked memory operations. It adds no
special game result, host-endian shortcut, flag update or runtime PC bypass.

The instruction forms and unchanged flags are specified in the
[Intel SDM, MOVBE, PDF page 788](https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf#page=788).

Run on a mounted external build disk:

```sh
python3 ios/cpu/review/movbe.py \
  --backends /Volumes/office/Build/OpenCrown/cpu-backends \
  --output /Volumes/office/Build/OpenCrown/movbe-review
```

The review copies DATAXFER into an isolated tree, applies or reverse-checks the
patch, and builds private semantics and a private lifter. Shared backend source,
semantics and lifter hashes must remain unchanged. Synthetic x86-64 routines run
through Rosetta; their outputs must also match independent byte/register/flag
expectations before they become the arm64 oracle. Both arm64 normal and
ASan/UBSan runs execute the lifted routines. The sanitized IR and runtime use the
same LLVM compiler/runtime rather than mixing Apple's and Homebrew's ASan ABIs.

The 24 routines each run 64 deterministic payloads and every combination of CF,
PF, AF, ZF, SF and OF. They cover both directions and all three widths, low and
high registers, unaligned and scaled addresses, in-place stores and a load whose
address register is also its destination. Comparisons check the complete payload,
source preservation, 16-bit partial-register preservation, 32-bit zero-extension,
address-register preservation and arithmetic flags. Six additional raw forms
exercise the final valid memory word, a one-byte overrun, an address below the
guest span and address overflow, without deliberately faulting the native oracle.

On 2026-11-15 both modes pass 21,552 checks, including 48 boundary checks, with
zero failures. Oracle SHA-256:
`6f07f174c3707ae2b4746ca8c451b37a89c56c4580253a2f6a105fc2b82c67e2`.
This proves instruction behavior in the arm64 host fixtures. Actual post-adventure
execution and iPhone/iPad acceptance remain separate gates.
