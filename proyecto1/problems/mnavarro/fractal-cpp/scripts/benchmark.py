"""Campaña fractal: CSV incremental; ejecución física en Ryzen o diagnóstico explícito."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import time
import uuid

from test_models import BINARIES, ROOT, check_affinity, invoke, require, sha

LABELS = {"sequential": "Secuencial", "CMP": "CMP", "SMT": "SMT",
          "coarse-grained": "Coarse-Grained", "fine-grained": "Fine-Grained (Cooperativo)"}
FIELDS = ["campaign_id", "experiment", "measurement_kind", "run_id", "modelo", "model_key", "modo", "smt_state",
          "hilos", "teams", "threads_per_team", "quantum", "range", "stride", "width", "height", "channels",
          "eventos_contexto", "scheduler_switches", "turns", "stalls", "tiempo_ms", "tiempo_s", "wall_s",
          "frc_bytes", "input_sha256", "frc_sha256", "expected_runs", "status", "error", "timestamp"]


def selected_models(value):
    aliases = {"seq": "sequential", "cmp": "CMP", "smt": "SMT", "fine": "fine-grained", "fine_coop": "fine-grained", "coarse": "coarse-grained"}
    models = list(BINARIES) if value == "all" else [aliases.get(v.strip(), v.strip()) for v in value.split(",")]
    require(models and len(models) == len(set(models)) and all(v in BINARIES for v in models), "Modelos invalidos o repetidos")
    return ["sequential"] + [m for m in models if m != "sequential"]


def configurations(args, models):
    configs, excluded = [], []
    for model in models:
        if model == "sequential":
            layouts = [(1, 1)]
        elif args.strong_scaling:
            layouts = [(max(1, n // args.members), min(n, args.members)) for n in args.threads]
            require(all(t * m == n for (t, m), n in zip(layouts, args.threads)), "Cada cantidad de hilos debe ser divisible por --members (salvo p menor)")
        else:
            layouts = [(args.smt_teams, args.smt_members)] if model == "SMT" else [(args.teams, args.members)]
        for teams, members in layouts:
            workers = teams * members
            if model == "CMP" and workers > 8:
                excluded.append({"model": model, "threads": workers, "reason": "CMP permite como maximo 8 nucleos fisicos"})
                if not args.strong_scaling:
                    raise ValueError(excluded[-1]["reason"])
                continue
            require(workers <= 16, "La campaña admite hasta 16 trabajadores")
            configs.append((model, teams, members))
    return configs, excluded


def snapshot():
    return {"control": Path("/sys/devices/system/cpu/smt/control").read_text().strip(),
            "active": Path("/sys/devices/system/cpu/smt/active").read_text().strip(),
            "online": Path("/sys/devices/system/cpu/online").read_text().strip(),
            "allowed": sorted(os.sched_getaffinity(0))}


def campaign(args):
    models = selected_models(args.models)
    configs, excluded = configurations(args, models)
    require(args.diagnostic or args.runs >= 200, "La campaña formal requiere al menos 200 corridas; usar --diagnostic para probar el pipeline")
    binaries = {m: args.build_dir.resolve() / BINARIES[m] for m in models}
    require(args.input.is_file(), "No existe la imagen de entrada")
    require(all(p.is_file() for p in binaries.values()), "Faltan ejecutables; compilar con make all")
    directory = args.output_dir.resolve()
    require(not directory.exists(), "El directorio de campaña ya existe; elegir uno nuevo")
    directory.mkdir(parents=True)
    metadata = {"schema": 1, "success": False, "campaign_id": uuid.uuid4().hex,
                "measurement_kind": "diagnostic" if args.diagnostic else "physical", "state_verified": False, "runs": args.runs,
                "warmups": args.warmups, "cooldown": args.cooldown, "smt_state": args.smt_state,
                "experiment": "scaling" if args.strong_scaling else "benchmark", "excluded": excluded, "configurations": configs,
                "input_sha256": sha(args.input), "range": args.range, "stride": args.stride, "gray": args.gray,
                "quantum": args.quantum, "machine_sha256": sha("/etc/machine-id"), "kernel": platform.release(),
                "binaries_sha256": {m: sha(p) for m, p in binaries.items()}}
    def save():
        (directory / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    cache = args.build_dir.resolve() / "CMakeCache.txt"
    metadata["build_flags"] = [line for line in cache.read_text().splitlines() if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_COMPILER:", "CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:"))] if cache.exists() else []
    metadata["os_release"] = Path("/etc/os-release").read_text()
    metadata["script_sha256"] = sha(__file__)
    save()
    with (directory / "commands.jsonl").open("w") as logs, (directory / "raw.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        stream.flush()
        def command(binary, *options):
            result = invoke([binary, *options], args.timeout)
            logs.write(json.dumps(result) + "\n")
            logs.flush()
            require(not result["timeout"], "Timeout del subproceso")
            require(result["returncode"] == 0, result["stderr"] or "Subproceso fallido")
            return result["stdout"]
        try:
            # El perfil físico se verifica incluso si solo se mide el baseline.
            hardware = not args.diagnostic or any(m in models for m in ("CMP", "SMT"))
            if not args.diagnostic:
                require(any(line == "CMAKE_BUILD_TYPE:STRING=Release" for line in metadata["build_flags"]), "La campaña formal requiere build Release")
            if hardware:
                for model in ("CMP", "SMT"):
                    binary = args.build_dir.resolve() / BINARIES[model]
                    require(binary.is_file(), "Preflight fisico requiere fractal_cmp y fractal_smt")
                    options = ["--list-cpus"] + (["--smt-state", args.smt_state] if model == "SMT" else [])
                    metadata[model + "_topology"] = command(binary, *options)
                before = snapshot()
                metadata["topology"] = before
                metadata["state_verified"] = True
                governors = {}
                for path in sorted(Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor")):
                    try: governors[path.parent.parent.name] = path.read_text().strip()
                    except OSError: pass
                metadata["governors"] = governors
                boost = Path("/sys/devices/system/cpu/cpufreq/boost")
                metadata["boost"] = boost.read_text().strip() if boost.exists() else "unknown"
            else:
                before = None
                metadata["environment"] = "diagnostic-only; hardware no certificado"
            save()
            mode = "compress-gray" if args.gray else "compress"
            codec = ["--range", args.range, "--stride", args.stride]
            reference = directory / "reference.frc"
            command(binaries["sequential"], mode, args.input.resolve(), reference, *codec)
            reference_hash = sha(reference)
            measured = 0
            campaign_start = time.monotonic()
            for model, teams, members in configs:
                workers = teams * members
                options = codec + ["--teams", teams, "--threads-per-team", members]
                if model == "fine-grained":
                    options += ["--quantum-domains", args.quantum]
                if model == "SMT":
                    options += ["--smt-state", args.smt_state]
                prefix = directory / f"{model}-{teams}x{members}"
                prefix.mkdir()
                output, native, affinity = prefix / "output.frc", prefix / "metrics.csv", prefix / "affinity.csv"
                options += ["--report-metrics", native]
                if model in ("CMP", "SMT"):
                    options += ["--report-affinity", affinity]
                for index in range(-args.warmups + 1, args.runs + 1):
                    warmup = index <= 0
                    row = {"campaign_id": metadata["campaign_id"], "experiment": metadata["experiment"],
                           "measurement_kind": metadata["measurement_kind"], "run_id": index, "model_key": model,
                           "modelo": ("SMT (HW Real)" if args.smt_state == "on" else "SMT (Sobre-suscripción SW)") if model == "SMT" else LABELS[model],
                           "modo": {"sequential": "baseline", "CMP": "dedicated_cores", "SMT": "hardware_smt" if args.smt_state == "on" else "software_oversubscription", "fine-grained": "cooperative_quantum", "coarse-grained": "burst_block"}[model],
                           "smt_state": args.smt_state, "hilos": workers, "teams": teams, "threads_per_team": members,
                           "quantum": args.quantum if model == "fine-grained" else 0, "range": args.range, "stride": args.stride,
                           "input_sha256": metadata["input_sha256"], "expected_runs": args.runs, "status": "failed", "error": "",
                           "timestamp": datetime.now(timezone.utc).isoformat()}
                    try:
                        if before is not None:
                            require(snapshot() == before, "Topologia/estado SMT/mascara cambio durante la campaña")
                        start = time.perf_counter()
                        command(binaries[model], mode, args.input.resolve(), output, *options)
                        row["wall_s"] = time.perf_counter() - start
                        with native.open(newline="") as metric_stream:
                            samples = list(csv.DictReader(metric_stream))
                        require(len(samples) == 1, "Reporte nativo incompleto")
                        native_row = samples[0]
                        value = float(native_row["time_ms"])
                        require(math.isfinite(value) and value > 0, "Tiempo nativo invalido")
                        require(native_row["model"] == model and int(native_row["threads"]) == workers, "Configuracion nativa incorrecta")
                        require(int(native_row["range"]) == args.range and int(native_row["stride"]) == args.stride,
                                "Geometria nativa incorrecta")
                        require(int(native_row["channels"]) == (1 if args.gray else 3), "Canales nativos incorrectos")
                        require(int(native_row["quantum"]) == (args.quantum if model == "fine-grained" else 1), "Quantum nativo incorrecto")
                        dimensions = [int(native_row["width"]), int(native_row["height"]), int(native_row["channels"])]
                        require(min(dimensions) > 0 and dimensions == metadata.get("image_dimensions", dimensions), "Dimensiones inconsistentes")
                        metadata["image_dimensions"] = dimensions
                        require(int(native_row["teams"]) == teams and int(native_row["threads_per_team"]) == members,
                                "Equipo nativo incorrecto")
                        actual_hash = sha(output)
                        require(actual_hash == reference_hash, "FRC diferente a secuencial")
                        if model in ("CMP", "SMT"):
                            check_affinity(affinity, model, teams, members, int(native_row["channels"]), args.smt_state)
                        if before is not None:
                            require(snapshot() == before, "Estado fisico cambio durante una medicion")
                        row.update(tiempo_ms=value, tiempo_s=value / 1000, frc_sha256=actual_hash, status="ok")
                        for key in ("width", "height", "channels", "turns", "scheduler_switches", "stalls", "frc_bytes"):
                            row[key] = int(native_row[key])
                        row["eventos_contexto"] = row["stalls"] if model == "coarse-grained" else row["scheduler_switches"]
                    except (ValueError, OSError, KeyError) as error:
                        row["error"] = str(error)
                        if not warmup:
                            writer.writerow(row)
                            stream.flush()
                        raise
                    if not warmup:
                        writer.writerow(row)
                        stream.flush()
                        measured += 1
                        if index == 1 or index == args.runs or index % 20 == 0:
                            remaining = max(0, args.runs * len(configs) - measured)
                            eta = (time.monotonic() - campaign_start) / measured * remaining
                            print(f"{model} {teams}x{members}: {index}/{args.runs}, {value:.6g} ms; total {measured}/{args.runs * len(configs)}, ETA ~{eta:.0f}s", flush=True)
                    if args.cooldown:
                        time.sleep(args.cooldown)
            require(sha(args.input) == metadata["input_sha256"], "Entrada modificada")
            require(all(sha(binaries[m]) == h for m, h in metadata["binaries_sha256"].items()), "Binarios modificados")
            metadata.update(success=True, measured_rows=measured, reference_sha256=reference_hash)
            save()
        except (ValueError, OSError, KeyError) as error:
            metadata["error"] = str(error)
            save()
            raise
    print("CSV:", directory / "raw.csv")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--input", type=Path, default=ROOT / "sequential/examples/color128.png")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--models", default="all")
    parser.add_argument("--smt-state", choices=("on", "off"), default="on")
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--strong-scaling", action="store_true")
    parser.add_argument("--gray", action="store_true")
    for name, default in (("runs", 200), ("warmups", 1), ("teams", 4), ("members", 2), ("smt-teams", 4), ("smt-members", 4), ("range", 8), ("stride", 8), ("quantum", 1), ("timeout", 600)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.add_argument("--cooldown", type=float, default=0.15)
    parser.add_argument("--threads", default="1,2,4,8,16")
    args = parser.parse_args()
    try:
        require(all(getattr(args, name) > 0 for name in ("runs", "teams", "members", "smt_teams", "smt_members", "range", "stride", "quantum", "timeout")), "Parametros positivos requeridos")
        require(args.warmups >= 0 and math.isfinite(args.cooldown) and args.cooldown >= 0, "Warmups/cooldown invalidos")
        require(2 <= args.range <= 64 and args.stride <= 2 * args.range, "Geometria invalida")
        args.threads = [int(v) for v in args.threads.split(",")]
        require(args.threads and len(set(args.threads)) == len(args.threads) and min(args.threads) > 0, "Barrido invalido")
        return campaign(args)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
