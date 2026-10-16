"""Perf por proceso e hilos: CSV incremental en Ubuntu/AMD Ryzen, sin cambiar permisos/SMT."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import shutil
import time
import uuid

from benchmark import LABELS, configurations, selected_models, snapshot
from perf_support import EVENTS, GROUPS, SCOPE, STRATEGY, discover, stat_run
from test_models import BINARIES, ROOT, check_affinity, invoke, require, sha

FIELDS = ["campaign_id", "measurement_kind", "smt_state", "model_key", "modelo", "teams", "threads_per_team", "hilos",
          "quantum", "run_id", "pass_id", "event", "raw_event", "count", "unit", "runtime_ns", "running_pct", "status",
          "native_time_ms", "wall_s", "frc_sha256", "expected_runs", "timestamp"]


def native_preflight(build, state, command):
    info = {}
    cache = (build / "CMakeCache.txt").read_text()
    require("CMAKE_BUILD_TYPE:STRING=Release" in cache, "Perf requiere build Release")
    for model in ("CMP", "SMT"):
        binary = build / BINARIES[model]
        options = ["--list-cpus"] + (["--smt-state", state] if model == "SMT" else [])
        info[model + "_topology"] = command([binary, *options])["stdout"]
    info["topology"] = snapshot()
    info["build_flags"] = [line for line in cache.splitlines() if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:"))]
    return info


def campaign(args):
    require(args.diagnostic or args.check or args.runs >= 200, "Perfilado formal requiere 200 repeticiones por modelo y grupo; usar --diagnostic para comprobaciones breves")
    models = selected_models(args.models)
    configs, _ = configurations(args, models)
    build = args.build_dir.resolve()
    binaries = {m: build / BINARIES[m] for m in models}
    require(args.input.is_file() and all(p.is_file() for p in binaries.values()), "Falta imagen o ejecutables; ejecutar make all")
    directory = args.output_dir.resolve()
    require(not directory.exists(), "Directorio Perf ya existe; elegir uno nuevo")
    directory.mkdir(parents=True)
    meta = {"schema": 1, "experiment": "perf", "campaign_id": uuid.uuid4().hex, "success": False, "state_verified": False,
            "measurement_kind": "diagnostic" if args.diagnostic or args.check else "physical", "check_only": args.check,
            "scope": SCOPE, "strategy": STRATEGY, "runs": args.runs, "warmups": args.warmups, "cooldown": args.cooldown,
            "min_running_pct": args.min_running, "smt_state": args.smt_state, "configurations": configs, "events": list(EVENTS),
            "input_sha256": sha(args.input), "machine_sha256": sha("/etc/machine-id"), "kernel": platform.release(),
            "os_release": Path("/etc/os-release").read_text(), "range": args.range, "stride": args.stride, "gray": args.gray,
            "quantum": args.quantum, "binaries_sha256": {m: sha(p) for m, p in binaries.items()}, "script_sha256": sha(__file__), "support_sha256": sha(Path(__file__).with_name("perf_support.py"))}
    def save():
        (directory / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
    save()
    with (directory / "commands.jsonl").open("w") as logs, (directory / "events.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        stream.flush()
        def logged(argv, timeout=args.timeout, check=False):
            record = invoke(argv, timeout, env=dict(os.environ, LC_ALL="C"))
            logs.write(json.dumps(record) + "\n")
            logs.flush()
            if check:
                require(not record["timeout"] and record["returncode"] == 0, record["stderr"] or "Comando fallido")
            return record
        try:
            meta.update(native_preflight(build, args.smt_state, lambda cmd: logged(cmd, check=True)))
            meta["state_verified"] = True
            perf = shutil.which(args.perf)
            require(perf is not None, "No se encontro perf. Instalar Linux tools para el kernel del Ryzen; ver docs/profiling.md")
            meta["perf_version"] = logged([perf, "--version"], check=True)["stdout"].strip()
            meta["perf_event_paranoid"] = Path("/proc/sys/kernel/perf_event_paranoid").read_text().strip()
            before = meta["topology"]
            save()
            mode = "compress-gray" if args.gray else "compress"
            codec = ["--range", args.range, "--stride", args.stride]
            reference = directory / "reference.frc"
            logged([binaries["sequential"], mode, args.input.resolve(), reference, *codec], check=True)
            meta["reference_sha256"] = sha(reference)
            probe = directory / "probes"
            probe.mkdir()
            probe_command = [binaries["sequential"], mode, args.input.resolve(), probe / "output.frc", *codec]
            available = discover(perf, probe_command, probe, args.timeout, logged, args.min_running)
            meta["availability"] = available
            (directory / "availability.json").write_text(json.dumps(available, indent=2) + "\n")
            supported = {event for event, info in available.items() if info["status"] == "ok"}
            require(any(event in supported for event in EVENTS if event not in GROUPS["software"]),
                    "No hay contadores PMU accesibles. Revisar availability.json, permisos y soporte; no se inventan ceros")
            meta["active_groups"] = {name: [e for e in events if e in supported] for name, events in GROUPS.items()}
            meta["all_requested_events_available"] = len(supported) == len(EVENTS)
            save()
            if args.check:
                meta["success"] = True
                save()
                print("Disponibilidad Perf:", directory / "availability.json")
                for event, data in available.items():
                    print(event + ": " + data["status"])
                return 0
            samples, invocations = 0, 0
            for model, teams, members in configs:
                name = ("SMT (HW Real)" if args.smt_state == "on" else "SMT (Sobre-suscripción SW)") if model == "SMT" else LABELS[model]
                folder = directory / f"{model}-{teams}x{members}"
                folder.mkdir()
                output, native, affinity = folder / "output.frc", folder / "metrics.csv", folder / "affinity.csv"
                options = codec + ["--teams", teams, "--threads-per-team", members, "--report-metrics", native]
                if model == "fine-grained":
                    options += ["--quantum-domains", args.quantum]
                if model == "SMT":
                    options += ["--smt-state", args.smt_state]
                if model in ("CMP", "SMT"):
                    options += ["--report-affinity", affinity]
                command = [binaries[model], mode, args.input.resolve(), output, *options]
                for run in range(-args.warmups + 1, args.runs + 1):
                    for group, events in GROUPS.items():
                        active = meta["active_groups"][group]
                        parsed = {event: dict(available[event], count=None, runtime_ns=None, running_pct=None) for event in events}
                        native_ms, wall, digest = "", "", ""
                        if active:
                            require(snapshot() == before, "Topologia/SMT/mascara cambiaron durante perfilado")
                            stat_path = folder / f"{group}-run{run}.stat"
                            start = time.perf_counter()
                            result, counted = stat_run(perf, active, command, stat_path, args.timeout, logged, args.min_running)
                            wall = time.perf_counter() - start
                            require(not result["timeout"] and result["returncode"] == 0, result["stderr"] or "Fallo de Perf o programa hijo")
                            parsed.update(counted)
                            with native.open(newline="") as native_file:
                                records = list(csv.DictReader(native_file))
                            require(len(records) == 1, "Reporte nativo incompleto")
                            data = records[0]
                            native_ms = float(data["time_ms"])
                            require(math.isfinite(native_ms) and native_ms > 0 and data["model"] == model, "Reporte nativo invalido")
                            require(int(data["teams"]) == teams and int(data["threads_per_team"]) == members,
                                    "Configuracion nativa incorrecta")
                            require(int(data["range"]) == args.range and int(data["stride"]) == args.stride, "Geometria diferente")
                            require(int(data["channels"]) == (1 if args.gray else 3), "Canales diferentes")
                            digest = sha(output)
                            require(digest == meta["reference_sha256"], "FRC diferente a secuencial")
                            if model in ("CMP", "SMT"):
                                check_affinity(affinity, model, teams, members, int(data["channels"]), args.smt_state)
                            require(snapshot() == before, "Estado fisico cambio durante una pasada")
                            if run > 0:
                                invocations += 1
                        if run > 0:
                            for event in events:
                                row = {"campaign_id": meta["campaign_id"], "measurement_kind": meta["measurement_kind"], "smt_state": args.smt_state,
                                       "model_key": model, "modelo": name, "teams": teams, "threads_per_team": members,
                                       "hilos": teams * members, "quantum": args.quantum if model == "fine-grained" else 0,
                                       "run_id": run, "pass_id": group, "event": event, **parsed[event], "native_time_ms": native_ms,
                                       "wall_s": wall, "frc_sha256": digest, "expected_runs": args.runs, "timestamp": datetime.now(timezone.utc).isoformat()}
                                writer.writerow(row)
                                samples += 1
                            stream.flush()
                        if active and args.cooldown:
                            time.sleep(args.cooldown)
                    if run > 0 and (run == 1 or run == args.runs or run % 20 == 0):
                        print(f"Perf {model} {teams}x{members}: {run}/{args.runs} repeticiones por grupo", flush=True)
            require(sha(args.input) == meta["input_sha256"] and all(sha(binaries[m]) == h for m, h in meta["binaries_sha256"].items()),
                    "Entrada o binarios cambiaron durante el perfilado")
            meta.update(success=True, event_rows=samples, measured_invocations=invocations)
            save()
        except (ValueError, OSError, KeyError) as error:
            meta["error"] = str(error)
            save()
            raise
    print("Contadores individuales:", directory / "events.csv")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--input", type=Path, default=ROOT / "assets/color128.png")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--models", default="all")
    parser.add_argument("--smt-state", choices=("on", "off"), default="on")
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--gray", action="store_true")
    parser.add_argument("--min-running", type=float, default=90.)
    parser.add_argument("--cooldown", type=float, default=.15)
    for name, default in (("runs", 200), ("warmups", 1), ("teams", 4), ("members", 2), ("smt-teams", 4), ("smt-members", 4),
                          ("range", 8), ("stride", 8), ("quantum", 1), ("timeout", 600)):
        parser.add_argument("--" + name, type=int, default=default)
    args = parser.parse_args()
    args.strong_scaling = False
    try:
        require(all(getattr(args, k) > 0 for k in ("runs", "teams", "members", "smt_teams", "smt_members", "range", "stride", "quantum", "timeout")), "Parametros positivos requeridos")
        require(args.warmups >= 0 and math.isfinite(args.cooldown) and args.cooldown >= 0, "Warmups/cooldown invalidos")
        require(math.isfinite(args.min_running) and 0 < args.min_running <= 100, "Min-running fuera de rango")
        require(2 <= args.range <= 64 and args.stride <= 2 * args.range, "Geometria invalida")
        return campaign(args)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
