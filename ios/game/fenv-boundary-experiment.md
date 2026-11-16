# Experimental HLE floating-environment boundary

This is a compile-time experiment on `codex/ios-fenv-runtime`. Normal builds do
not define `DCP_FENV_BOUNDARY_EXPERIMENT`; their Mac runtime object text and
relocations match the reference. No installed app or stable pipeline pin uses
this experiment. It does not update guest MXCSR, model traps or claim CPU
fidelity. See the [boundary fixture](../cpu/review/fenv-boundary/README.md).

With the experiment enabled, `service()` saves guest fenv and starts an isolated
native environment. A nested `runWorker()` callback restores the guest side for
the callback and then restores the native side. Diagnostic counters report
service entries, callbacks and observed restore mismatches. The separate
`DCP_FENV_BOUNDARY_INJECT` flag deliberately raises native invalid and invokes a
single real `scePthreadOnce` guest callback. It is **test-only** and must never
be enabled in a packaged app. The callback points to validated original bytes
`31 c0 c3` at canonical `0x20007bc0` (zero EAX and return).

A private Mac archive made from the qualified NaN candidate differs only in
`runtime.o`: all 50,479 archive members were independently hashed. With both
flags enabled, a real-game Town audit completes 2,224 frames, verifies 25,859
texture-version hits over 118,897,428,480 logical bytes with zero mismatch,
and preserves the disposable R save. The report records 2,335,376 service
entries and one injected callback, with zero observed guest-entry, native-return
or no-callback restore mismatches. Its final Town image was inspected. These
counters cover this route and instrumentation, not all services or callbacks.

The same private archive passes 20 parked-stop, five resume-cycle and two
cancellation/concurrent-report lifecycle sessions with 27 unload handoffs; the
largest observed stop is 40.3141 ms. This fixture uses an empty asset root and
is not an adventure or device-lifecycle test. Both iPhoneOS and Simulator
compile the experimental runtime syntax-only. A Mac thread-CPU microbenchmark
of the scope alone records a median 17.02 ns net over five million-iteration
rounds; it excludes real HLE work, injected instrumentation and device behavior,
so it is not an app performance result.

The tested callback has no FP work, and the earlier standalone fixture exercises
nested callback changes. Actual game callback FP behavior, HLE services that
intentionally change the environment, cancellation/exception paths at every
boundary, direct native helpers that bypass `service`, MXCSR status mapping,
DAZ/FTZ, exception priority, traps and compiler assumptions remain open. The
high service-call count makes a real matched performance comparison essential.
Do not promote or install this experiment on the basis of these checks.
Private evidence is in `mobile-adventure-live/sse-nan-candidate/`
`fenv-runtime-callback/`.
