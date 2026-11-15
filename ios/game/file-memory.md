# Checked native file memory

`DcpFileHooks` can supply `validate_memory`, `read_memory` and `write_memory`
together. The game runtime supplies all three, delegating to `guestValidate`,
`guestRead` and `guestWrite`. Validation returns no mutable guest pointer.
The read callback copies guest bytes to private host storage; the write callback
publishes host bytes to guest memory while holding the usual access locks.
Original guest LOCK exclusion therefore also covers native file output.

File `read`/`pread` and `write`/`pwrite` stage at most 256 KiB per active transfer.
The descriptor mutex still serializes position/append operations, and the save
service admission gate still spans the entire call. Guest access locks cover
only memory validation/copying, never blocking host I/O. The original 64 MiB
validation windows remain: a rejected window cannot advance its file offset.
Each successful read publishes only the bytes returned by the host, so EOF
does not overwrite the rest of a guest buffer. Position and error conventions
retain the adapter's existing behavior.

Path strings and `stat`/`fstat` results also use the copy callbacks. Passing only
some callbacks is rejected at initialization. Callers that supply none retain
the legacy pointer path; that path cannot be treated as version-tracked memory.
All callers and the file adapter must be rebuilt together after the C++ hook
structure changes. The translated program corpus does not need regeneration.

Final game diagnostics include `file_memory.synchronized_copies`, `read_bytes`
and `write_bytes`, captured before file-service reset. Byte directions refer
to guest memory: a file read increases `write_bytes`; writing a file increases
`read_bytes`. Path/stat copies contribute too. These count successful logical
copies, not storage/DRAM traffic or an atomic snapshot across active transfers.
Initialization/reset requires joined workers as before. Guest mappings must
remain alive until joined unload, and each actual copy rechecks its range.

This is preparation for texture invalidation, not a version cache. Native font,
IME, offline/service hooks and other mutable pointer escapes remain in the
[writer audit](../render/texture-coherence.md). Full-content texture comparisons
remain enabled.

Run the independent regression:

```sh
python3 ios/game/test-file-memory.py \
  --output /Volumes/office/Build/OpenCrown/file-memory-review \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

It exercises the real memory/file implementations natively, with ASan/UBSan
and with TSan, then compiles the sources for both SDKs. Coverage includes
multi-chunk data, the 64 MiB boundary, EOF/tail preservation, positioned I/O,
offset preservation on validation failure, rejected copy callbacks, POSIX
errno, stat outputs, zero/overflow spans, legacy compatibility and reset.
A gate after host I/O but before publication lets the original LOCK block the
checked write. A deliberately unlocked fixture callback must fail that same
functional assertion; this negative control is never linked into a game.
The save-service integration and quiescence fixtures separately run both
callback configurations through transaction, recovery and in-flight-call gates.
