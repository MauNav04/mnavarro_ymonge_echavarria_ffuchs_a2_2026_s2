"""Nombre compatible con los scripts de los proyectos de referencia."""
import sys
if __name__ == "__main__":
    if "--plot-only" in sys.argv:
        sys.argv.remove("--plot-only")
        from profile_metrics import main
    else:
        from perf_runner import main
    raise SystemExit(main())
