"""Prueba el control de fallos del orquestador sin simular ejecucion física SMT."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest

source = pathlib.Path(__file__).resolve().parents[1] / "scripts/test_models.py"
spec = importlib.util.spec_from_file_location("model_runner", source)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class RunnerTests(unittest.TestCase):
    def test_timeout_and_exit(self):
        result = runner.invoke([sys.executable, "-c", "import time; time.sleep(20)"], 0.1)
        self.assertTrue(result["timeout"])
        self.assertLess(result["returncode"], 0)
        result = runner.invoke([sys.executable, "-c", "import sys; sys.exit(3)"], 2)
        self.assertFalse(result["timeout"])
        self.assertEqual(result["returncode"], 3)

    def test_missing_binary_report(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            args = types.SimpleNamespace(results_dir=root / "results", build_dir=root / "missing",
                                         models=["sequential"], smt_state="on", quick=True, timeout=1)
            self.assertEqual(runner.execute(args), 1)
            reports = list((root / "results").glob("*/report.json"))
            self.assertEqual(len(reports), 1)
            report = json.loads(reports[0].read_text())
            self.assertFalse(report["success"])
            self.assertIn("Falta ejecutable", report["error"])

    def test_affinity_inconsistency(self):
        with tempfile.TemporaryDirectory() as directory:
            csv = pathlib.Path(directory) / "bad.csv"
            csv.write_text("profile,channel,worker,team,cpu,socket,core,observed_before,observed_after\ntarget,0,0,0,1,0,0,2,1\n")
            with self.assertRaises(ValueError):
                runner.check_affinity(csv, "CMP", 1, 1, 1, "on")

    def test_smt_affinity_contracts(self):
        import csv
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "fixture.csv"
            for state in ("on", "off"):
                with path.open("w", newline="") as stream:
                    writer = csv.writer(stream)
                    writer.writerow("profile,channel,worker,team,cpu,socket,core,observed_before,observed_after,smt_state,smt_control,smt_active,online_cpus".split(","))
                    for i in range(16):
                        cpu = i if state == "on" else i % 8
                        writer.writerow(["target", 0, i, i // 4, cpu, 0, i % 8, cpu, cpu, state, state, int(state == "on"), 16 if state == "on" else 8])
                runner.check_affinity(path, "SMT", 4, 4, 1, state)
                with self.assertRaises(ValueError):
                    runner.check_affinity(path, "SMT", 4, 4, 1, "off" if state == "on" else "on")

    def test_duplicate_models_rejected(self):
        result = subprocess.run([sys.executable, str(source), "--models", "sequential", "sequential"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("No repetir", result.stderr)

if __name__ == "__main__":
    unittest.main()
