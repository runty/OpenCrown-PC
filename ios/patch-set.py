#!/usr/bin/env python3
"""Apply an ordered patch set only to pristine or known intermediate files.

Later patches may modify earlier patch context, making independent reverse
checks unreliable. Replay against HEAD in a scratch tree and validate every
destination before changing any cached source. Unknown changes are preserved.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile


def apply(tree, patches, dry_run=False):
    paths = set()
    for patch in patches:
        for name in re.findall(r"^\+\+\+ b/(.+)$", patch.read_text(), re.M):
            path = Path(name)
            if path.is_absolute() or ".." in path.parts:
                raise RuntimeError("Unsafe patch path: " + name)
            paths.add(path)
    if not paths:
        raise RuntimeError("Patch set has no destination files")
    # Keep scratch/cache work on the same mounted build volume.
    with tempfile.TemporaryDirectory(prefix=".patch-review-", dir=tree.parent) as tmp:
        scratch = Path(tmp)
        known = {path: set() for path in paths}
        for path in paths:
            original = subprocess.check_output(
                ["git", "-C", str(tree), "show", "HEAD:" + str(path)]
            )
            target = scratch / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original)
            known[path].add(hashlib.sha256(original).digest())
        for patch in patches:
            subprocess.run(["git", "apply", "--check", str(patch)], cwd=scratch, check=True)
            subprocess.run(["git", "apply", str(patch)], cwd=scratch, check=True)
            for path in paths:
                known[path].add(hashlib.sha256((scratch / path).read_bytes()).digest())
        changed = []
        for path in sorted(paths):
            target = tree / path
            if target.is_symlink() or not target.is_file():
                raise RuntimeError("Expected regular cache source: " + str(target))
            current = target.read_bytes()
            if hashlib.sha256(current).digest() not in known[path]:
                raise RuntimeError("Divergent cache source; left untouched: " + str(target))
            final = (scratch / path).read_bytes()
            if current != final:
                changed.append((target, final))
        if not dry_run:
            for target, final in changed:
                mode = target.stat().st_mode & 0o777
                with tempfile.NamedTemporaryFile(prefix=".patched-", dir=target.parent, delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(final)
                os.chmod(temporary, mode)
                temporary.replace(target)
        print(f"Verified {len(patches)} ordered patches across {len(paths)} files; "
              f"{len(changed)} {'pending' if dry_run else 'updated'}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", type=Path)
    parser.add_argument("patch", type=Path, nargs="+")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    apply(args.tree.resolve(), [p.resolve() for p in args.patch], args.check)
