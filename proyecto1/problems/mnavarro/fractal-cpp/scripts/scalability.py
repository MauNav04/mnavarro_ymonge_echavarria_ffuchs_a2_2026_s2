"""Alias del barrido fuerte, con datos individuales y el mismo benchmark runner."""
import sys
from benchmark import main
if __name__ == "__main__":
    if "--strong-scaling" not in sys.argv:
        sys.argv.append("--strong-scaling")
    raise SystemExit(main())
