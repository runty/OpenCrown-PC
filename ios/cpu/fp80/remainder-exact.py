from pathlib import Path
import struct, json
import sys

p = Path(sys.argv[1]).resolve()
cache = {}
stats = {
    "rows": 0,
    "finite_exact_rows": 0,
    "candidate_failures": 0,
    "native_failures": [0, 0],
    "native_failed_partial": [0, 0],
    "native_failed_single": [0, 0],
}
examples = []


def exact(op, a, b):
    sa, ea = struct.unpack("<QH", a)
    sb, eb = struct.unpack("<QH", b)
    na = ea >> 15
    ea &= 32767
    eb &= 32767
    if (
        ea == 32767
        or eb == 32767
        or (ea and sa >> 63 == 0)
        or (eb and sb >> 63 == 0)
        or not sb
    ):
        return None
    pa = (ea or 1) - 16446
    pb = (eb or 1) - 16446
    unit = min(pa, pb)
    A = sa << (pa - unit)
    B = sb << (pb - unit)
    q, r = divmod(A, B)
    if not op and (2 * r > B or (2 * r == B and q & 1)):
        r -= B
    neg = na ^ (r < 0)
    r = abs(r)
    if not r:
        return struct.pack("<QH", 0, na << 15)
    bits = r.bit_length()
    e = unit + bits - 1 + 16383
    if e > 0:
        assert bits <= 64, (bits, e)
        sig = r << (64 - bits)
    else:
        sig = r << (unit + 16445)
        e = 0
    return struct.pack("<QH", sig, e | (neg << 15))


rows = p.joinpath("vectors.bin").read_bytes()
native = p.joinpath("native.bin").read_bytes()
candidate = p.joinpath("candidate.bin").read_bytes()
for i in range(len(rows) // 24):
    cw, op, _ = struct.unpack_from("<HBB", rows, 24 * i)
    a = rows[24 * i + 4 : 24 * i + 14]
    b = rows[24 * i + 14 : 24 * i + 24]
    key = (op, a, b)
    if key not in cache:
        cache[key] = exact(*key)
    want = cache[key]
    stats["rows"] += 1
    if want is None:
        continue
    stats["finite_exact_rows"] += 1
    got = native[18 * i : 18 * i + 10]
    count = struct.unpack_from("<I", native, 18 * i + 12)[0]
    if got != want:
        stats["native_failures"][op] += 1
        stats["native_failed_partial" if count > 1 else "native_failed_single"][op] += 1
        if len(examples) < 12:
            examples.append(
                {
                    "row": i + 1,
                    "op": op,
                    "cw": cw,
                    "a": a.hex(),
                    "b": b.hex(),
                    "native": got.hex(),
                    "exact": want.hex(),
                    "iterations": count,
                }
            )
    if candidate[16 * i : 16 * i + 10] != want:
        stats["candidate_failures"] += 1
stats["examples"] = examples
p.joinpath("exact-results.json").write_text(json.dumps(stats, indent=2) + "\n")
print(json.dumps(stats, indent=2))

raise SystemExit(bool(stats["candidate_failures"]))
