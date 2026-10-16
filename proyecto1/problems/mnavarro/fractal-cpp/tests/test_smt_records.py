"""Contratos del registro ON/OFF con artefactos sinteticos, sin mediciones fisicas."""
import contextlib
import copy
import csv
import importlib.util
import io
import json
import pathlib
import tempfile
import types
import unittest

source = pathlib.Path(__file__).resolve().parents[1] / "SMT" / "verify_smt.py"
spec = importlib.util.spec_from_file_location("verify_smt", source)
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

class Records(unittest.TestCase):
    def test_comparison_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            for state in ("on", "off"):
                directory = root / state
                directory.mkdir()
                for name in ("smt.frc", "sequential.frc", "smt.png", "sequential.png"):
                    (directory / name).write_bytes(b"fixture " + name.split(".")[-1].encode())
                (directory / "topology.txt").write_text("synthetic fixture")
                (directory / "commands.json").write_text("[]")
                with (directory / "affinity.csv").open("w", newline="") as stream:
                    writer = csv.writer(stream)
                    writer.writerow("profile,channel,worker,team,cpu,socket,core,observed_before,observed_after,smt_state,smt_control,smt_active,online_cpus".split(","))
                    for i in range(16):
                        cpu = i if state == "on" else i % 8
                        writer.writerow(["target", 0, i, i // 4, cpu, 0, i % 8, cpu, cpu, state, state, int(state == "on"), 16 if state == "on" else 8])
                names = [item.name for item in directory.iterdir()]
                report = {"schema": 1, "state": state, "config": {"teams": 4, "threads_per_team": 4}, "channels": 1,
                          "input_sha256": "fixture-input", "machine_sha256": "fixture-machine", "binaries": {"smt": "fixture-binary"},
                          "artifacts": {name: verify.digest(directory / name) for name in names}}
                (directory / "run.json").write_text(json.dumps(report))
            args = types.SimpleNamespace(on=root / "on", off=root / "off", output=root / "comparison.json")
            with contextlib.redirect_stdout(io.StringIO()):
                verify.compare(args)
            self.assertTrue(json.loads(args.output.read_text())["functional_match"])
            original = json.loads((args.off / "run.json").read_text())
            for key in ("input_sha256", "machine_sha256", "binaries", "config", "state"):
                changed = copy.deepcopy(original)
                changed[key] = "mismatch"
                (args.off / "run.json").write_text(json.dumps(changed))
                with self.assertRaises((ValueError, TypeError)):
                    verify.compare(args)
            (args.off / "run.json").write_text(json.dumps(original))
            (args.off / "smt.frc").write_bytes(b"tampered")
            with self.assertRaises(ValueError):
                verify.load(args.off, "off")

if __name__ == "__main__":
    unittest.main()
