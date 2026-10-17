"""Estadísticas y figuras con datos sintéticos identificados; no son benchmarks medidos."""
import csv
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

if importlib.util.find_spec("scipy") is None or importlib.util.find_spec("matplotlib") is None:
    print("SKIP: instalar scripts/requirements.txt para probar estadísticas y gráficas")
    raise SystemExit(77)
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from benchmark import FIELDS
from metrics import describe, ratio_interval, read_campaigns, summarize


def fixture(directory, cid, state="on", experiment="benchmark"):
    directory.mkdir()
    models = [("sequential", 1, 1, [10., 12., 14.]), ("CMP", 4, 2, [5., 6., 7.]),
              ("SMT", 4, 4, [3., 4., 5.]), ("fine-grained", 4, 2, [20., 22., 24.]),
              ("coarse-grained", 4, 2, [8., 9., 10.])]
    if experiment == "scaling":
        models = [(m, 1, 1, values) for m, _, _, values in models] + models[1:]
    meta = {"schema": 1, "campaign_id": cid, "success": True, "measurement_kind": "synthetic", "runs": 3,
            "smt_state": state, "experiment": experiment, "machine_sha256": "fixture-machine", "input_sha256": "fixture-input",
            "range": 4, "stride": 3, "gray": False, "quantum": 1, "binaries_sha256": {m: "fixture-binary" for m, _, _, _ in models},
            "reference_sha256": "fixture-frc", "configurations": [(m, t, n) for m, t, n, _ in models]}
    (directory / "metadata.json").write_text(json.dumps(meta))
    with (directory / "raw.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        for model, teams, members, values in models:
            for index, value in enumerate(values, 1):
                row = dict.fromkeys(FIELDS, "")
                row.update(campaign_id=cid, experiment=experiment, measurement_kind="synthetic", run_id=index, modelo=model,
                           model_key=model, modo="fixture", smt_state=state, hilos=teams * members, teams=teams,
                           threads_per_team=members, quantum=1 if model == "fine-grained" else 0, range=4, stride=3,
                           width=16, height=24, channels=3, eventos_contexto=10, scheduler_switches=10, turns=12,
                           stalls=2, tiempo_ms=value, tiempo_s=value / 1000, wall_s=value / 1000,
                           frc_bytes=100, input_sha256="fixture-input", frc_sha256="fixture-frc", expected_runs=3, status="ok")
                writer.writerow(row)
    return directory / "raw.csv"


class MetricsTests(unittest.TestCase):
    def test_statistics(self):
        data = describe([10, 12, 14])
        self.assertEqual(data["media_ms"], 12)
        self.assertEqual(data["std_dev_ms"], 2)
        self.assertEqual(data["mediana_ms"], 12)
        self.assertAlmostEqual(data["margen_error_ms"], 4.968275423, places=6)
        self.assertAlmostEqual(data["cv_pct"], 100 / 6)
        large = describe(list(range(1, 201)))
        self.assertEqual(large["media_ms"], 100.5)
        self.assertAlmostEqual(large["std_dev_ms"], math.sqrt(200 * 201 / 12))
        self.assertAlmostEqual(large["error_estandar_ms"], large["std_dev_ms"] / math.sqrt(200))
        self.assertEqual(describe([3])["margen_error_ms"], None)
        self.assertEqual(describe([3, 3])["margen_error_ms"], 0)
        for sample in ([0, 1], [float("nan")], [float("inf")]):
            with self.assertRaises(ValueError):
                describe(sample)
        low, high = ratio_interval(data, describe([5, 6, 7]))
        self.assertLess(low, 2)
        self.assertGreater(high, 2)
        self.assertEqual(ratio_interval(describe([10, 10]), describe([5, 5])), (2, 2))

    def test_summary_and_incompatible_campaigns(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            on = fixture(root / "on", "on")
            off = fixture(root / "off", "off", "off")
            groups, meta = read_campaigns([on, off])
            summary, convergence = summarize(groups, meta)
            cmp = next(row for row in summary if row["model_key"] == "CMP")
            self.assertEqual(cmp["speedup"], 2)
            self.assertEqual(cmp["eficiencia"], 25)
            self.assertAlmostEqual(cmp["amdahl_98"], 1 / (.02 + .98 / 8))
            self.assertEqual(len(convergence), 30)
            path = off.parent / "metadata.json"
            original = json.loads(path.read_text())
            for field in ("machine_sha256", "input_sha256", "measurement_kind", "binaries_sha256"):
                bad = dict(original)
                bad[field] = {"CMP": "other"} if field == "binaries_sha256" else "other"
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    read_campaigns([on, off])
            path.write_text(json.dumps(original))
            with self.assertRaises(ValueError):
                read_campaigns([on, on])
            rows = list(csv.DictReader(on.open()))
            with on.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, FIELDS)
                writer.writeheader()
                writer.writerows(rows[:-1])
            with self.assertRaises(ValueError):
                read_campaigns([on])
            groups, meta = read_campaigns([on], allow_partial=True)
            summary, _ = summarize(groups, meta)
            self.assertTrue(any(not row["complete"] for row in summary))
            with on.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, FIELDS)
                writer.writeheader()
                writer.writerows(row for row in rows if row["model_key"] != "coarse-grained")
            groups, meta = read_campaigns([on], allow_partial=True)
            summary, _ = summarize(groups, meta)
            self.assertTrue(all(not row["complete"] for row in summary))

    def test_all_figures_from_csv(self):
        with tempfile.TemporaryDirectory(prefix="fractal-synthetic-plots-") as tmp:
            root = Path(tmp)
            paths = [fixture(root / "on", "on"), fixture(root / "off", "off", "off"),
                     fixture(root / "scaling", "scaling", experiment="scaling")]
            output = root / "figures"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/metrics.py"), "--csv", *map(str, paths),
                                     "--output-dir", str(output)], capture_output=True, text=True, timeout=80)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(len(list(output.glob("*.png"))), 9)
            self.assertTrue(all(path.stat().st_size > 1000 for path in output.glob("*.png")))
            manifest = json.loads((output / "analysis.json").read_text())
            self.assertTrue(manifest["success"])
            self.assertEqual(manifest["measurement_kind"], "synthetic")
            self.assertTrue((output / "scaling.csv").exists())
            self.assertTrue((output / "convergence.csv").exists())


if __name__ == "__main__":
    unittest.main()
