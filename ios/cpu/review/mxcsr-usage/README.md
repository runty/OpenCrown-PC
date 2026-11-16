# Town floating-point-control entry audit

This is an isolated probe, not an app or performance change. `town-entry-probe.patch`
instruments `dcp_trace_enter` on a private copy of the Mac runtime (apply it
with `git apply --unidiff-zero`). It counts
17 AOT trace entry PCs whose translated IR contains calls to
`__remill_fpu_get_rounding` or `__remill_fpu_set_rounding`, plus the known Town
cursor trace `0x20045850` as a positive control. Those helper calls are not
necessarily exclusive to MXCSR; an entry hit alone would not prove a particular
instruction executed.

On 2026-11-16 the regenerated NaN-candidate corpus contained 17 matching IR
files, all mapped to the bundled libc. A muted offscreen Mac Town replay using a
disposable copy of the R save reached Town and completed 2,225 frames. The
positive control was entered 10,774 times; all 17 candidate entries were zero.
The replay verified its checkpoint and unchanged save hash. An earlier run with
no positive control also found zero candidate entries.

Scope: this is evidence only for the exercised Mac Town route, the selected
translated corpus and the tested counters. It does not prove MXCSR instructions
are absent in other scenes, other versions, direct native substitutions, or
other paths. It does not measure iPhone frame rates or justify integrating the
experimental MXCSR adapter. Raw input list, host build, scripts, logs and
verification are under `/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/mxcsr-usage`.
The corpus scan used `rg -l -e 'call.*@__remill_fpu_(get|set)_rounding' traces -g '*.ll'`;
the candidate list is retained beside the experimental MXCSR fixture in
`mxcsr-lift-private-v2/rounding-call-traces.txt`.
