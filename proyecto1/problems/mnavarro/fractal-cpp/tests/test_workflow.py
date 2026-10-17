"""Contratos de automatización; las campañas de prueba se identifican como sintéticas."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import workflow
import smt_control


def metadata(folder, role, state="on", kind="diagnostic", image="image"):
    folder.mkdir(parents=True)
    meta = dict(schema=1, campaign_id=folder.name, experiment=role, success=True,
                smt_state=state, measurement_kind=kind, machine_sha256="machine", input_sha256=image,
                range=8, stride=8, gray=False, quantum=1, binaries_sha256={}, state_verified=True)
    (folder / "metadata.json").write_text(json.dumps(meta))
    (folder / workflow.ROLES[role]).write_text("fixture\n")
    return meta


class WorkflowTests(unittest.TestCase):
    def test_smt_pair_order_and_restore_on_errors(self):
        for failure in (None, "on", "off", "interrupt", "restore"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                image = root / "image.png"
                image.write_bytes(b"fixture")
                args = argparse.Namespace(action="run", input=image, build_dir=root,
                                          metrics_dir=root / "metrics", results_dir=root / "results")
                events = []
                state = {"active": "1", "control": "on"}
                class Controller:
                    def __enter__(self):
                        return self
                    def set_state(self, value):
                        events.append(value)
                        state.update(active="1" if value == "on" else "0", control=value)
                    def __exit__(self, *ignored):
                        events.append("restore")
                        if failure == "restore":
                            raise ValueError("restauración fallida")
                        state.update(active="1", control="on")
                def capture(options, make_report, parent_dir):
                    self.assertFalse(make_report)
                    current = state["control"]
                    events.append("capture-" + current)
                    if failure == current:
                        raise ValueError("captura fallida")
                    if failure == "interrupt":
                        raise KeyboardInterrupt()
                    folder = parent_dir / current
                    folder.mkdir()
                    return {"directory": str(folder)}
                def report(*ignored, before_archive, campaigns_dir):
                    self.assertEqual(state["control"], "on")
                    self.assertEqual({p.name for p in campaigns_dir.iterdir() if p.is_dir()}, {"on", "off"})
                    events.append("report")
                    before_archive(dict(report="report", archive="archive"))
                with patch.object(workflow, "dependencies"), patch("perf_runner.native_preflight"), \
                     patch.object(workflow, "snapshot", side_effect=lambda: state.copy()), \
                     patch.object(workflow, "capture", side_effect=capture), \
                     patch.object(workflow, "report", side_effect=report) as report_mock, \
                     patch.object(smt_control, "SmtController", Controller):
                    if failure is None:
                        self.assertTrue(workflow.run_both_states(args)["success"])
                        self.assertEqual(events, ["on", "capture-on", "off", "capture-off", "restore", "report"])
                    else:
                        with self.assertRaises((ValueError, KeyboardInterrupt)):
                            workflow.run_both_states(args)
                        report_mock.assert_not_called()
                    manifest = json.loads(next(args.metrics_dir.rglob("run.json")).read_text())
                    self.assertEqual(manifest["success"], failure is None)
                    self.assertEqual(manifest["smt_restored"], failure != "restore")

    def test_privileged_helper_protocol_eof_and_lock_using_fake_sysfs(self):
        # Ejecutar el ayudante real contra archivos temporales, nunca contra /sys.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            control = root / "control"
            control.write_text("on\n")
            active = root / "active"
            cat = root / "cat"
            cat.write_text('#!/bin/sh\nif test "$( /bin/cat "' + str(control) + '" )" = on; then echo 1; else echo 0; fi\n')
            cat.chmod(0o755)
            helper = smt_control.HELPER.replace("/sys/devices/system/cpu/smt/control", str(control)).replace(
                "/sys/devices/system/cpu/smt/active", str(active)).replace("/run/fractal-smt.lock", str(root / "lock")).replace("/bin/cat", str(cat))
            self.assertNotIn("/sys/", helper)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"])
            def current():
                value = control.read_text().strip()
                return dict(control=value, active="1" if value == "on" else "0")
            with patch.object(smt_control, "HELPER", helper), patch.object(smt_control.os, "geteuid", return_value=0), \
                 patch.object(smt_control, "snapshot", side_effect=current), patch.dict(os.environ, env), redirect_stdout(io.StringIO()):
                with smt_control.SmtController() as controller:
                    controller.set_state("off")
                    self.assertEqual(current()["active"], "0")
                    other = subprocess.run(["/bin/sh", "-c", helper], input="on\n", text=True,
                                           capture_output=True, timeout=5, env=env)
                    self.assertNotEqual(other.returncode, 0)
                    self.assertEqual(current()["control"], "off")  # No toca el dueño del lock.
                self.assertEqual(current()["control"], "on")
                with self.assertRaises(KeyboardInterrupt):
                    with smt_control.SmtController() as controller:
                        controller.set_state("off")
                        raise KeyboardInterrupt()
                self.assertEqual(current()["control"], "on")
                old_handler = signal.getsignal(signal.SIGTERM)
                with self.assertRaises(KeyboardInterrupt):
                    with smt_control.SmtController() as controller:
                        controller.set_state("off")
                        os.kill(os.getpid(), signal.SIGTERM)
                self.assertEqual(current()["control"], "on")
                self.assertEqual(signal.getsignal(signal.SIGTERM), old_handler)
            # EOF sin solicitud stop equivale a pérdida del padre: restauración automática.
            for request in ("off\n", "off\ninvalid\n"):
                child = subprocess.run(["/bin/sh", "-c", helper], input=request, text=True,
                                       capture_output=True, timeout=5, env=env)
                self.assertIn("off", child.stdout)
                self.assertEqual(current()["control"], "on")
                self.assertEqual(child.returncode, 0 if request == "off\n" else 2)
            child = subprocess.Popen(["/bin/sh", "-c", helper], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True, env=env)
            self.assertEqual(child.stdout.readline().strip(), "ready")
            child.stdin.write("off\n")
            child.stdin.flush()
            self.assertEqual(child.stdout.readline().strip(), "off")
            child.terminate()
            child.communicate(timeout=5)
            self.assertEqual(current()["control"], "on")
            self.assertEqual(child.returncode, 143)

    def test_discovery_requires_complete_compatible_families(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for role in workflow.ROLES:
                metadata(root / ("on-" + role), role)
            metadata(root / "off-benchmark", "benchmark", "off")
            rows = workflow.select_campaigns(root)
            self.assertEqual(len(rows), 3)
            self.assertEqual({row["meta"]["smt_state"] for row in rows}, {"on"})
            metadata(root / "off-scaling", "scaling", "off")
            metadata(root / "off-perf", "perf", "off", image="different-image")
            self.assertEqual(len(workflow.select_campaigns(root)), 3)
            path = root / "off-perf/metadata.json"
            meta = json.loads(path.read_text())
            meta["input_sha256"] = "image"
            path.write_text(json.dumps(meta))
            self.assertEqual(len(workflow.select_campaigns(root)), 6)
            # Un sondeo y un archivo sin CSV no reemplazan mediciones completas.
            check = metadata(root / "check", "perf")
            check["check_only"] = True
            (root / "check/metadata.json").write_text(json.dumps(check))
            (root / "on-benchmark/raw.csv").unlink()
            self.assertEqual({row["meta"]["smt_state"] for row in workflow.select_campaigns(root)}, {"off"})

    def test_formal_preference_and_diagnostic_anchor(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for role in workflow.ROLES:
                metadata(root / ("formal-" + role), role, kind="physical")
                anchor = metadata(root / ("quick-" + role), role)
            self.assertEqual({row["meta"]["measurement_kind"] for row in workflow.select_campaigns(root)}, {"physical"})
            self.assertEqual({row["meta"]["measurement_kind"] for row in workflow.select_campaigns(root, anchor)}, {"diagnostic"})

    def test_run_presets_and_failure_stop(self):
        for action, expected in [("quick", [5, 2, 1]), ("run", [200, 200, 20])]:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                image = root / "input.png"
                image.write_bytes(b"fixture")
                args = argparse.Namespace(action=action, input=image, build_dir=root / "build",
                                          metrics_dir=root / "metrics", results_dir=root / "results")
                calls = []
                def child(argv):
                    calls.append(list(map(str, argv)))
                    output = Path(argv[argv.index("--output-dir") + 1])
                    output.mkdir()
                    if "benchmark.py" == Path(argv[1]).name:
                        (output / "metadata.json").write_text('{"measurement_kind":"synthetic"}')
                def report(*args, before_archive, **kwargs):
                    data = dict(report="report", archive="archive")
                    before_archive(data)
                    return data
                with patch.object(workflow, "dependencies"), patch.object(workflow, "hardware_state", return_value="off"), \
                     patch.object(workflow, "execute", side_effect=child), patch.object(workflow, "report", side_effect=report), redirect_stdout(io.StringIO()):
                    result = workflow.capture(args)
                self.assertTrue(result["success"])
                self.assertEqual(result["smt_state"], "off")
                self.assertIn("--check", calls[0])
                self.assertEqual([int(cmd[cmd.index("--runs") + 1]) for cmd in calls[1:]], expected)
                self.assertEqual(["--diagnostic" in cmd for cmd in calls[1:]], [action == "quick"] * 3)
                self.assertEqual(calls[-1][calls[-1].index("--raw-storage") + 1], "jsonl")
                self.assertEqual(result["completed"], ["preflight", "benchmark", "scaling", "perf"])
                args.metrics_dir = root / "failed"
                def fail(argv):
                    raise subprocess.CalledProcessError(1, argv)
                with patch.object(workflow, "dependencies"), patch.object(workflow, "hardware_state", return_value="on"), \
                     patch.object(workflow, "execute", side_effect=fail), patch.object(workflow, "report") as report_mock, redirect_stdout(io.StringIO()):
                    with self.assertRaises(subprocess.CalledProcessError):
                        workflow.capture(args)
                    report_mock.assert_not_called()
                manifest = json.loads(next(args.metrics_dir.rglob("run.json")).read_text())
                self.assertFalse(manifest["success"])
                self.assertEqual(manifest["completed"], [])

    def test_no_csv_and_invalid_smt_do_not_fake_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "CSV"):
                workflow.select_campaigns(Path(temporary))
        with patch.object(workflow, "snapshot", return_value={"active": "unknown"}):
            with self.assertRaises(ValueError):
                workflow.hardware_state()

    def test_setup_uses_project_venv_and_requirements(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(workflow, "ROOT", root), patch.object(workflow, "execute") as command, redirect_stdout(io.StringIO()):
                workflow.setup()
            self.assertEqual(command.call_count, 2)
            self.assertIn("venv", command.call_args_list[0].args[0])
            self.assertEqual(command.call_args_list[1].args[0][0], root / ".venv/bin/python")


class ReportTests(unittest.TestCase):
    def test_real_automatic_report_and_zip_from_synthetic_csv(self):
        # Reutilizar generadores existentes de datos sintéticos, nunca simular hardware.
        sys.path.insert(0, str(ROOT / "tests"))
        from test_metrics import fixture as benchmark_fixture
        from test_perf import fixture as perf_fixture
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metrics = root / "metrics"
            metrics.mkdir()
            for state in ("on", "off"):
                fixed = benchmark_fixture(metrics / ("fixed-" + state), "fixed-" + state, state)
                benchmark_fixture(metrics / ("scaling-" + state), "scaling-" + state, state, "scaling")
                perf, meta, rows = perf_fixture(metrics / ("perf-" + state), state)
                reference = json.loads(fixed.with_name("metadata.json").read_text())
                for key in workflow.WORKLOAD:
                    if key in reference:
                        meta[key] = reference[key]
                meta["reference_sha256"] = "test-output"
                perf.with_name("metadata.json").write_text(json.dumps(meta))
            original = {str(p): p.read_bytes() for p in metrics.glob("*/metadata.json")}
            manifest = metrics / "run-fixture.json"
            manifest.write_text('{"success": false, "measurement_kind": "synthetic"}')
            def completed(latest):
                manifest.write_text(json.dumps(dict(success=True, measurement_kind="synthetic", report=latest["report"])))
            with redirect_stdout(io.StringIO()):
                result = workflow.report(metrics, root / "results", before_archive=completed)
            self.assertEqual(result["measurement_kind"], "synthetic")
            self.assertEqual(result["states"], ["off", "on"])
            self.assertEqual(result["figures"], 14)
            self.assertTrue(Path(result["archive"]).is_file())
            self.assertTrue((Path(result["report"]) / "perf/smt_comparison.csv").exists())
            from PIL import Image
            from plot_style import MODEL_COLORS
            # Comprobar los PNG reales, no sólo que se llamó a savefig.
            for png in Path(result["report"]).rglob("*.png"):
                with Image.open(png) as picture:
                    self.assertAlmostEqual(picture.info["dpi"][0], 300, delta=.1)
                    self.assertAlmostEqual(picture.info["dpi"][1], 300, delta=.1)
            with Image.open(Path(result["report"]) / "benchmark/05_dashboard.png") as picture:
                pixels = set(picture.convert("RGB").getdata())
                for color in MODEL_COLORS.values():
                    self.assertIn(tuple(bytes.fromhex(color[1:])), pixels)
            with Image.open(Path(result["report"]) / "perf/10_perf_ipc.png") as picture:
                self.assertIn((100, 116, 139), set(picture.convert("RGB").getdata()))
            with zipfile.ZipFile(result["archive"]) as stream:
                self.assertIn("metrics/fixed-on/raw.csv", stream.namelist())
                self.assertIn("metrics/perf-off/events.csv", stream.namelist())
                self.assertEqual(json.loads(stream.read("metrics/latest.json")), result)
                self.assertTrue(json.loads(stream.read("metrics/run-fixture.json"))["success"])
            for name, content in original.items():
                self.assertEqual(Path(name).read_bytes(), content)


if __name__ == "__main__":
    integration = "--integration" in sys.argv
    if integration and any(importlib.util.find_spec(name) is None for name in ("scipy", "matplotlib")):
        print("SKIP: instalar scripts/requirements.txt")
        raise SystemExit(77)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ReportTests if integration else WorkflowTests)
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
