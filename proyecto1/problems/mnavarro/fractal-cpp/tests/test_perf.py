"""Contratos Perf. Los CSV de prueba son sintéticos, nunca resultados del Ryzen."""
import argparse
import copy
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from perf_support import EVENTS, GROUPS, SCOPE, STRATEGY, discover, parse_stat, perf_command, stat_run
from perf_runner import FIELDS, main as profile_main
from test_models import invoke


def line(event, value=100, unit="", runtime=1000000, pct=100):
    return f"{value};{unit};{event};{runtime};{pct};;\n"


class ParserTests(unittest.TestCase):
    def test_zero_unavailable_and_multiplex(self):
        result = parse_stat(line("context-switches", 0) + line("cycles", "<not supported>") + line("instructions", 500, pct=20), ["context-switches", "cycles", "instructions", "branches"])
        self.assertEqual(result["context-switches"]["count"], 0)
        self.assertEqual(result["context-switches"]["status"], "ok")
        self.assertIsNone(result["cycles"]["count"])
        self.assertEqual(result["cycles"]["status"], "not_supported")
        self.assertEqual(result["instructions"]["status"], "low_running")
        self.assertEqual(result["branches"]["status"], "missing")

    def test_units_modifiers_and_invalid(self):
        result = parse_stat(line("task-clock", 1000, "us") + line("duration_time", 2, "ms", "", "") + line("cycles:u", 9), ["task-clock", "duration_time", "cycles"])
        self.assertEqual(result["task-clock"]["count"], 1)
        self.assertEqual(result["duration_time"]["count"], 2000000)
        self.assertEqual(result["cycles"]["raw_event"], "cycles:u")
        for value in ("NaN", "inf", "-1", "garbage"):
            self.assertEqual(parse_stat(line("cycles", value), ["cycles"])["cycles"]["status"], "invalid_value")
        self.assertEqual(parse_stat(line("task-clock", 1, "unknown"), ["task-clock"])["task-clock"]["status"], "unknown_unit")
        self.assertEqual(parse_stat(line("cycles", runtime=0), ["cycles"])["cycles"]["status"], "not_counted")
        with self.assertRaises(ValueError):
            parse_stat(line("cycles") * 2, ["cycles"])

    def test_group_inheritance_and_command_failure(self):
        command = perf_command("perf", ["cycles", "instructions"], ["codec", "a b"], "out.stat")
        self.assertIn("{cycles,instructions}", command)
        self.assertNotIn("--no-inherit", command)
        self.assertNotIn("-a", command)
        self.assertEqual(command[-2:], ["codec", "a b"])
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "out.stat"
            def denied(argv, timeout):
                output.write_text(line("cycles", 10))
                return dict(returncode=1, stderr="Permission denied", timeout=False)
            _, events = stat_run("perf", ["cycles"], ["true"], output, 2, denied, 90)
            self.assertEqual(events["cycles"]["status"], "permission_denied")

    def test_discovery_fallback(self):
        calls = []
        with tempfile.TemporaryDirectory() as directory:
            def fake(argv, timeout):
                events = argv[argv.index("-e") + 1].strip("{}").split(",")
                calls.append(events)
                lines = []
                for event in events:
                    unit = "ms" if event == "task-clock" else "ns" if event == "duration_time" else ""
                    lines.append(line(event, "<not supported>" if event == "LLC-loads" else 100, unit))
                Path(argv[argv.index("-o") + 1]).write_text("".join(lines))
                return dict(returncode=0, stderr="", timeout=False)
            availability = discover("perf", ["true"], Path(directory), 2, fake, 90)
            self.assertEqual(set(availability), set(EVENTS))
            self.assertEqual(availability["LLC-loads"]["status"], "not_supported")
            self.assertIn(["LLC-load-misses"], calls)

    def test_env_timeout_and_stdlib_profile(self):
        result = invoke([sys.executable, "-c", "import os;print(os.environ['LC_ALL'])"], 3, env=dict(os.environ, LC_ALL="C"))
        self.assertEqual(result["stdout"].strip(), "C")
        timed = invoke([sys.executable, "-c", "import time;time.sleep(5)"], .05)
        self.assertTrue(timed["timeout"])
        result = invoke([sys.executable, "-c", "import cProfile; cProfile.Profile().enable()"], 3, env=dict(os.environ, PYTHONPATH=str(ROOT / "scripts")))
        self.assertEqual(result["returncode"], 0, result["stderr"])


    def test_capture_pipeline_with_fake_perf_and_real_codec(self):
        if BUILD is None:
            self.skipTest("Sin --build-dir para contrato integrado")
        from perf_runner import native_preflight
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            image = directory / "input.ppm"
            image.write_bytes(b"P6\n16 16\n255\n" + bytes(range(256)) * 3)
            perf = directory / "fake-perf"
            perf.write_text("#!" + sys.executable + "\n" + FAKE_PERF)
            perf.chmod(0o755)
            argv = ["profile.py", "--build-dir", str(BUILD), "--input", str(image), "--output-dir", str(directory / "capture"),
                    "--perf", str(perf), "--models", "seq,fine", "--diagnostic", "--runs", "1", "--warmups", "0", "--cooldown", "0"]
            state = {"synthetic_test": True}
            # Únicamente el test reemplaza preflight; el CLI de producción no tiene bypass.
            with patch("sys.argv", argv), patch("perf_runner.native_preflight", return_value={"topology": state}), patch("perf_runner.snapshot", return_value=state):
                self.assertEqual(profile_main(), 0)
            folder = directory / "capture"
            metadata = json.loads((folder / "metadata.json").read_text())
            self.assertTrue(metadata["success"])
            self.assertEqual(metadata["measured_invocations"], 12)
            with (folder / "events.csv").open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 2 * len(EVENTS))
            self.assertTrue(all(row["status"] == "ok" for row in rows))
            self.assertEqual(len({row["frc_sha256"] for row in rows}), 1)
            # No conservar un artefacto que parezca validación física.
            metadata.update(measurement_kind="synthetic", state_verified=False)
            (folder / "metadata.json").write_text(json.dumps(metadata))


FAKE_PERF = r"""
import sys, subprocess
from pathlib import Path
if '--version' in sys.argv:
    print('synthetic perf for contract tests')
    raise SystemExit(0)
args = sys.argv[1:]
events = args[args.index('-e') + 1].strip('{}').split(',')
output = Path(args[args.index('-o') + 1])
child = subprocess.run(args[args.index('--') + 1:])
lines = []
for event in events:
    unit = 'ms' if event == 'task-clock' else 'ns' if event == 'duration_time' else ''
    value = 10 if event == 'task-clock' else 10000000 if event == 'duration_time' else 100
    lines.append(f'{value};{unit};{event};1000000;100;;\n')
output.write_text(''.join(lines))
raise SystemExit(child.returncode)
"""


def fixture(directory, state="on", kind="synthetic"):
    directory.mkdir()
    meta = dict(schema=1, experiment="perf", campaign_id=state, success=True, state_verified=False,
                measurement_kind=kind, check_only=False, scope=SCOPE, strategy=STRATEGY,
                runs=2, smt_state=state, configurations=[["sequential", 1, 1]], quantum=1,
                input_sha256="test-input", machine_sha256="test-machine", range=8, stride=8, gray=False,
                binaries_sha256={"sequential": "test-binary"}, reference_sha256="test-output", min_running_pct=90)
    (directory / "metadata.json").write_text(json.dumps(meta))
    rows = []
    values = {event: 100 for event in EVENTS}
    values.update({"cycles": 200, "instructions": 400, "context-switches": 0, "task-clock": 20, "duration_time": 10000000,
                   "cache-misses": 10, "L1-dcache-load-misses": 5, "branch-misses": 3})
    for run in (1, 2):
        for group, events in GROUPS.items():
            for event in events:
                missing = event.startswith("LLC-")
                rows.append(dict(campaign_id=state, measurement_kind=kind, smt_state=state, model_key="sequential", modelo="Secuencial",
                                 teams=1, threads_per_team=1, hilos=1, quantum=0, run_id=run, pass_id=group, event=event, raw_event=event,
                                 count="" if missing else values[event], unit="ms" if event == "task-clock" else "ns" if event == "duration_time" else "",
                                 runtime_ns=1000000, running_pct=100, status="not_supported" if missing else "ok", native_time_ms=1,
                                 wall_s=.1, frc_sha256="test-output", expected_runs=2, timestamp="fixture"))
    path = directory / "events.csv"
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    return path, meta, rows


class MetricsTests(unittest.TestCase):
    def test_statistics_and_all_figures(self):
        from profile_metrics import analyze
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            on, _, _ = fixture(directory / "on")
            off, _, _ = fixture(directory / "off", "off")
            rows = analyze([on, off], directory / "figures")
            self.assertEqual(rows[0]["ipc"], 2)
            self.assertEqual(rows[0]["effective_cpus"], 2)
            self.assertEqual(rows[0]["l1_miss_rate_pct"], 5)
            self.assertEqual(rows[0]["cache_miss_rate_pct"], 10)
            self.assertEqual(rows[0]["context_switches"], 0)
            self.assertEqual(rows[0]["context_switches_ci95_halfwidth"], 0)
            self.assertIsNone(rows[0]["llc_miss_rate_pct"])
            self.assertEqual(rows[0]["llc_miss_rate_pct_n"], 0)
            self.assertEqual(len(list((directory / "figures").glob("*.png"))), 5)
            report = json.loads((directory / "figures/analysis.json").read_text())
            self.assertTrue(report["execution_complete"])
            self.assertFalse(report["all_events_measured"])
            self.assertTrue((directory / "figures/smt_comparison.csv").exists())

    def test_reject_corruption_and_incomplete(self):
        from profile_metrics import load
        with tempfile.TemporaryDirectory() as temporary:
            path, meta, rows = fixture(Path(temporary) / "fixture")
            def write(records):
                with path.open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, FIELDS)
                    writer.writeheader()
                    writer.writerows(records)
            write(rows + [rows[0]])
            with self.assertRaisesRegex(ValueError, "duplicados"):
                load([path])
            write(rows[:-1])
            with self.assertRaisesRegex(ValueError, "Faltan"):
                load([path])
            groups, _ = load([path], True)
            self.assertFalse(next(iter(groups.values()))["complete"])
            write(rows)
            bad = copy.deepcopy(rows)
            bad[0]["count"] = "nan"
            write(bad)
            with self.assertRaises(ValueError):
                load([path])
            write(rows)
            other, other_meta, _ = fixture(Path(temporary) / "other", "off")
            other_meta["input_sha256"] = "different"
            other.with_name("metadata.json").write_text(json.dumps(other_meta))
            with self.assertRaisesRegex(ValueError, "diferentes"):
                load([path, other])
            meta.update(measurement_kind="physical", state_verified=True)
            path.with_name("metadata.json").write_text(json.dumps(meta))
            with self.assertRaisesRegex(ValueError, "corridas"):
                load([path])

    def test_single_sample_and_invalid_ratio(self):
        from profile_metrics import describe, observations
        self.assertIsNone(describe([0])["ci95_halfwidth"])
        values = observations({1: {"cycles": dict(count=0, status="ok", pass_id="cpu"), "instructions": dict(count=100, status="ok", pass_id="cpu")}})
        self.assertEqual(values["ipc"], [])


def hardware(build):
    from perf_runner import native_preflight
    try:
        from benchmark import snapshot
        state = "on" if snapshot()["active"] == "1" else "off"
        def command(argv):
            result = invoke(argv, 10)
            if result["returncode"] or result["timeout"]:
                raise ValueError(result["stderr"])
            return result
        native_preflight(build, state, command)
        if shutil.which("perf") is None:
            raise ValueError("perf ausente")
        probe = invoke(["perf", "stat", "-e", "instructions", "--", "true"], 10)
        if probe["returncode"] or probe["timeout"]:
            raise ValueError(probe["stderr"])
    except (ValueError, OSError, KeyError) as error:
        print("SKIP Perf físico:", error)
        return 77
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        image = root / "input.ppm"
        image.write_bytes(b"P6\n16 16\n255\n" + bytes(range(256)) * 3)
        result = invoke([sys.executable, ROOT / "scripts/profile.py", "--build-dir", build, "--input", image,
                         "--output-dir", root / "capture", "--models", "seq", "--smt-state", state,
                         "--diagnostic", "--runs", 1, "--warmups", 0, "--cooldown", 0], 90)
        if result["returncode"] or result["timeout"]:
            raise AssertionError(result)
        meta = json.loads((root / "capture/metadata.json").read_text())
        assert meta["success"] and meta["state_verified"] and meta["measurement_kind"] == "diagnostic"
        assert meta["measured_invocations"] > 0
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", action="store_true")
    parser.add_argument("--hardware", action="store_true")
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    BUILD = args.build_dir
    if args.hardware:
        raise SystemExit(hardware(args.build_dir))
    if args.metrics:
        try:
            import scipy, matplotlib
        except ImportError:
            print("SKIP: instalar scripts/requirements.txt")
            raise SystemExit(77)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MetricsTests if args.metrics else ParserTests)
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
