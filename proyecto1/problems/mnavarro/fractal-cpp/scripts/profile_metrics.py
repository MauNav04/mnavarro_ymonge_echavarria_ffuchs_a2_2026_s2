"""Estadísticas y figuras Perf desde CSV auditables; nunca completar ausencias con cero."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import statistics

from perf_support import EVENTS, GROUPS, SCOPE, STRATEGY
from test_models import require, sha

COUNTERS = {
    "cycles": "cycles", "instructions": "instructions", "cache_references": "cache-references", "cache_misses": "cache-misses",
    "l1_loads": "L1-dcache-loads", "l1_misses": "L1-dcache-load-misses", "llc_loads": "LLC-loads", "llc_misses": "LLC-load-misses",
    "branches": "branches", "branch_misses": "branch-misses", "context_switches": "context-switches",
    "cpu_migrations": "cpu-migrations", "page_faults": "page-faults", "task_clock_ms": "task-clock", "elapsed_ns": "duration_time",
}
RATIOS = {"ipc": ("instructions", "cycles", 1), "cache_miss_rate_pct": ("cache-misses", "cache-references", 100),
          "l1_miss_rate_pct": ("L1-dcache-load-misses", "L1-dcache-loads", 100),
          "llc_miss_rate_pct": ("LLC-load-misses", "LLC-loads", 100),
          "branch_miss_rate_pct": ("branch-misses", "branches", 100),
          "effective_cpus": ("task-clock", "duration_time", 1e6)}
METRICS = tuple(COUNTERS) + tuple(RATIOS)
IDENTITY = ("machine_sha256", "kernel", "os_release", "input_sha256", "range", "stride", "gray", "quantum", "scope", "strategy", "measurement_kind", "min_running_pct", "perf_version")


def describe(values):
    require(all(math.isfinite(v) and v >= 0 for v in values), "Muestra no finita/negativa")
    n = len(values)
    if not n:
        return dict(n=0, mean=None, std=None, ci95_halfwidth=None)
    mean = statistics.mean(values)
    std, half = None, None
    if n > 1:
        from scipy.stats import t
        std = statistics.stdev(values)
        half = float(t.ppf(.975, n - 1)) * std / math.sqrt(n)
    return dict(n=n, mean=mean, std=std, ci95_halfwidth=half)


def observations(runs):
    values = {name: [] for name in METRICS}
    for events in runs.values():
        def good(event):
            row = events.get(event)
            if row is None or row["status"] != "ok":
                return None
            value = float(row["count"])
            require(math.isfinite(value) and value >= 0, "Contador marcado ok invalido")
            return value
        for name, event in COUNTERS.items():
            value = good(event)
            if value is not None:
                values[name].append(value)
        for name, (numerator, denominator, factor) in RATIOS.items():
            num, den = good(numerator), good(denominator)
            if num is not None and den is not None and den > 0:
                require(events[numerator]["pass_id"] == events[denominator]["pass_id"], "Cociente entre pasadas distintas")
                values[name].append(factor * num / den)
    return values


def load(paths, allow_partial=False):
    groups, sources = {}, []
    identity, binaries, campaigns = None, {}, set()
    for path in paths:
        path = path.resolve()
        meta = json.loads(path.with_name("metadata.json").read_text())
        require(meta.get("experiment") == "perf" and meta.get("schema") == 1, "Metadatos Perf ausentes/incompatibles")
        require(meta.get("scope") == SCOPE and meta.get("strategy") == STRATEGY, "Alcance/estrategia incompatible")
        require(meta.get("success") or allow_partial, "Campaña fallida/incompleta; inspeccionar registros o usar --allow-partial")
        require(not meta.get("check_only"), "setup_perf no contiene muestras")
        require(meta.get("measurement_kind") in ("physical", "diagnostic", "synthetic"), "Tipo de medicion invalido")
        require(meta.get("state_verified") or meta.get("measurement_kind") == "synthetic", "Estado fisico sin verificar")
        require(meta.get("smt_state") in ("on", "off"), "Estado SMT invalido")
        require(meta["runs"] >= (200 if meta["measurement_kind"] == "physical" else 1), "Numero de corridas invalido")
        cid = meta["campaign_id"]
        require(cid not in campaigns, "Campaña duplicada")
        campaigns.add(cid)
        current = tuple(meta.get(k) for k in IDENTITY)
        require(identity is None or identity == current, "Campañas de maquinas/entradas/parametros/alcances diferentes")
        identity = current
        for model, digest in meta["binaries_sha256"].items():
            require(model not in binaries or binaries[model] == digest, "Binarios incompatibles")
            binaries[model] = digest
        expected = {tuple(config) for config in meta["configurations"]}
        state = meta["smt_state"]
        # Precrear configuraciones permite detectar una campaña truncada antes de la primera fila.
        for model, teams, members in expected:
            key = (cid, model, teams, members, state)
            groups[key] = dict(meta=meta, runs={}, label=model, path=str(path))
        with path.open(newline="") as stream:
            for row in csv.DictReader(stream):
                require(row["campaign_id"] == cid and row["measurement_kind"] == meta["measurement_kind"] and row["smt_state"] == state, "Identidad CSV inconsistente")
                config = (row["model_key"], int(row["teams"]), int(row["threads_per_team"]))
                require(config in expected and int(row["hilos"]) == config[1] * config[2], "Configuracion CSV inesperada")
                require(int(row["expected_runs"]) == meta["runs"], "Repeticiones CSV inconsistentes")
                require(int(row["quantum"]) == (meta["quantum"] if config[0] == "fine-grained" else 0), "Cuantum CSV inconsistente")
                event, run = row["event"], int(row["run_id"])
                require(event in EVENTS and 1 <= run <= meta["runs"], "Evento/corrida inesperados")
                require(row["pass_id"] == next(name for name, events in GROUPS.items() if event in events), "Grupo de evento incorrecto")
                if row["status"] == "ok":
                    require(row["frc_sha256"] == meta["reference_sha256"], "Falta equivalencia FRC para muestra valida")
                    value = float(row["count"])
                    require(math.isfinite(value) and value >= 0, "Contador invalido")
                    if event != "duration_time":
                        require(float(row["runtime_ns"]) > 0 and meta["min_running_pct"] <= float(row["running_pct"]) <= 100, "Runtime insuficiente marcado ok")
                    if event in ("task-clock", "duration_time"):
                        require(row["unit"] == ("ms" if event == "task-clock" else "ns"), "Unidad temporal inconsistente")
                group = groups[(cid, *config, state)]
                group["label"] = row["modelo"]
                events = group["runs"].setdefault(run, {})
                require(event not in events, "Evento/corrida duplicados")
                events[event] = row
        sources.append(dict(csv=str(path), csv_sha256=sha(path), metadata_sha256=sha(path.with_name("metadata.json")), campaign_id=cid, success=meta["success"]))
    require(bool(groups), "Sin configuraciones")
    for group in groups.values():
        meta, runs = group["meta"], group["runs"]
        complete = len(runs) == meta["runs"] and all(set(events) == set(EVENTS) for events in runs.values())
        require(complete or allow_partial, "Faltan eventos/corridas en CSV")
        group["complete"] = complete and meta["success"]
    return groups, sources


def write_csv(path, rows):
    require(bool(rows), "Tabla vacia: " + str(path))
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(groups):
    summary, metrics, coverage = [], [], []
    for (cid, model, teams, members, state), group in groups.items():
        meta = group["meta"]
        base = dict(campaign_id=cid, model_key=model, modelo=group["label"], teams=teams, threads_per_team=members,
                    hilos=teams * members, smt_state=state, measurement_kind=meta["measurement_kind"], expected_runs=meta["runs"])
        row = dict(base, complete=group["complete"])
        for metric, values in observations(group["runs"]).items():
            stats = describe(values)
            row[metric] = stats["mean"]
            for field in ("n", "std", "ci95_halfwidth"):
                row[metric + "_" + field] = stats[field]
            metrics.append(dict(base, metric=metric, **stats))
        for event in EVENTS:
            statuses = Counter(events[event]["status"] for events in group["runs"].values() if event in events)
            missing = meta["runs"] - sum(statuses.values())
            if missing:
                statuses["missing_row"] += missing
            coverage.append(dict(base, event=event, valid_n=statuses["ok"], status_counts=json.dumps(dict(statuses), sort_keys=True)))
        summary.append(row)
    return summary, metrics, coverage


def draw(rows, directory, partial):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    names = {"sequential": "Secuencial", "fine-grained": "Fine-Grained", "coarse-grained": "Coarse-Grained", "CMP": "CMP", "SMT": "SMT"}
    labels = [f'{names[r["model_key"]]}\n{r["teams"]}×{r["threads_per_team"]} · SMT {r["smt_state"].upper()}' for r in rows]
    specs = [
        ("10_perf_ipc.png", [("ipc", "IPC (instrucciones/ciclo)", 1)]),
        ("11_perf_cache_miss_rates.png", [("l1_miss_rate_pct", "Fallos L1 (%)", 1), ("llc_miss_rate_pct", "Fallos LLC (%)", 1), ("cache_miss_rate_pct", "Fallos cache genérica (%)", 1)]),
        ("12_perf_context_switches.png", [("context_switches", "Cambios de contexto SO", 1), ("cpu_migrations", "Migraciones CPU", 1), ("page_faults", "Fallos de página", 1)]),
        ("13_perf_dashboard.png", [("ipc", "IPC", 1), ("l1_miss_rate_pct", "Fallos L1 (%)", 1), ("branch_miss_rate_pct", "Fallos de predicción (%)", 1), ("context_switches", "Cambios de contexto SO", 1)]),
        ("14_perf_cpu_utilization.png", [("ipc", "IPC", 1), ("effective_cpus", "CPU efectivas (task-clock/duración)", 1), ("cache_miss_rate_pct", "Fallos cache genérica (%)", 1), ("cycles", "Ciclos (miles de millones)", 1e9)]),
    ]
    kinds = sorted({r["measurement_kind"] for r in rows})
    for filename, panels in specs:
        columns = 2 if len(panels) == 4 else 1
        fig, axes = plt.subplots(math.ceil(len(panels) / columns), columns, figsize=(max(12, len(rows) * (1.8 if columns == 2 else .95)), 4.2 * math.ceil(len(panels) / columns)), squeeze=False)
        for ax, (metric, title, divisor) in zip(axes.flat, panels):
            for index, row in enumerate(rows):
                value = row[metric]
                if value is None:
                    ax.text(index, .04, "N/D", ha="center", transform=ax.get_xaxis_transform())
                else:
                    error = row[metric + "_ci95_halfwidth"]
                    ax.bar(index, value / divisor, color="#2563a6" if row["smt_state"] == "on" else "#d68032",
                           yerr=error / divisor if error is not None else None, capsize=3)
                    if value == 0:
                        ax.text(index, .04, "0", ha="center", transform=ax.get_xaxis_transform())
                    if row[metric + "_n"] < row["expected_runs"]:
                        ax.text(index, .92, f'n={row[metric + "_n"]}', ha="center", transform=ax.get_xaxis_transform(), fontsize=8)
            ax.set_xticks(np.arange(len(rows)), labels, rotation=25, ha="right", fontsize=8)
            ax.set_ylabel(title)
            ax.set_ylim(bottom=0)
            ax.grid(axis="y", alpha=.2)
            ax.set_axisbelow(True)
        fig.suptitle("Linux Perf · " + "/".join(kinds) + (" · INCOMPLETO" if partial else "") + " · IC 95% Student-t; N/D = sin medición válida")
        fig.tight_layout(rect=(0, 0, 1, .96))
        fig.savefig(directory / filename, dpi=300)
        plt.close(fig)


def analyze(paths, directory, allow_partial=False, no_plots=False):
    groups, sources = load(paths, allow_partial)
    rows, metrics, coverage = summarize(groups)
    directory.mkdir(parents=True, exist_ok=True)
    write_csv(directory / "summary.csv", rows)
    write_csv(directory / "metric_statistics.csv", metrics)
    write_csv(directory / "coverage.csv", coverage)
    # Comparar sólo la misma configuración; las razones de contadores no son speedup.
    comparisons = []
    paired = defaultdict(lambda: defaultdict(list))
    for row in rows:
        paired[(row["model_key"], row["teams"], row["threads_per_team"])][row["smt_state"]].append(row)
    for (model, teams, members), states in paired.items():
        if set(states) == {"on", "off"}:
            require(len(states["on"]) == len(states["off"]) == 1, "Multiples campañas para la misma configuracion/estado; analizar por separado")
            on, off = states["on"][0], states["off"][0]
            for metric in METRICS:
                a, b = on[metric], off[metric]
                comparisons.append(dict(model_key=model, teams=teams, threads_per_team=members, metric=metric, on=a, off=b,
                                        off_over_on=b / a if a is not None and a > 0 and b is not None else None))
    if comparisons:
        write_csv(directory / "smt_comparison.csv", comparisons)
    elif (directory / "smt_comparison.csv").exists():
        (directory / "smt_comparison.csv").unlink()
    partial = not all(row["complete"] for row in rows)
    report = dict(schema=1, scope=SCOPE, strategy=STRATEGY, sources=sources, execution_complete=not partial,
                  all_events_measured=all(row["valid_n"] == row["expected_runs"] for row in coverage),
                  measurement_kinds=sorted({row["measurement_kind"] for row in rows}), configurations=len(rows))
    (directory / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    if not no_plots:
        draw(rows, directory, partial)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", nargs="+", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()
    try:
        analyze(args.csv, args.output_dir, args.allow_partial, args.no_plots)
    except (ValueError, OSError, KeyError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")
    print("Tablas/figuras Perf:", args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
