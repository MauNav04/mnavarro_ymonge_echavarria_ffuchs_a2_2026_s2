"""Pruebas funcionales integradas de fractales; no hace benchmarks ni cambia SMT."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BINARIES = {"sequential": "fractal_seq", "fine-grained": "fractal_fine",
            "coarse-grained": "fractal_coarse", "CMP": "fractal_cmp", "SMT": "fractal_smt"}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(argv, timeout, env=None):
    """Un timeout cancela el grupo del proceso; ningún trabajador queda ejecutándose."""
    process = subprocess.Popen(list(map(str, argv)), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True, env=env)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
    return {"argv": list(map(str, argv)), "returncode": process.returncode,
            "stdout": stdout, "stderr": stderr, "timeout": timed_out}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_affinity(path, model, teams, members, channels, state):
    with Path(path).open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    workers = teams * members
    require(len(rows) == channels * workers, "Reporte de afinidad incompleto")
    require({int(row["channel"]) for row in rows} == set(range(channels)), "Canales de afinidad incorrectos")
    for channel in range(channels):
        selected = [r for r in rows if int(r["channel"]) == channel]
        require(sorted(int(r["worker"]) for r in selected) == list(range(workers)), "IDs de trabajadores incorrectos")
        cpus, cores = [], []
        for row in selected:
            require(row["profile"] == "target" and row["cpu"] == row["observed_before"] == row["observed_after"], "Afinidad efectiva incorrecta")
            require(int(row["team"]) == int(row["worker"]) // members, "Equipo incorrecto")
            cpus.append(int(row["cpu"]))
            cores.append((int(row["socket"]), int(row["core"])))
            if model == "SMT":
                require(row["smt_state"] == state and int(row["smt_active"]) == int(state == "on"), "Estado SMT incorrecto")
                require(int(row["online_cpus"]) == (16 if state == "on" else 8), "Topologia SMT incorrecta")
                require(row["smt_control"] in (("on",) if state == "on" else ("off", "forceoff", "notsupported")), "Control SMT incompatible")
        if model == "CMP":
            require(len(set(cores)) == workers, "CMP comparte un nucleo entre trabajadores")
        elif state == "on":
            require(len(set(cpus)) == workers, "SMT ON repite una CPU logica")
        else:
            require(len(set(cpus)) == min(workers, 8), "SMT OFF no distribuye los nucleos")
            require(max(cpus.count(cpu) for cpu in cpus) <= 2, "SMT OFF excede dos trabajadores por nucleo")
        if model == "SMT" and workers == 16:
            require(len(set(cores)) == 8, "SMT no utiliza los ocho nucleos")


def layouts(model, quick):
    if model == "sequential":
        return [(1, 1)]
    if quick:
        return [(1, 1), (4, 4) if model == "SMT" else (2, 2)]
    base = [(1, 1), (1, 2), (2, 2), (4, 2)]
    return base + ([(4, 4), (1, 16), (16, 1)] if model == "SMT" else ([] if model == "CMP" else [(16, 1)]))


def execute(args):
    directory = args.results_dir.resolve() / (time.strftime("run-%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True)
    report = {"schema": 1, "models": args.models, "smt_state": args.smt_state,
              "quick": args.quick, "build_dir": str(args.build_dir.resolve()),
              "timeout_seconds": args.timeout, "commands": [], "cases": [], "success": False}
    def save():
        (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    def command(binary, *options, reject=False, message=None):
        record = invoke([binary, *options], args.timeout)
        report["commands"].append(record)
        save()
        require(not record["timeout"], "Timeout: " + " ".join(record["argv"]))
        if reject:
            require(record["returncode"] > 0, "No se rechazo limpiamente una entrada invalida")
            require(message in record["stderr"], "Motivo de rechazo inesperado: " + record["stderr"])
        else:
            require(record["returncode"] == 0, record["stderr"] or "Comando fallido")
        return record["stdout"]
    try:
        binaries = {model: args.build_dir.resolve() / BINARIES[model] for model in set(args.models + ["sequential"])}
        for model, binary in binaries.items():
            require(binary.is_file(), "Falta ejecutable: " + str(binary))
        report["binaries_sha256"] = {model: sha(binary) for model, binary in binaries.items()}
        # La suite completa falla si el destino no está disponible; nunca se omite silenciosamente.
        for model in ("CMP", "SMT"):
            if model in args.models:
                options = ["--list-cpus"] + (["--smt-state", args.smt_state] if model == "SMT" else [])
                report[model + "_topology"] = command(binaries[model], *options)
        sources = []
        if args.input:
            require(args.input.is_file(), "Entrada inexistente")
            sources.append((args.input.resolve(), args.range, args.stride))
        else:
            for pattern in (["pattern"] if args.quick else ["pattern", "constant"]):
                image = directory / (pattern + ".ppm")
                pixels = bytes((i * 17 + i // 7) % 256 if pattern == "pattern" else 127 for i in range(16 * 24 * 3))
                image.write_bytes(b"P6\n16 24\n255\n" + pixels)
                for block, stride in ([(4, 3)] if args.quick else [(4, 3), (8, 16)]):
                    sources.append((image, block, stride))
        report["inputs_sha256"] = {str(source): sha(source) for source, _, _ in sources}
        for source, block, stride in sources:
            for gray in (False, True):
                mode = "compress-gray" if gray else "compress"
                case_dir = directory / (source.stem + f"-r{block}-s{stride}-" + ("gray" if gray else "rgb"))
                case_dir.mkdir()
                reference, reconstructed = case_dir / "reference.frc", case_dir / "reference.png"
                original_hash = sha(source)
                command(binaries["sequential"], mode, source, reference, "--range", block, "--stride", stride)
                command(binaries["sequential"], "decompress", reference, reconstructed, "--iterations", args.iterations)
                for model in args.models:
                    for teams, members in layouts(model, args.quick):
                        case = {"model": model, "teams": teams, "threads_per_team": members,
                                "input": str(source), "gray": gray, "range": block, "stride": stride, "status": "failed"}
                        report["cases"].append(case)
                        prefix = case_dir / f"{model}-{teams}x{members}"
                        encoded, png, affinity = prefix.with_suffix(".frc"), prefix.with_suffix(".png"), prefix.with_suffix(".csv")
                        options = ["--range", block, "--stride", stride, "--teams", teams, "--threads-per-team", members]
                        if model == "fine-grained":
                            options += ["--quantum-domains", args.quantum_domains]
                        if model in ("CMP", "SMT"):
                            options += ["--report-affinity", affinity]
                        if model == "SMT":
                            options += ["--smt-state", args.smt_state]
                        command(binaries[model], mode, source, encoded, *options)
                        require(sha(encoded) == sha(reference), "FRC diferente al secuencial: " + str(prefix))
                        command(binaries["sequential"], "decompress", encoded, png, "--iterations", args.iterations)
                        require(sha(png) == sha(reconstructed), "Reconstruccion diferente: " + str(prefix))
                        if model in ("CMP", "SMT"):
                            check_affinity(affinity, model, teams, members, 1 if gray else 3, args.smt_state)
                        case.update(status="passed", frc_sha256=sha(encoded), png_sha256=sha(png))
                        save()
                require(sha(source) == original_hash, "La entrada fue modificada")
        # Errores de parámetros, modelo fijo y salidas protegidas: comprobarlos con todos los modelos.
        source = sources[0][0]
        sentinel = directory / "protected.frc"
        sentinel.write_bytes(b"preserve")
        input_hash = sha(source)
        for model in args.models:
            checks = [(["compress", source, sentinel, "--teams", 0], "entero positivo"),
                      (["compress", source, sentinel, "--model", "CMP" if model != "CMP" else "SMT"], "solo admite el modelo")]
            if model != "sequential":
                checks.append((["decompress", sentinel, directory / "protected.png"], "Decodificacion"))
            if model != "fine-grained":
                checks.append((["compress", source, sentinel, "--quantum-domains", 1], "requiere fine-grained"))
            checks.append((["compress", source, source], "archivos distintos"))
            for options, message in checks:
                command(binaries[model], *options, reject=True, message=message)
                require(sentinel.read_bytes() == b"preserve" and sha(source) == input_hash, "Error altero archivos protegidos")
        report["success"] = True
        save()
        print(f"OK: {len(report['cases'])} casos exactos FRC/PNG; errores verificados" + (" y afinidad fisica verificada." if any(m in args.models for m in ("CMP", "SMT")) else "."))
        print("Registro:", directory / "report.json")
        return 0
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        save()
        print("ERROR:", error)
        print("Registro parcial:", directory / "report.json")
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--results-dir", type=Path, default=ROOT / "results/integration")
    parser.add_argument("--models", nargs="+", choices=BINARIES, default=list(BINARIES))
    parser.add_argument("--smt-state", choices=("on", "off"), default="on")
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--input", type=Path)
    for name, default in (("range", 8), ("stride", 8), ("iterations", 80), ("quantum-domains", 1), ("timeout", 120)):
        parser.add_argument("--" + name, type=int, default=default)
    args = parser.parse_args()
    if min(args.range, args.stride, args.iterations, args.quantum_domains, args.timeout) < 1:
        parser.error("Los parametros deben ser positivos")
    if args.range < 2 or args.range > 64 or args.stride > 2 * args.range or args.iterations > 10000:
        parser.error("Range, stride o iterations fuera de limites")
    if len(set(args.models)) != len(args.models):
        parser.error("No repetir modelos")
    return execute(args)


if __name__ == "__main__":
    raise SystemExit(main())
