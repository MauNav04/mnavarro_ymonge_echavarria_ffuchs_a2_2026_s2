"""Pipeline real de diagnóstico en tres modelos; ninguna medición física simulada."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from benchmark import configurations, selected_models

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="fractal-benchmark-pipeline-") as tmp:
    work = Path(tmp)
    image = work / "image.ppm"
    image.write_bytes(b"P6\n16 24\n255\n" + bytes((i * 17 + i // 7) % 256 for i in range(1152)))
    base = [sys.executable, str(ROOT / "scripts/benchmark.py"), "--build-dir", str(args.build_dir), "--input", str(image),
            "--models", "seq,fine,coarse", "--runs", "2", "--warmups", "1", "--cooldown", "0", "--range", "4", "--stride", "3", "--teams", "2", "--members", "2"]
    output = work / "diagnostic"
    rejected = subprocess.run(base + ["--output-dir", str(output)], capture_output=True, text=True, timeout=10)
    assert rejected.returncode != 0 and not output.exists() and "200" in rejected.stderr
    result = subprocess.run(base + ["--diagnostic", "--output-dir", str(output)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    meta = json.loads((output / "metadata.json").read_text())
    assert meta["success"] and meta["measurement_kind"] == "diagnostic" and meta["measured_rows"] == 6
    with (output / "raw.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 6 and all(row["run_id"] in ("1", "2") and float(row["tiempo_ms"]) > 0 for row in rows)
    assert all(row["frc_sha256"] == meta["reference_sha256"] for row in rows)
    assert any(int(row["scheduler_switches"]) > 0 for row in rows if row["model_key"] == "fine-grained")
    assert len((output / "commands.jsonl").read_text().splitlines()) == 10
    before = (output / "raw.csv").read_bytes()
    repeated = subprocess.run(base + ["--diagnostic", "--output-dir", str(output)], capture_output=True, text=True, timeout=10)
    assert repeated.returncode != 0 and before == (output / "raw.csv").read_bytes()
    physical = subprocess.run([str(args.build_dir / "fractal_cmp"), "--list-cpus"], capture_output=True, text=True, timeout=10)
    if physical.returncode:
        invalid = subprocess.run(base + ["--runs", "200", "--output-dir", str(work / "physical")], capture_output=True, text=True, timeout=10)
        assert invalid.returncode != 0 and not json.loads((work / "physical/metadata.json").read_text())["success"]
    cfg = argparse.Namespace(strong_scaling=True, threads=[1, 2, 4, 8, 16], members=2)
    configs, excluded = configurations(cfg, selected_models("all"))
    assert ("SMT", 8, 2) in configs and ("CMP", 8, 2) not in configs
    assert excluded == [{"model": "CMP", "threads": 16, "reason": "CMP permite como maximo 8 nucleos fisicos"}]
print("OK pipeline diagnóstico: CSV incremental, warmups excluidos, FRC exactos, protección de campañas y escalabilidad limitada por hardware.")
