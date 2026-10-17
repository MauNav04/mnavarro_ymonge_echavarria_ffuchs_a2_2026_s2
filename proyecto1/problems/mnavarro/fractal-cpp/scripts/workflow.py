"""Flujo automático: preparar, comprobar, medir y publicar un informe local completo."""
import argparse
from datetime import datetime, timezone
import importlib.util
import importlib
import json
from pathlib import Path
import subprocess
import sys
import uuid

from archive_metrics import archive
from benchmark import snapshot
from test_models import ROOT, require

ROLES = {"benchmark": "raw.csv", "scaling": "raw.csv", "perf": "events.csv"}
WORKLOAD = ("measurement_kind", "machine_sha256", "input_sha256", "range", "stride", "gray",
            "quantum", "kernel", "os_release", "binaries_sha256")


def identifier():
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:6]


def save(path, data):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def execute(argv):
    print("+", " ".join(map(str, argv)), flush=True)
    subprocess.run(list(map(str, argv)), check=True)


def setup():
    python = ROOT / ".venv/bin/python"
    if not python.is_file():
        execute([sys.executable, "-m", "venv", ROOT / ".venv"])
    execute([python, "-m", "pip", "install", "-r", ROOT / "scripts/requirements.txt"])
    print("Preparación completa. Siguiente comando: make quick", flush=True)


def dependencies():
    require(all(importlib.util.find_spec(name) is not None for name in ("scipy", "matplotlib")),
            "Faltan dependencias científicas. Ejecutar make setup y volver a intentar.")
    try:
        for name in ("scipy", "matplotlib"):
            importlib.import_module(name)
    except ImportError as error:
        raise ValueError("El entorno científico no carga correctamente; ejecutar make setup: " + str(error)) from error


def hardware_state():
    state = snapshot()
    require(state["active"] in ("0", "1"), "El kernel no informa un estado SMT verificable")
    return "on" if state["active"] == "1" else "off"


def fingerprint(meta):
    return json.dumps({**{key: meta.get(key) for key in WORKLOAD},
                       "state_verified": bool(meta.get("state_verified", False))}, sort_keys=True)


def select_campaigns(directory, anchor=None):
    """Elegir las campañas completas más recientes de una misma carga, por estado/tipo."""
    families = {}
    for path in sorted(Path(directory).rglob("metadata.json")):
        try:
            meta = json.loads(path.read_text())
            role, state = meta.get("experiment"), meta.get("smt_state")
            if meta.get("schema") != 1 or not meta.get("success") or meta.get("check_only") or role not in ROLES or state not in ("on", "off"):
                continue
            if meta.get("measurement_kind") not in ("physical", "diagnostic", "synthetic"):
                continue
            csv_path = path.with_name(ROLES[role])
            if not csv_path.is_file():
                continue
            key = fingerprint(meta)
            family = families.setdefault(key, {})
            record = dict(csv=csv_path, metadata=path, meta=meta, modified=path.stat().st_mtime_ns)
            previous = family.get((state, role))
            if previous is None or record["modified"] > previous["modified"]:
                family[(state, role)] = record
        except (OSError, ValueError):
            print("Aviso: metadatos ilegibles, se omiten:", path, flush=True)
    eligible = []
    for key, family in families.items():
        states = [state for state in ("on", "off") if all((state, role) in family for role in ROLES)]
        if states and (anchor is None or key == fingerprint(anchor)):
            records = [family[(state, role)] for state in states for role in ROLES]
            formal = records[0]["meta"]["measurement_kind"] == "physical"
            eligible.append((formal, max(record["modified"] for record in records), records))
    require(eligible, "No hay un conjunto completo y compatible de benchmark, escalabilidad y Perf con sus CSV. "
            "Conservar también metadata.json; ejecutar make quick o make run para crear el conjunto.")
    return max(eligible, key=lambda item: item[:2])[2]


def report(metrics_dir, results_dir, anchor=None, before_archive=None, campaigns_dir=None):
    dependencies()
    records = select_campaigns(campaigns_dir or metrics_dir, anchor)
    kind = records[0]["meta"]["measurement_kind"]
    name = kind + "-" + identifier()
    output = Path(metrics_dir) / "reports" / name
    output.mkdir(parents=True)
    benchmark = [row["csv"] for row in records if row["meta"]["experiment"] != "perf"]
    perf = [row["csv"] for row in records if row["meta"]["experiment"] == "perf"]
    selection = dict(success=False, measurement_kind=kind,
                     states=sorted({row["meta"]["smt_state"] for row in records}),
                     sources=[str(row["csv"].resolve()) for row in records])
    selection_path = output / "report.json"
    save(selection_path, selection)
    latest = None
    try:
        execute([sys.executable, ROOT / "scripts/plot_metrics.py", "--csv", *benchmark,
                 "--output-dir", output / "benchmark"])
        execute([sys.executable, ROOT / "scripts/profile_metrics.py", "--csv", *perf,
                 "--output-dir", output / "perf"])
        selection["figures"] = len(list(output.rglob("*.png")))
        selection["success"] = True
        save(selection_path, selection)
        bundle = Path(results_dir) / "exports" / (name + ".zip")
        latest = dict(report=str(output.resolve()), archive=str(bundle.resolve()), **selection)
        if before_archive is not None:
            before_archive(latest)
        # El ZIP debe incluir la referencia actual y el manifiesto de la campaña terminada.
        save(Path(metrics_dir) / "latest.json", latest)
        print("Exportando todos los resultados a", bundle, flush=True)
        archive(metrics_dir, bundle)
    except (OSError, ValueError, subprocess.CalledProcessError, KeyboardInterrupt) as error:
        selection.update(success=False, error=str(error))
        save(selection_path, selection)
        if latest is not None:
            latest.update(success=False, error=str(error))
            save(Path(metrics_dir) / "latest.json", latest)
        raise
    print(f"\nListo: {selection['figures']} figuras. Tipo: {kind}. SMT: {', '.join(selection['states'])}.")
    print("Gráficas y tablas:", output)
    print("ZIP para compartir:", bundle)
    print("Referencia al resultado más reciente:", Path(metrics_dir) / "latest.json")
    if selection["states"] != ["off", "on"]:
        print("La comparación ON/OFF se añadirá cuando exista el otro estado compatible.")
    return latest


def capture(args, make_report=True, parent_dir=None):
    dependencies()
    require(args.input.is_file(), "No existe la imagen de entrada")
    state = hardware_state()
    quick = args.action == "quick"
    name = ("quick" if quick else "run") + "-" + state + "-" + identifier()
    directory = (parent_dir or args.metrics_dir / "automatic") / name
    directory.mkdir(parents=True)
    manifest = dict(success=False, directory=str(directory.resolve()), action=args.action, smt_state=state, input=str(args.input.resolve()),
                    measurement_kind="diagnostic" if quick else "physical", completed=[])
    path = directory / "run.json"
    save(path, manifest)
    common = ["--build-dir", args.build_dir.resolve(), "--input", args.input.resolve(), "--smt-state", state,
              "--models", "all", "--teams", 4, "--members", 2, "--smt-teams", 4, "--smt-members", 4,
              "--range", 8, "--stride", 8, "--quantum", 1, "--timeout", 600]
    print(f"\nCampaña automática {args.action}: SMT {state.upper()}, entrada {args.input}", flush=True)
    print("Carpeta de la campaña:", directory, flush=True)
    try:
        # Perf/topología/permisos se comprueban antes de invertir tiempo en 200 corridas.
        execute([sys.executable, ROOT / "scripts/profile.py", *common, "--raw-storage", "jsonl",
                 "--check", "--runs", 1, "--output-dir", directory / "check"])
        manifest["completed"].append("preflight")
        save(path, manifest)
        for role, script, runs in (("benchmark", "benchmark.py", 5 if quick else 200),
                                   ("scaling", "scalability.py", 2 if quick else 200),
                                   ("perf", "profile.py", 1 if quick else 20)):
            options = ["--diagnostic"] if quick else []
            if role == "perf":
                options += ["--raw-storage", "jsonl"]
            execute([sys.executable, ROOT / "scripts" / script, *common, *options,
                     "--runs", runs, "--output-dir", directory / role])
            manifest["completed"].append(role)
            save(path, manifest)
        anchor = json.loads((directory / "benchmark/metadata.json").read_text())
        def completed(latest):
            manifest.update(success=True, report=latest["report"], archive=latest["archive"])
            save(path, manifest)
        if make_report:
            report(args.metrics_dir, args.results_dir, anchor, before_archive=completed)
        else:
            manifest["success"] = True
            save(path, manifest)
        print("Campaña terminada correctamente.", flush=True)
    except (OSError, ValueError, subprocess.CalledProcessError, KeyboardInterrupt) as error:
        manifest.update(success=False, error=str(error))
        save(path, manifest)
        raise
    return manifest


def run_both_states(args):
    from perf_runner import native_preflight
    from smt_control import SmtController
    dependencies()
    require(args.input.is_file(), "No existe la imagen de entrada")
    def command(argv):
        result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=30)
        require(result.returncode == 0, result.stderr or "Falló la comprobación de topología")
        return dict(stdout=result.stdout)
    # Nunca pedir privilegios ni cambiar SMT en un destino no validado.
    native_preflight(args.build_dir.resolve(), hardware_state(), command)
    directory = args.metrics_dir / "automatic" / ("run-pair-" + identifier())
    directory.mkdir(parents=True)
    manifest = dict(success=False, states=[], smt_restored=False, campaigns=[])
    path = directory / "run.json"
    save(path, manifest)
    try:
        try:
            with SmtController() as controller:
                for state in ("on", "off"):
                    controller.set_state(state)
                    result = capture(args, make_report=False, parent_dir=directory)
                    manifest["states"].append(state)
                    manifest["campaigns"].append(result["directory"])
                    save(path, manifest)
        finally:
            current = snapshot()
            manifest["smt_restored"] = current["active"] == "1" and current["control"] == "on"
            save(path, manifest)
        def completed(latest):
            manifest.update(success=True, report=latest["report"], archive=latest["archive"])
            save(path, manifest)
        # Sólo esta pareja: no sustituir una captura por datos de una campaña antigua.
        report(args.metrics_dir, args.results_dir, before_archive=completed, campaigns_dir=directory)
        return manifest
    except (OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        manifest.update(success=False, error=str(error))
        save(path, manifest)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("setup", "quick", "run", "report"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--input", type=Path, default=ROOT / "assets/color128.png")
    parser.add_argument("--metrics-dir", type=Path, default=ROOT / "metrics")
    parser.add_argument("--results-dir", type=Path, default=ROOT / "results")
    args = parser.parse_args()
    try:
        if args.action == "setup":
            setup()
        elif args.action == "report":
            report(args.metrics_dir, args.results_dir)
        elif args.action == "run":
            run_both_states(args)
        else:
            capture(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Error: {error}\n")
    except KeyboardInterrupt:
        parser.exit(130, "Ejecución cancelada; los datos parciales se conservan.\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
