# Shared scalar-read lock study

The physical Town profile showed substantial sampled weight in guest scalar
bridges and lock/wait/wake paths. This experiment asks whether parallel reads
of the same stripe help. It is an isolated patch, **not an app change**:
`shared-reader-study.patch` replaces the research runtime's stripe locks with
`std::shared_mutex` and takes shared locks only for scalar reads. Writes,
snapshot operations, other bulk reads and guest atomics remain exclusive;
existing ordered multi-stripe acquisition is retained. This changes allowed
reader interleavings and has not been qualified for every possible guest or
host access pattern.

Run `shared-reader-study.sh NEW_EXTERNAL_OUTPUT BACKEND_ROOT` from a clean
source checkout. It creates a separate runtime copy, applies the patch, runs
the existing memory and snapshot suites, verifies reader overlap and writer
exclusion, performs six alternating synthetic timing pairs, and checks iOS SDK
syntax. It does not alter a backend, app, game data or device. The fixture's
same-address workload is deliberately contended, not a reconstruction of game
thread scheduling. The standalone contention fixture also passed TSan; no
sanitizer can prove absence of all races or starvation.

On the busy development Mac, both initial and reproducibility runs pass 267,130
memory checks per native/ASan+UBSan/TSan mode. Snapshot native/sanitizer/wrap
checks pass and the omitted-writer negative control is rejected. A separate
fixture confirms two readers can overlap and that an exclusive writer and a
later reader block each other. Both iOS SDK syntax checks pass.

The reproducibility run's six-pair medians for 1,000,000 uncontended scalar
reads were 4.24 ms with the existing unfair mutex and 11.28 ms with shared
locks. Four readers of the same address plus one writer took 36.46 ms versus
58.00 ms for the fixed-count mixed workload. Median individual-run maximum
writer waits were 37.8 versus 57.1 microseconds; maxima and noisy host scheduling
are not a fairness or tail-latency qualification. These are synthetic Mac wall
times, not game or iPhone timings. Despite valid parallel-reader behavior,
the large observed overhead does not justify an app integration or physical
benchmark. No performance gain is claimed and the current production locks
remain unchanged.

Evidence: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/shared-reader-study/`
and `/Volumes/office/Build/OpenCrown/mobile-adventure-live/shared-reader-study-repro/`.
