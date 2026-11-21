"""Resumen estadístico y figuras reproducibles desde raw.csv; jamás ejecuta benchmarks."""
import argparse
from collections import defaultdict
import csv
import json
import math
import os
import tempfile
from importlib.metadata import version
from pathlib import Path
import statistics

from scipy.stats import t as student_t


def require(ok, message):
    if not ok:
        raise ValueError(message)


def describe(values):
    require(values and all(math.isfinite(v) and v > 0 for v in values), "Muestras deben ser finitas y positivas")
    n, mean = len(values), statistics.mean(values)
    std = statistics.stdev(values) if n > 1 else None
    margin = float(student_t.ppf(0.975, n - 1)) * std / math.sqrt(n) if n > 1 else None
    return {"corridas": n, "media_ms": mean, "std_dev_ms": std, "error_estandar_ms": std / math.sqrt(n) if std is not None else None, "margen_error_ms": margin,
            "ci_95_inf_ms": mean - margin if margin is not None else None,
            "ci_95_sup_ms": mean + margin if margin is not None else None,
            "min_ms": min(values), "max_ms": max(values), "mediana_ms": statistics.median(values),
            "cv_pct": 100 * std / mean if std is not None else None}


def ratio_interval(baseline, parallel):
    """IC aproximado log-delta con incertidumbre de ambas medias y g.l. de Welch."""
    if min(baseline["corridas"], parallel["corridas"]) < 2:
        return None, None
    a = baseline["std_dev_ms"] ** 2 / baseline["corridas"] / baseline["media_ms"] ** 2
    b = parallel["std_dev_ms"] ** 2 / parallel["corridas"] / parallel["media_ms"] ** 2
    ratio = baseline["media_ms"] / parallel["media_ms"]
    if a + b == 0:
        return ratio, ratio
    df = (a + b) ** 2 / (a * a / (baseline["corridas"] - 1) + b * b / (parallel["corridas"] - 1))
    margin = float(student_t.ppf(0.975, df)) * math.sqrt(a + b)
    return ratio * math.exp(-margin), ratio * math.exp(margin)


def read_campaigns(paths, allow_partial=False):
    groups = defaultdict(list)
    metadata = {}
    common = None
    fingerprints = {}
    for path in paths:
        path = Path(path)
        meta = json.loads((path.parent / "metadata.json").read_text())
        require(meta["schema"] == 1, "Formato de campaña no soportado")
        cid = meta["campaign_id"]
        require(cid not in metadata, "Campaña repetida")
        require(meta["measurement_kind"] in ("physical", "diagnostic", "synthetic"), "Tipo de medicion invalido")
        require(allow_partial or meta["success"], "Campaña incompleta; usar --allow-partial para inspeccionarla")
        if meta["measurement_kind"] == "physical":
            require(meta["runs"] >= 200 and meta.get("state_verified", False), "Una campaña fisica formal requiere 200 corridas y estado verificado")
        identity = tuple(meta[k] for k in ("measurement_kind", "machine_sha256", "input_sha256", "range", "stride", "gray", "quantum")) + (bool(meta.get("state_verified", False)),)
        require(common is None or identity == common, "Campañas incompatibles: tipo, maquina, imagen o parametros diferentes")
        common = identity
        for model, digest in meta["binaries_sha256"].items():
            require(model not in fingerprints or fingerprints[model] == digest, "Binarios diferentes entre campañas")
            fingerprints[model] = digest
        metadata[cid] = meta
        seen = set()
        with path.open(newline="") as stream:
            for row in csv.DictReader(stream):
                require(row["campaign_id"] == cid and row["measurement_kind"] == meta["measurement_kind"], "Identidad de fila incorrecta")
                require(row["smt_state"] == meta["smt_state"] and row["experiment"] == meta["experiment"], "Estado/experimento incorrecto")
                require(row["input_sha256"] == meta["input_sha256"], "Entrada diferente en fila")
                if row["status"] != "ok":
                    require(allow_partial, "CSV contiene corridas fallidas")
                    continue
                for field in ("hilos", "teams", "threads_per_team", "quantum", "range", "stride", "width", "height", "channels", "run_id", "expected_runs", "turns", "scheduler_switches", "stalls", "eventos_contexto", "frc_bytes"):
                    row[field] = int(row[field])
                row["tiempo_ms"] = float(row["tiempo_ms"])
                row["state_verified"] = bool(meta.get("state_verified", False))
                require(math.isfinite(row["tiempo_ms"]) and row["tiempo_ms"] > 0, "Tiempo CSV invalido")
                require(row["teams"] * row["threads_per_team"] == row["hilos"] and min(row["teams"], row["threads_per_team"]) > 0, "Cantidad de hilos incorrecta")
                require(row["width"] > 0 and row["height"] > 0, "Dimensiones incorrectas")
                if meta.get("image_dimensions"):
                    require([row["width"], row["height"], row["channels"]] == meta["image_dimensions"], "Dimensiones diferentes en fila")
                require(row["range"] == meta["range"] and row["stride"] == meta["stride"] and row["channels"] == (1 if meta["gray"] else 3), "Geometria/canales incompatibles")
                require(row["quantum"] == (meta["quantum"] if row["model_key"] == "fine-grained" else 0), "Quantum incompatible")
                require(row["expected_runs"] == meta["runs"] and 1 <= row["run_id"] <= meta["runs"], "Repeticion fuera de rango")
                if meta["success"]:
                    require(row["frc_sha256"] == meta["reference_sha256"], "Resultado distinto al baseline")
                key = (cid, row["experiment"], row["smt_state"], row["model_key"], row["teams"], row["threads_per_team"], row["quantum"])
                run_key = key + (row["run_id"],)
                require(run_key not in seen, "Corrida duplicada")
                seen.add(run_key)
                groups[key].append(row)
    require(groups, "No hay muestras validas")
    for key, rows in groups.items():
        meta = metadata[key[0]]
        require(allow_partial or len(rows) == meta["runs"], "Numero de muestras incompleto")
        rows.sort(key=lambda row: row["run_id"])
    for cid, meta in metadata.items():
        meta["_analysis_complete"] = bool(meta["success"] and all(len(rows) == meta["runs"] for key, rows in groups.items() if key[0] == cid))
        if meta.get("configurations"):
            expected = {(m, t, n) for m, t, n in meta["configurations"]}
            observed = {(key[3], key[4], key[5]) for key in groups if key[0] == cid}
            require(allow_partial or expected == observed, "Faltan configuraciones de la campaña")
            meta["_analysis_complete"] = meta["_analysis_complete"] and expected == observed
    return groups, metadata


def summarize(groups, metadata):
    baseline = {}
    for key, rows in groups.items():
        if key[3] == "sequential":
            require(key[4:6] == (1, 1), "Baseline no secuencial")
            baseline[key[0]] = describe([r["tiempo_ms"] for r in rows])
    summary, convergence = [], []
    for key, rows in groups.items():
        require(key[0] in baseline, "Falta baseline de la misma campaña y estado SMT")
        first, meta = rows[0], metadata[key[0]]
        values = [r["tiempo_ms"] for r in rows]
        item = {k: first[k] for k in ("campaign_id", "experiment", "measurement_kind", "modelo", "model_key", "modo", "smt_state", "state_verified", "hilos", "teams", "threads_per_team", "quantum")}
        item.update(describe(values))
        speedup = baseline[key[0]]["media_ms"] / item["media_ms"]
        low, high = (1.0, 1.0) if key[3] == "sequential" else ratio_interval(baseline[key[0]], item)
        item.update(speedup=speedup, eficiencia=100 * speedup / first["hilos"], speedup_ci_low=low, speedup_ci_high=high,
                    eficiencia_ci_low=100 * low / first["hilos"] if low is not None else None,
                    eficiencia_ci_high=100 * high / first["hilos"] if high is not None else None,
                    expected_runs=meta["runs"], complete=bool(meta["_analysis_complete"] and len(values) == meta["runs"]),
                    eventos_contexto_mean=statistics.mean(r["eventos_contexto"] for r in rows),
                    scheduler_switches_mean=statistics.mean(r["scheduler_switches"] for r in rows),
                    stalls_mean=statistics.mean(r["stalls"] for r in rows),
                    amdahl_90=1 / (0.10 + 0.90 / first["hilos"]), amdahl_95=1 / (0.05 + 0.95 / first["hilos"]),
                    amdahl_98=1 / (0.02 + 0.98 / first["hilos"]), lineal_ideal=first["hilos"])
        previous = None
        for n in range(1, len(values) + 1):
            stats = describe(values[:n])
            relative = 100 * stats["margen_error_ms"] / stats["media_ms"] if n > 1 else None
            convergence.append({"campaign_id": key[0], "modelo": first["modelo"], "model_key": first["model_key"],
                                "smt_state": first["smt_state"], "teams": key[4], "threads_per_team": key[5], "quantum": key[6],
                                "samples": n, "running_mean_ms": stats["media_ms"], "ci95_halfwidth_ms": stats["margen_error_ms"],
                                "relative_ci95_pct": relative,
                                "mean_change_pct": 100 * abs(stats["media_ms"] - previous) / previous if previous else None})
            previous = stats["media_ms"]
        window = max(1, len(values) // 5)
        item["last_window_drift_pct"] = 100 * abs(statistics.mean(values[-window:]) - item["media_ms"]) / item["media_ms"]
        item["relative_ci95_pct"] = 100 * item["margen_error_ms"] / item["media_ms"] if len(values) > 1 else None
        summary.append(item)
    return summary, convergence


def write_csv(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def plot_all(groups, summary, convergence, directory):
    os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / f"fractal-matplotlib-{os.getuid()}"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from plot_style import MODEL_COLORS
    colors = MODEL_COLORS
    plt.rcParams.update({"font.size": 9, "axes.grid": True, "grid.alpha": 0.25, "savefig.dpi": 300})
    kind = summary[0]["measurement_kind"]
    watermark = {"physical": "Mediciones físicas", "diagnostic": "DIAGNÓSTICO — no benchmark formal", "synthetic": "DATOS SINTÉTICOS — prueba de gráficas"}[kind]
    if any(not row["complete"] for row in summary):
        watermark += " — CAMPAÑA INCOMPLETA"
    def label(row):
        state_label = "SMT" if row.get("state_verified", False) else "SMT esperado"
        return f"{row['modelo']}\n{row['teams']}×{row['threads_per_team']}, {state_label} {row['smt_state'].upper()}"
    def save(fig, name):
        fig.text(0.5, 0.005, watermark, ha="center", fontsize=8, color="#b91c1c" if kind != "physical" else "#444444")
        fig.tight_layout(rect=(0, 0.04, 1, 0.96))
        fig.savefig(directory / name)
        plt.close(fig)
    def bars(ax, rows, field, title, interval=None):
        values = [r[field] if r[field] is not None else float("nan") for r in rows]
        if interval:
            low, high = interval
            errors = [[max(0, r[field] - r[low]) if r[low] is not None else 0 for r in rows],
                      [max(0, r[high] - r[field]) if r[high] is not None else 0 for r in rows]]
        elif field == "media_ms":
            errors = [r["margen_error_ms"] or 0 for r in rows]
        else:
            errors = None
        ax.bar(range(len(rows)), values, yerr=errors, capsize=3, color=[colors[r["model_key"]] for r in rows])
        ax.set_xticks(range(len(rows)))
        ax.set_xticklabels([label(r) for r in rows], rotation=25, ha="right", fontsize=8)
        ax.set_title(title)
    ordinary = [r for r in summary if r["experiment"] == "benchmark"]
    if ordinary:
        fig, ax = plt.subplots(figsize=(14, 7))
        samples = []
        for row in ordinary:
            key = (row["campaign_id"], row["experiment"], row["smt_state"], row["model_key"], row["teams"], row["threads_per_team"], row["quantum"])
            samples.append([r["tiempo_ms"] for r in groups[key]])
        boxes = ax.boxplot(samples, patch_artist=True, showmeans=True)
        for patch, row in zip(boxes["boxes"], ordinary):
            patch.set_facecolor(colors[row["model_key"]])
            patch.set_alpha(0.65)
        ax.set_xticks(range(1, len(ordinary) + 1))
        ax.set_xticklabels([label(r) for r in ordinary], rotation=25, ha="right")
        ax.set_ylabel("Tiempo de cómputo (ms)")
        ax.set_title("Distribución de tiempos: mediana, media y valores atípicos")
        save(fig, "01_execution_boxplot.png")
        for field, title, interval, name, reference in [
            ("speedup", "Speedup vs secuencial (IC 95 % aproximado)", ("speedup_ci_low", "speedup_ci_high"), "02_speedup.png", 1),
            ("eficiencia", "Eficiencia paralela (%)", ("eficiencia_ci_low", "eficiencia_ci_high"), "03_efficiency.png", 100)]:
            fig, ax = plt.subplots(figsize=(14, 7))
            bars(ax, ordinary, field, title, interval)
            ax.axhline(reference, color="black", linestyle="--")
            save(fig, name)
        fig, axes = plt.subplots(1, 2, figsize=(16, 7))
        for ax, models, title in [(axes[0], ("CMP", "SMT"), "CMP y SMT ON/OFF: tiempo medio (IC 95 %)"),
                                  (axes[1], ("coarse-grained", "fine-grained"), "Granularidad: tiempo medio (IC 95 %)")]:
            selected = [r for r in ordinary if r["model_key"] in models]
            if selected:
                bars(ax, selected, "media_ms", title)
            else:
                ax.text(0.5, 0.5, "Sin datos medidos", ha="center")
                ax.set_title(title)
        save(fig, "04_comparisons.png")
        fig, axes = plt.subplots(2, 2, figsize=(18, 13))
        for ax, field, title in zip(axes.flat, ("media_ms", "speedup", "eficiencia", "cv_pct"),
                                   ("Tiempo medio (ms, IC 95 %)", "Speedup", "Eficiencia (%)", "Coeficiente de variación (%)")):
            bars(ax, ordinary, field, title)
        save(fig, "05_dashboard.png")
    scaling = [r for r in summary if r["experiment"] == "scaling"]
    if scaling:
        fig, axes = plt.subplots(1, 2, figsize=(16, 7))
        points = sorted({r["hilos"] for r in scaling})
        for ax in axes:
            ax.set_xlabel("Trabajadores p")
            ax.set_xticks(points)
            ax.axvline(8, linestyle="-.", color="#555555", label="8 núcleos físicos")
        axes[0].plot(points, points, "k--", label="Ideal S=p")
        axes[1].axhline(100, color="black", linestyle="--", label="Ideal 100 %")
        for fraction, style in zip((0.90, 0.95, 0.98), (":", "--", "-.")):
            theory = [1 / (1 - fraction + fraction / n) for n in points]
            axes[0].plot(points, theory, linestyle=style, color="#64748b", label=f"Amdahl f={fraction}")
            axes[1].plot(points, [100 * s / n for s, n in zip(theory, points)], linestyle=style, color="#64748b", label=f"Amdahl f={fraction}")
        families = defaultdict(list)
        for row in scaling:
            families[(row["campaign_id"], row["model_key"], row["smt_state"])].append(row)
        for (_, model, state), rows in families.items():
            rows.sort(key=lambda r: r["hilos"])
            if model == "sequential":
                continue
            for ax, field in zip(axes, ("speedup", "eficiencia")):
                ax.plot([r["hilos"] for r in rows], [r[field] for r in rows], marker="o", color=colors[model],
                        linestyle="-" if state == "on" else "--", label=f"{rows[0]['modelo']} / {state}")
        axes[0].set_title("Escalabilidad fuerte: speedup y ley de Amdahl")
        axes[1].set_title("Escalabilidad fuerte: eficiencia (%)")
        for ax in axes:
            ax.legend(fontsize=7)
        save(fig, "06_strong_scaling_amdahl.png")
    fig, axes = plt.subplots(1, 2, figsize=(16, 7))
    fig_stability, ax_stability = plt.subplots(figsize=(14, 7))
    for key, rows in groups.items():
        first = rows[0]
        name = label(first).replace("\n", " ")
        style = dict(color=colors[first["model_key"]], linestyle="-" if first["smt_state"] == "on" else "--")
        series = [r for r in convergence if (r["campaign_id"], r["model_key"], r["teams"], r["threads_per_team"], r["quantum"]) == (key[0], key[3], key[4], key[5], key[6])]
        axes[0].plot([r["samples"] for r in series], [r["running_mean_ms"] for r in series], label=name, **style)
        valid = [r for r in series if r["relative_ci95_pct"] is not None]
        axes[1].plot([r["samples"] for r in valid], [r["relative_ci95_pct"] for r in valid], label=name, **style)
        ax_stability.plot([r["run_id"] for r in rows], [r["tiempo_ms"] for r in rows], label=name, **style)
    axes[0].set_title("Convergencia: media acumulada (ms)")
    axes[1].set_title("Convergencia: semiancho IC 95 % / media (%)")
    for ax in axes:
        ax.set_xlabel("Muestras")
        ax.legend(fontsize=6)
    save(fig, "07_convergence.png")
    ax_stability.set_title("Estabilidad temporal: tiempos individuales (ms)")
    ax_stability.set_xlabel("Número de corrida")
    ax_stability.legend(fontsize=6)
    save(fig_stability, "08_temporal_stability.png")
    event_rows = [r for r in (ordinary or summary) if r["model_key"] in ("fine-grained", "coarse-grained")]
    if event_rows:
        fig, axes = plt.subplots(1, 2, figsize=(16, 7))
        bars(axes[0], event_rows, "scheduler_switches_mean", "Cambios de turno de software (media)")
        bars(axes[1], event_rows, "stalls_mean", "Stalls simulados de búsqueda (media)")
        save(fig, "09_software_events.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, nargs="+", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()
    try:
        require(not args.output_dir.exists(), "Directorio de analisis ya existe; elegir uno nuevo")
        groups, metadata = read_campaigns(args.csv, args.allow_partial)
        summary, convergence = summarize(groups, metadata)
        args.output_dir.mkdir(parents=True)
        write_csv(args.output_dir / "summary.csv", summary)
        write_csv(args.output_dir / "convergence.csv", convergence)
        write_csv(args.output_dir / "scaling.csv", [r for r in summary if r["experiment"] == "scaling"])
        manifest = {"success": False, "source_csv": [str(p.resolve()) for p in args.csv],
                    "measurement_kind": summary[0]["measurement_kind"], "complete": all(r["complete"] for r in summary),
                    "dependencies": {"scipy": version("scipy"), "matplotlib": version("matplotlib")},
                    "ci_method": "Student-t; speedup approximate log-delta + Welch, independent means",
                    "notes": "Sin eliminar outliers; Amdahl es referencia teorica; convergencia descriptiva."}
        (args.output_dir / "analysis.json").write_text(json.dumps(manifest, indent=2))
        if not args.no_plots:
            plot_all(groups, summary, convergence, args.output_dir)
        manifest["success"] = True
        (args.output_dir / "analysis.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print("Resumen, convergencia y figuras:", args.output_dir)
    except (ValueError, OSError, KeyError, OverflowError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
