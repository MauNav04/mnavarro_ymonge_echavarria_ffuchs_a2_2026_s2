"""Fallo real de pthread_create bajo un limite de memoria privado del subproceso."""
import os
import pathlib
import subprocess
import sys
import tempfile

try:
    import resource
except ImportError:
    print("SKIP: RLIMIT_AS no disponible")
    raise SystemExit(77)


def limit_memory():
    resource.setrlimit(resource.RLIMIT_AS, (512 * 1024 * 1024,) * 2)


with tempfile.TemporaryDirectory(prefix="fractal-thread-failure-") as temporary:
    root = pathlib.Path(temporary)
    image, output = root / "input.ppm", root / "output.frc"
    image.write_bytes(b"P6\n16 24\n255\n" + bytes([127] * 1152))
    original = image.read_bytes()
    for binary in sys.argv[1:]:
        output.write_bytes(b"preserve")
        result = subprocess.run([binary, "compress", str(image), str(output), "--teams", "512", "--threads-per-team", "2"],
                                preexec_fn=limit_memory, env=dict(os.environ, OPENBLAS_NUM_THREADS="1"),
                                capture_output=True, text=True, timeout=15)
        assert result.returncode > 0, result.stdout + result.stderr
        assert "pthread_create" in result.stderr, result.stderr
        assert output.read_bytes() == b"preserve" and image.read_bytes() == original
        print("OK: fallo pthread_create propagado sin bloqueo ni truncamiento:", pathlib.Path(binary).name)
