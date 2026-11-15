"""Expose the CPU runtime's pure flag helpers to LLVM during arm64 codegen."""

import re

# Accept only the ABI emitted by the pinned lifter. Unknown declarations keep
# their external call and use the implementation in ios/cpu/runtime.cpp.
_FLAG = re.compile(
    rb"^declare dso_local zeroext i1 @(__remill_flag_computation_"
    rb"(?:carry|zero|sign|overflow))\(i1 noundef zeroext, \.\.\.\) #\d+$",
    re.MULTILINE,
)
_COMPARE = re.compile(
    rb"^declare dso_local zeroext i1 @(__remill_compare_"
    rb"(?:eq|neq|ult|ule|ugt|uge|slt|sle|sgt|sge))\(i1 noundef zeroext\) #\d+$",
    re.MULTILINE,
)
_UNDEFINED = re.compile(
    rb"^declare dso_local zeroext i8 @__remill_undefined_8\(\) #\d+$", re.MULTILINE
)


def inline_flags(ir):
    # available_externally allows local folding without exporting duplicate
    # definitions from thousands of separately compiled translation units.
    ir = _FLAG.sub(
        rb"define available_externally zeroext i1 @\1(i1 noundef zeroext %value, ...) "
        rb"alwaysinline { ret i1 %value }",
        ir,
    )
    ir = _COMPARE.sub(
        rb"define available_externally zeroext i1 @\1(i1 noundef zeroext %value) "
        rb"alwaysinline { ret i1 %value }",
        ir,
    )
    return _UNDEFINED.sub(
        b"define available_externally zeroext i8 @__remill_undefined_8() "
        b"alwaysinline { ret i8 0 }",
        ir,
    )
