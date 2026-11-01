"""Compatibility launcher: production tools are implemented in C."""
import os
from pathlib import Path
import subprocess
import sys

def main(argv=None):
    binary = Path(__file__).resolve().parents[3] / 'build' / 'upsilon'
    if not binary.is_file():
        print('Build the C toolchain first: make -C ' + str(binary.parent.parent), file=sys.stderr)
        return 1
    if argv is None:
        os.execv(str(binary), [str(binary), *sys.argv[1:]])
    return subprocess.call([str(binary), *argv])
