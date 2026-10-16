"""Tiempo nativo y contadores de software contrastados con la traza existente."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, *args):
    return subprocess.run([binary, *map(str, args)], capture_output=True, text=True, timeout=30)

with tempfile.TemporaryDirectory(prefix="fractal-bench-report-") as temp:
    root = Path(temp)
    image = root / "image.ppm"
    image.write_bytes(b"P6\n16 24\n255\n" + bytes((i * 17 + i // 7) % 256 for i in range(1152)))
    reference = root / "reference.frc"
    assert run(sys.argv[1], "compress", image, reference, 4, 3).returncode == 0
    original = image.read_bytes()
    for binary, model in zip(sys.argv[1:], ("sequential", "fine-grained", "coarse-grained")):
        for command, channels in (("compress", 3), ("compress-gray", 1)):
            metrics, encoded, trace = root / "metrics.csv", root / "output.frc", root / "trace.csv"
            args = [command, image, encoded, 4, 3, "--report-metrics", metrics]
            if model != "sequential":
                args += ["--teams", 2, "--threads-per-team", 2, "--trace-schedule", trace]
            result = run(binary, *args)
            assert result.returncode == 0, result.stderr
            with metrics.open() as stream:
                row = list(csv.DictReader(stream))[0]
            assert row["model"] == model and int(row["channels"]) == channels and float(row["time_ms"]) > 0
            if command == "compress":
                assert encoded.read_bytes() == reference.read_bytes()
            if model != "sequential":
                with trace.open() as stream:
                    events = list(csv.DictReader(stream))
                assert int(row["turns"]) == len(events)
                switches = sum(a["channel"] == b["channel"] and a["worker"] != b["worker"] for a, b in zip(events, events[1:]))
                assert int(row["scheduler_switches"]) == switches
                assert int(row["stalls"]) == sum(e.get("reason") == "stall" for e in events)
            else:
                assert int(row["turns"]) == int(row["stalls"]) == int(row["scheduler_switches"]) == 0
        # Proteger entrada, salida y diagnóstico ante rutas iguales o modo inválido.
        before = encoded.read_bytes()
        assert run(binary, "compress", image, encoded, "--report-metrics", image).returncode != 0
        assert run(binary, "compress", image, encoded, "--report-metrics", encoded).returncode != 0
        assert run(binary, "compare", image, image, "--report-metrics", metrics).returncode != 0
        assert image.read_bytes() == original and encoded.read_bytes() == before
print("OK CSV nativo: seis casos RGB/gris, tiempos positivos, contadores iguales a trazas y archivos protegidos.")
