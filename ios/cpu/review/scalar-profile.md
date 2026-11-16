# Scalar access diagnostic

`DCP_SCALAR_PROFILE=1` is a compile-time diagnostic for the native CPU and game
runtime objects. It is absent from normal builds. Do not compare its frame rate,
CPU use or temperature with an uninstrumented build. It changes execution costs
and can change scheduling and contention.

Collection starts at the first verified active Town component update. Each
execution thread owns its counters. Exact read/write counts cover the templated
checked scalar bridges (including their native callers, reported separately).
They exclude bulk copies, snapshots, CAS and the distinct ten-byte FP80 paths.
Entry counts group accesses by the **most recent trace entry**, not necessarily
the active caller after a translated function returns. They are useful context,
not proof of an exact instruction or function attribution.

Every 1,024 scalar accesses, staggered between workers, the diagnostic samples
the existing AccessLock constructor's elapsed acquisition time. It never changes
lock selection, adds try-locks, or bypasses original LOCK exclusion. Operations
already inside original LOCK are counted separately and not latency-sampled.
Counter publication and sampled-site allocation happen after the data lock is
released. Samples include overhead and preemption/scheduler delays; they do not
establish actual contested-lock duration or CPU-time percentages. Periodic
sampling can alias a periodic workload. No extrapolated total wait is reported.

A scoped binding keeps the current guest State alive for sampled RIP reads and
restores the parent binding across nested native guest callbacks and exceptions.
Sampled RIP is the literal guest-state value at bridge entry. Remill can expose
the instruction's fallthrough address: correlate with the original bytes and IR
before identifying an instruction. Module ranges in the final report permit
native addresses to be normalized to guest addresses. Host pages use the
runtime's logical 4 KiB tracking granularity, not the device's physical page size.

Final publication requires all guest workers to have joined. Failed joins return
an unavailable object without inspecting live counters. Reset also requires
quiescence and no live StateScope. Epochs invalidate old TLS owners on rebinding;
their shared ownership prevents premature reclamation. Limits are 128 threads,
65,536 entry records, 4,096 sampled pages and 16,384 sampled RIP sites per thread.
Omission and allocation-failure counters must be checked before interpreting data.
These limits still allow substantial memory use; the diagnostic is intended for
short isolated sessions, never the normal player build.

```sh
python3 ios/cpu/review/scalar-profile.py \
  --output /Volumes/office/Build/OpenCrown/scalar-profile-review \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

The same fixture verifies exact counts and unchanged values on the main thread
and four workers, inactive-start exclusion, original LOCK accounting, nested
State/RIP restoration, refusal to read unjoined counters and quiescent epoch
reset under native, ASan/UBSan and TSan. Disabled Mac, iPhoneOS and Simulator
runtime objects have byte-identical text and relocations to the guarded-cache
reference. Real game and physical-device data are separate from these fixtures.
