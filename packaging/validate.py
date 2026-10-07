"""Source-tree entry point; the installed CLI implementation is in host."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host.package_validator import main

if __name__ == "__main__":
    raise SystemExit(main())
