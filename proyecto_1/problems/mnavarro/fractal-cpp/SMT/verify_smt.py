"""Registro funcional ON/OFF en Ubuntu/Ryzen; no modifica SMT ni hace benchmarks."""
import argparse
import csv
import hashlib
import json
import pathlib
import subprocess


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def affinity(directory, state, workers, members):
    with (directory / "affinity.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    channels = sorted({int(row["channel"]) for row in rows})
    if channels not in ([0], [0, 1, 2]):
        raise ValueError("Canales de afinidad invalidos")
    for channel in channels:
        group = [row for row in rows if int(row["channel"]) == channel]
        if len(group) != workers or sorted(int(r["worker"]) for r in group) != list(range(workers)):
            raise ValueError("Trabajadores de afinidad incompletos")
        cpus = set()
        for row in group:
            if (row["profile"] != "target" or row["smt_state"] != state or
                int(row["smt_active"]) != int(state == "on") or
                int(row["online_cpus"]) != (16 if state == "on" else 8) or
                row["smt_control"] not in (("on",) if state == "on" else ("off", "forceoff", "notsupported")) or
                int(row["team"]) != int(row["worker"]) // members or
                row["cpu"] != row["observed_before"] or row["cpu"] != row["observed_after"]):
                raise ValueError("Estado o afinidad efectiva incompatible")
            cpus.add(row["cpu"])
        if workers == 16 and len(cpus) != (16 if state == "on" else 8):
            raise ValueError("Distribucion ON/OFF incompleta")
    return len(channels)


def run(args):
    build = args.build_dir.resolve()
    smt, seq = build / "fractal_smt", build / "fractal_seq"
    # Verifica destino y estado antes de crear resultados; nunca cambia el kernel.
    topology = subprocess.check_output([str(smt), "--list-cpus", "--smt-state", args.state], text=True)
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "topology.txt").write_text(topology)
    logs = []
    def command(binary, *values):
        argv = [str(binary), *map(str, values)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=args.timeout)
        logs.append({"argv": argv, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        (directory / "commands.json").write_text(json.dumps(logs, indent=2))
        if result.returncode:
            raise ValueError(result.stderr)
    mode = "compress-gray" if args.gray else "compress"
    options = ["--range", args.range, "--stride", args.stride]
    command(seq, mode, args.input.resolve(), directory / "sequential.frc", *options)
    command(smt, mode, args.input.resolve(), directory / "smt.frc", *options,
            "--smt-state", args.state, "--teams", args.teams, "--threads-per-team", args.threads_per_team,
            "--report-affinity", directory / "affinity.csv")
    for model in ("sequential", "smt"):
        command(seq, "decompress", directory / (model + ".frc"), directory / (model + ".png"), "--iterations", args.iterations)
    for extension in ("frc", "png"):
        if digest(directory / ("sequential." + extension)) != digest(directory / ("smt." + extension)):
            raise ValueError("Resultado diferente al secuencial: " + extension)
    channels = affinity(directory, args.state, args.teams * args.threads_per_team, args.threads_per_team)
    report = {"schema": 1, "state": args.state,
              "config": {key: getattr(args, key) for key in ("teams", "threads_per_team", "range", "stride", "iterations", "gray")},
              "channels": channels, "input_sha256": digest(args.input),
              "machine_sha256": digest("/etc/machine-id"),
              "binaries": {"smt": digest(smt), "sequential": digest(seq)},
              "artifacts": {name: digest(directory / name) for name in
                            ("smt.frc", "sequential.frc", "smt.png", "sequential.png", "affinity.csv", "topology.txt", "commands.json")}}
    (directory / "run.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Validacion funcional registrada:", directory / "run.json")


def load(directory, state):
    report = json.loads((directory / "run.json").read_text())
    if report["schema"] != 1 or report["state"] != state:
        raise ValueError("Estado o formato del registro incorrecto")
    required = {"smt.frc", "sequential.frc", "smt.png", "sequential.png", "affinity.csv", "topology.txt", "commands.json"}
    if set(report["artifacts"]) != required:
        raise ValueError("Artefactos incompletos")
    for name, expected in report["artifacts"].items():
        if digest(directory / name) != expected:
            raise ValueError("Artefacto modificado: " + name)
    for extension in ("frc", "png"):
        if report["artifacts"]["smt." + extension] != report["artifacts"]["sequential." + extension]:
            raise ValueError("Resultado diferente al secuencial")
    config = report["config"]
    if affinity(directory, state, config["teams"] * config["threads_per_team"], config["threads_per_team"]) != report["channels"]:
        raise ValueError("Cantidad de canales incompatible")
    return report


def compare(args):
    on, off = load(args.on, "on"), load(args.off, "off")
    for key in ("config", "channels", "input_sha256", "machine_sha256", "binaries"):
        if on[key] != off[key]:
            raise ValueError("Las corridas no son comparables: " + key)
    for name in ("smt.frc", "smt.png"):
        if on["artifacts"][name] != off["artifacts"][name]:
            raise ValueError("Resultados ON/OFF diferentes: " + name)
    result = {"functional_match": True, "on": str(args.on.resolve()), "off": str(args.off.resolve()),
              "config": on["config"], "note": "Comparacion funcional; benchmarks estadisticos y Perf pendientes."}
    with args.output.open("x") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print("Configuraciones ON/OFF equivalentes:", args.output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    record = sub.add_parser("run")
    record.add_argument("--state", choices=("on", "off"), required=True)
    record.add_argument("--input", type=pathlib.Path, required=True)
    record.add_argument("--output-dir", type=pathlib.Path, required=True)
    record.add_argument("--build-dir", type=pathlib.Path, default=pathlib.Path("build"))
    for name, default in (("teams", 4), ("threads-per-team", 4), ("range", 8), ("stride", 8), ("iterations", 80), ("timeout", 600)):
        record.add_argument("--" + name, type=int, default=default)
    record.add_argument("--gray", action="store_true")
    check = sub.add_parser("compare")
    check.add_argument("--on", type=pathlib.Path, required=True)
    check.add_argument("--off", type=pathlib.Path, required=True)
    check.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "run":
            if min(args.teams, args.threads_per_team, args.range, args.stride, args.iterations, args.timeout) < 1:
                raise ValueError("Parametros positivos requeridos")
            run(args)
        else:
            compare(args)
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, "Error: " + str(error) + "\n")


if __name__ == "__main__":
    main()
