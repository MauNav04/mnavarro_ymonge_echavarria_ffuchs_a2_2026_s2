"""Entrada CLI del perfilado; conserva compatibilidad con el módulo estándar profile."""
if __name__ == "__main__":
    from perf_runner import main
    raise SystemExit(main())
else:
    # cProfile importa 'profile' y scripts/ está antes que la biblioteca estándar.
    import importlib.util
    from pathlib import Path
    import sysconfig
    _spec = importlib.util.spec_from_file_location("_stdlib_profile", Path(sysconfig.get_path("stdlib")) / "profile.py")
    _module = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_module)
    run, runctx, Profile, _Utils = _module.run, _module.runctx, _module.Profile, _module._Utils
