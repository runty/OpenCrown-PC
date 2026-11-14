"""Compare only tools used by a trace, preserving its generation record intact."""

FP80_FIELDS = frozenset({"fp80_lower_sha256", "fp80_manifest_sha256"})


def needs_fp80(record):
    # Unknown old records remain conservative. Native byte-copy shims and
    # explicitly non-FP80 Remill traces never invoke the FP80 lowering tool.
    return not (
        record.get("implementation") == "native-libc-bulk"
        or record.get("fp80_lowered") is False
    )


def backend_matches(record, active):
    generated = record.get("backend")
    if not isinstance(generated, dict) or not generated or not active:
        return False
    if needs_fp80(record):
        return generated == active
    return {k: v for k, v in generated.items() if k not in FP80_FIELDS} == {
        k: v for k, v in active.items() if k not in FP80_FIELDS
    }


def record_current(record, active, ir_sha256, module_hashes):
    return bool(
        record
        and backend_matches(record, active)
        and record.get("ir_sha256") == ir_sha256
        and record.get("module_image_sha256") == module_hashes.get(record.get("module"))
        and record.get("module") in module_hashes
    )
