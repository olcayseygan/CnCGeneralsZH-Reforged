#!/usr/bin/env python3
"""Reject retail distribution containers in the index; never read their contents."""

from pathlib import Path
import subprocess
import sys

# Source-release resources and project-created media require separate provenance review.
# These containers have no place in an engine-only checkout, even with git add -f.
RETAIL_SUFFIXES = {".big", ".bik", ".iso", ".cab", ".mdf", ".mds", ".mp3", ".wav", ".csf"}


def forbidden(paths):
    return [p for p in paths if Path(p).suffix.lower() in RETAIL_SUFFIXES]


def main():
    root = Path(__file__).resolve().parents[3]
    result = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True)
    if result.returncode:
        print("skip: a Git checkout is needed to check indexed files")
        return 77
    paths = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
    bad = forbidden(paths)
    if bad:
        print("Retail archive/media containers must stay outside the repository:")
        for path in bad:
            print("  " + path)
        return 1
    print("ok: no retail archive/media containers in the Git index")
    return 0


if __name__ == "__main__":
    sys.exit(main())
