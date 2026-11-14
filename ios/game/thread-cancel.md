# Per-worker cancellation regression

The real game cancels a worker during startup. A per-thread cancel is a normal
thread exit, not a request to stop the complete translated engine. The runtime
checks `dcp_thread_current_cancelled()` separately from global engine shutdown
and returns the PS4/BSD canceled pointer value **1** to the thread adapter.
The wait-service `ECANCELED` error remains `0x80020055`; it is not the thread's
join result. The independent fixture pins literal 1 rather than deriving its
expected result from the production constant.

The ABI value is defined by the [FreeBSD 9 pthread header](https://raw.githubusercontent.com/freebsd/freebsd-src/releng/9.0/include/pthread.h)
and independently used by [shadPS4's PS4 pthread implementation](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/threads/pthread.cpp).
The earlier fixture's Linux `UINT64_MAX` expectation was incorrect. Results
recorded after this correction supersede the earlier cancellation ABI results.
Result JSON explicitly identifies `canceled_join_value=1`. Sources were checked
on 2026-11-14. `dcp_thread_stopping()` remains useful for native blocking waits,
where it includes both kinds of stop; `dcp_thread_shutdown_requested()` excludes
per-worker cancellation.

The adapter consumes cancellation before invoking TLS destructors. Destructors
receive the original nonzero value after the key has been cleared. A worker
enters a protected finishing state before cleanup, so repeated cancellation
cannot abort a destructor or change an already returned normal result. Global
shutdown deliberately skips guest cleanup; its memory and services may be in
teardown. `cancellation_changed` notifies the host so a parked runtime worker can
wake and observe its own cancellation without waking unrelated work into a
global error path.

Run the independent native service fixture:

```sh
./ios/game/test-thread-cancel.sh /Volumes/office/Build/OpenCrown/thread-cancel-probe
```

The script requires a mounted external output disk. It builds/runs native,
AddressSanitizer plus UndefinedBehaviorSanitizer, and ThreadSanitizer variants,
then compiles the fixture and production thread adapter for arm64 macOS,
iPhoneOS and iPhone Simulator with `-Wall -Wextra -Werror`. Outputs, objects,
JSON and sanitizer logs stay in that external directory.

The fixture has 885 checks, 69 isolated cancellation cases and 16 additional
notification-context retirement generations. It verifies:

- One canceled worker joins with the canceled sentinel while another continues
  advancing and later returns its own normal result.
- Original TLS values reach one completed destructor call each, with no pending
  cancel flag during cleanup and no global adapter error.
- Condition, timed-condition and event waits return their canceled status.
- A parked worker wakes through the cancellation notification hook.
- Normal return remains normal, including 100 repeated cancel requests while
  its destructor is deliberately blocked.
- Three canceling native threads race on each of 64 additional workers without
  repeated cleanup or changed join results.
- Global shutdown releases waiters and skips guest TLS callbacks safely.
- Parallel native cancel/stop callers safely snapshot notification hooks while the
  context remains alive. After successful join, a retired-context sentinel sees
  no more notifications. The context is deleted, repeated stops remain safe,
  and a fresh context is installed for the next generation.
- A failed zero-timeout join retains its callback/context for later stop retries.
  Releasing the blocked worker permits a successful join, which then retires the
  callback before destruction. Notification callbacks reenter the diagnostic
  error getter, confirming the adapter's state lock is not held across callbacks.

A successful join clears the notification callback under the adapter's state
lock. Notifications copy the callback/context together under that lock and invoke
it outside the lock. External native callers must also be quiescent before their
context is destroyed or replaced: worker join covers callbacks executing on
those workers, not arbitrary unrelated native callers still inside an earlier
notification. The fixture joins its native racing callers before retirement.
This boundary is explicit rather than treating a lock-protected pointer copy as
ownership of an already running callback.

All three runtime variants pass with zero sanitizer reports. Both sanitizer logs
are empty. SDK compilation also passes. The run hook is a cooperative native
stand-in for translated execution: it uses public cancellation state and returns
the same sentinel as the real guest wrapper. This isolates thread-adapter
behavior. It does not prove the private `GuestThreadExit` unwind path or complete
Engine isolation by itself; the real-game startup run supplies that integration
evidence. No physical device or CPU-backend change is involved in this fixture.
