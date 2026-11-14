"""Signed, fixed-ABI wrappers for three metadata-verified bulk libc exports."""


def native_libc_ir(pc, kind):
    if not 0 <= pc < 2**64 or kind not in (0, 1, 2):
        raise ValueError("Invalid native libc wrapper")
    return f"""; Native bulk libc adapter; no original instruction bytes in this wrapper.
define ptr @sub_{pc:x}(ptr noalias %state, i64 %program_counter, ptr noalias %memory) uwtable {{
entry:
  call void @dcp_trace_enter(ptr %state, i64 %program_counter)
  %result = call ptr @dcp_libc_bulk(ptr %state, i64 {kind}, ptr %memory)
  ret ptr %result
}}
declare void @dcp_trace_enter(ptr, i64)
declare ptr @dcp_libc_bulk(ptr, i64, ptr)
"""
