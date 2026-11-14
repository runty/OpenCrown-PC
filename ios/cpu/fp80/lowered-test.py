#!/usr/bin/env python3
"""Exercise generated LLVM arithmetic through the lowering tool and arm64 codegen."""

import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--build", type=Path, required=True)
args = p.parse_args()
r = args.build.resolve()
s = Path(__file__).resolve().parent
if (
    len(r.parts) < 3
    or r.parts[1] != "Volumes"
    or not Path("/Volumes", r.parts[2]).is_mount()
):
    p.error("Build output must be on a mounted external disk")
ir = "declare x86_fp80 @exp2l(x86_fp80)\n"
for name in ["trunc", "sqrt", "floor", "ceil", "roundeven", "fabs"]:
    ir += f"declare x86_fp80 @llvm.{name}.f80(x86_fp80)\n"
ops = list(range(13)) + list(range(14, 36))
for op in ops:
    body = ""
    out_type = "x86_fp80"
    value = "%result"
    if op in [6, 7]:
        body = f"%a = load i64, ptr %ap, align 1\n%result = {'sitofp' if op == 6 else 'uitofp'} i64 %a to x86_fp80\n"
    elif op in [8, 9]:
        small = "float" if op == 8 else "double"
        body = f"%a = load {small}, ptr %ap, align 1\n%result = fpext {small} %a to x86_fp80\n"
    else:
        body = "%a = load x86_fp80, ptr %ap, align 1\n%b = load x86_fp80, ptr %bp, align 1\n"
        if op < 4:
            body += (
                f"%result = {['fadd', 'fsub', 'fmul', 'fdiv'][op]} x86_fp80 %a, %b\n"
            )
        elif op == 4:
            body += "%trunc = call x86_fp80 @llvm.trunc.f80(x86_fp80 %b)\n%power = call x86_fp80 @exp2l(x86_fp80 %trunc)\n%result = fmul x86_fp80 %a, %power\n"
        elif op in [5, 14, 15, 16, 17, 19]:
            intrinsic = {
                5: "sqrt",
                14: "trunc",
                15: "floor",
                16: "ceil",
                17: "roundeven",
                19: "fabs",
            }[op]
            body += f"%result = call x86_fp80 @llvm.{intrinsic}.f80(x86_fp80 %a)\n"
        elif op in [10, 11]:
            out_type = "float" if op == 10 else "double"
            body += f"%result = fptrunc x86_fp80 %a to {out_type}\n"
        elif op == 12:
            out_type = "i64"
            body += "%result = fptosi x86_fp80 %a to i64\n"
        elif op == 18:
            body += "%result = fneg x86_fp80 %a\n"
        else:
            pred = [
                "false",
                "oeq",
                "ogt",
                "oge",
                "olt",
                "ole",
                "one",
                "ord",
                "uno",
                "ueq",
                "ugt",
                "uge",
                "ult",
                "ule",
                "une",
                "true",
            ][op - 20]
            out_type = "i8"
            body += (
                f"%cmp = fcmp {pred} x86_fp80 %a, %b\n%result = zext i1 %cmp to i8\n"
            )
    ir += f"define void @lower_{op}(ptr %out, ptr %ap, ptr %bp) {{\n{body}store {out_type} {value}, ptr %out, align 1\nret void\n}}\n"
ir += "define void @copy_select(ptr %out, ptr %ap, ptr %bp, i1 %c) { %a=load x86_fp80, ptr %ap\n%b=load x86_fp80, ptr %bp\n%v=select i1 %c,x86_fp80 %a,x86_fp80 %b\nstore x86_fp80 %v,ptr %out\nret void }\n"
ir += """define void @copy_phi(ptr %out,ptr %ap,ptr %bp,i1 %c,i32 %n){
entry:
%a=load x86_fp80,ptr %ap,align 1
%b=load x86_fp80,ptr %bp,align 1
br label %loop
loop:
%left=phi i32 [%n,%entry],[%next,%body]
%value=phi x86_fp80 [%a,%entry],[%alternate,%body]
%empty=icmp eq i32 %left,0
br i1 %empty,label %done,label %body
body:
%alternate=select i1 %c,x86_fp80 %b,x86_fp80 %value
%next=sub i32 %left,1
br label %loop
done:
%f=freeze x86_fp80 %value
store x86_fp80 %f,ptr %out,align 1
ret void
}
"""
(r / "lower-fixtures.ll").write_text(ir)


def run(command, log):
    with (r / log).open("w") as f:
        subprocess.run(
            [str(x) for x in command], stdout=f, stderr=subprocess.STDOUT, check=True
        )


run(
    [r / "fp80-lower", r / "lower-fixtures.ll", r / "lower-fixtures-output.ll"],
    "lower-fixtures.log",
)
for platform, target, sdk in [
    ("mac", "arm64-apple-macos15", "macosx"),
    ("ios", "arm64-apple-ios18", "iphoneos"),
    ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator"),
]:
    sdk_path = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    run(
        [
            "/opt/homebrew/opt/llvm/bin/clang",
            "-target",
            target,
            "-isysroot",
            sdk_path,
            "-O2",
            "-c",
            r / "lower-fixtures-output.ll",
            "-o",
            r / platform / "lower-fixtures.o",
        ],
        "lower-fixtures-" + platform + ".log",
    )
cpp = "\n".join(
    f'extern "C" void lower_{op}(void*,const void*,const void*);' for op in ops
)
cpp += (
    '\nextern "C" void dcp_lower_fixture(void *out,const void *a,const void *b,unsigned op){switch(op){'
    + "".join(f"case {op}:lower_{op}(out,a,b);return;" for op in ops)
    + "default:__builtin_trap();}}\n"
)
cpp += """
#include <cstring>
#include <cstdio>
extern "C" void copy_select(void*,const void*,const void*,bool);
extern "C" void copy_phi(void*,const void*,const void*,bool,unsigned);
__attribute__((constructor)) static void storage_checks(){
    unsigned char a[16]={1,2,3,4,5,6,7,128,0xfe,0x7f},b[16]={9,8,7,6,5,4,3,192,0xff,0x3f},out[16]{};
    for(unsigned c=0;c<2;++c){
        copy_select(out,a,b,c);if(std::memcmp(out,c?a:b,10))__builtin_trap();
        for(unsigned n=0;n<4;++n){copy_phi(out,a,b,c,n);if(std::memcmp(out,(c&&n)?b:a,10))__builtin_trap();}
    }
    std::fprintf(stderr,"10 exact i80 select/loop-PHI/freeze storage checks passed\\n");
}
"""
(r / "lower-fixtures.cpp").write_text(cpp)
run(
    [
        "xcrun",
        "clang++",
        "-std=c++17",
        "-O2",
        "-DLOWERED_FIXTURES",
        s / "oracle.cpp",
        r / "lower-fixtures.cpp",
        r / "mac/lower-fixtures.o",
        r / "mac/libdcp-fp80.a",
        "-o",
        r / "lower-checker",
    ],
    "lower-checker-build.log",
)
run([r / "lower-checker", r / "oracle-results.txt"], "lower-checker-results.txt")
print((r / "lower-checker-results.txt").read_text())
# Unknown transcendental calls must fail before arm64 codegen.
(r / "unknown.ll").write_text(
    "declare x86_fp80 @sinl(x86_fp80)\ndefine void @unknown(ptr %o,ptr %p){%x=load x86_fp80,ptr %p\n%y=call x86_fp80 @sinl(x86_fp80 %x)\nstore x86_fp80 %y,ptr %o\nret void}\n"
)
result = subprocess.run(
    [str(r / "fp80-lower"), str(r / "unknown.ll"), str(r / "unknown-output.ll")],
    capture_output=True,
    text=True,
)
assert result.returncode and "Unsupported FP80" in result.stderr
print("Unknown transcendental rejected explicitly")
