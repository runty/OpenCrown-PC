#!/usr/bin/env python3
"""Verify used-tool freshness without retagging old generation provenance."""

import copy
from trace_provenance import backend_matches, needs_fp80, record_current

checks = 0


def require(value):
    global checks
    checks += 1
    assert value, f"Provenance policy check {checks} failed"


active = {
    "lifter_sha256": "lifter",
    "runtime_bc_sha256": "semantics",
    "fp80_lower_sha256": "new-pass",
    "fp80_manifest_sha256": "new-helper",
}
old = dict(active, fp80_lower_sha256="old-pass", fp80_manifest_sha256="old-helper")
record = {
    "backend": old,
    "fp80_lowered": False,
    "ir_sha256": "ir",
    "module_image_sha256": "image",
    "module": "eboot",
}
saved = copy.deepcopy(record)
require(backend_matches(record, active))
require(record_current(record, active, "ir", {"eboot": "image"}))
require(record == saved)  # Original generation tool identities stay unchanged.
require(record["backend"] != active)
require(not needs_fp80(record))
require(not backend_matches(dict(record, fp80_lowered=True), active))
require(
    not backend_matches(
        {k: v for k, v in record.items() if k != "fp80_lowered"}, active
    )
)
require(backend_matches(dict(record, implementation="native-libc-bulk"), active))
require(not record_current(record, active, "corrupt-ir", {"eboot": "image"}))
require(not record_current(record, active, "ir", {"eboot": "different-image"}))
require(not record_current(record, active, "ir", {}))
require(not record_current(None, active, "ir", {"eboot": "image"}))
require(not backend_matches({}, active))
for key in ["lifter_sha256", "runtime_bc_sha256"]:
    changed = dict(old)
    changed[key] = "different"
    require(not backend_matches(dict(record, backend=changed), active))
    missing = dict(old)
    missing.pop(key)
    require(not backend_matches(dict(record, backend=missing), active))
require(backend_matches(dict(record, backend=active, fp80_lowered=True), active))
require(not backend_matches(dict(record, backend=None), active))
require(not backend_matches(record, {}))
print(f'{{"checks":{checks},"failed":0}}')
