# Bounded native tail dispatch proof

The real-game SIGBUS was a native stack overflow, rather than a guest-data
pointer fault. The recorded crash reported 16,587 recursive `__remill_jump`
frames. Its native implementation retained 32 bytes around a non-tail call to
dispatch because of the active try/catch. Roughly 531 KB accumulated on the
worker's approximately 544 KB stack. The faulting instruction was the first
stack-saving STP in `dcp_thread_stopping`, inside the stack guard.

The matching executable UUID was
`4FE3E9EC-8BEE-3286-B4CC-9477487DB8CE`, SHA-256
`d1cd3d3c405bb21c1fcf4f1d27fc604227f6061b2e5f9c271eb8e28611eae97b`.
The root task preserved that artifact. Its generated traces already restored
their native frames and branched to `__remill_jump`, and dispatch restored its
frame before branching to the trace. The retained bridge frame caused growth.

The production fix adds `[[clang::musttail]] return dispatch(...)` before the
catch scope when `Memory::stopOnFault` is true in jump, missing-block and
function-call bridges. The latter still increments its call counter first.
The synthetic false path retains fault/status recording. Production dispatch
likewise uses exact-three-argument musttail returns for services and trace
function pointers. No larger native stack is required to hide tail-jump growth.

## Independent regression

```sh
python3 ios/cpu/review/tail-dispatch.py \
  --output /Volumes/office/Build/OpenCrown/tail-dispatch-probe \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

The fixture lifts owned synthetic x86-64 routines through the pinned Remill
backend and instruments their actual trace entries. The two-block loop executes
`jmp rsi` / `jmp rdi`, alternating translated targets. It runs 1,100,003 entries
per mode on a pthread requested with a 128 KiB stack:

| Mode | Translated entries | pthread-reported stack | Entry-SP span |
| --- | --- | --- | --- |
| Ordinary translated indirect jumps | 1,100,003 | 143,360 bytes | 0 bytes |
| Same trace loop routed through missing-block bridge | 1,100,003 | 143,360 bytes | 0 bytes |
| Same trace loop routed through function-call bridge | 1,100,003 | 143,360 bytes | 0 bytes |

The latter two modes deliberately insert the bridge into fixture dispatch
before resolving the real translated target; they test bridge boundedness,
not a million nested guest CALL instructions. The call bridge's counter is
exactly 1,100,003. A controlled exception terminates each loop and reaches its
outer worker boundary without synthetic fault recording.

Separate translated direct and indirect x86 CALL/RET routines verify that the
caller resumes its arithmetic after the callee returns, and that the saved
guest stack, return PC and call/return counters remain correct. Missing-target
tests cover all three bridges with both `stopOnFault` settings. The fixture
dispatcher mirrors the production three-argument service/trace signature and
also exercises a native service target.

The complete native suite passed **127 checks with zero failures**. Core
runtime, translated routines and fixture dispatcher cross compiled for arm64
macOS, iPhoneOS and Simulator. Relocation-bearing disassembly verified:

- A native **B dispatch**, rather than BL, in each production core bridge's
  true fast path.
- An indirect **BR** for the fixture trace target.
- A direct **B** for the fixture service target.

No musttail signature or active-cleanup constraint was encountered. The test
does not compile the entire game Engine dispatcher; the root game build owns
its final assembly verification. The synthetic false path intentionally retains
its original catch behavior and is not the unbounded production-loop path.

`results.json` contains exact runtime/header/fixture/runner hashes, lifter and
semantics-bitcode hashes, generated-IR hashes, native branch checks and output.
The runner rejects a backend change during fixture generation. Complete
assembly and compiler logs remain in the selected external output directory.
No original game instruction bytes, source edits outside this fixture, physical
device actions or game-archive rebuilds are involved in this regression.

The root integration review also disassembled the frozen 5,435-entry real-game
`runtime.o` and verified B to its native service and BR to its trace pointer.
`production-game-dispatch.{asm,json}` in the same external probe directory
record the object hash and those checks.
