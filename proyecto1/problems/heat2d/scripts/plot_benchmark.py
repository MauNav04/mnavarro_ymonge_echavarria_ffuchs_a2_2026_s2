#!/usr/bin/env python3
"""
==============================================================================
Generador de Gráficas de Rendimiento Estandarizado - Heat2D
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Problema: Difusión Térmica 2D (Heat2D)
==============================================================================

Genera la suite completa de figuras científicas a 300 DPI alineada 100% con
el estándar del proyecto (paleta institucional CE 4302 y español formal):
  01_tiempos_ejecucion_boxplot.png  - Distribución de tiempos y Tukey boxplots.
  02_speedup_comparativo.png        - Speedup con IC 95% y línea base 1.0x.
  03_eficiencia_paralela.png        - Eficiencia paralela vs ideal 100% y ref 50%.
  04_escalabilidad_hilos.png        - Escalabilidad fuerte vs Amdahl (90%, 95%, 98%).
  08_dashboard_consolidado.png      - Dashboard científico consolidado (2x2 multipanel).
  09_throughput_mupdates.png        - Throughput en MUpdates/s.
  11_convergencia_estabilidad.png   - Convergencia de la media acumulada (200 corridas).

(Las figuras 05, 06, 07 y 10 de perfilado microarquitectónico son generadas por profile_perf.py).
==============================================================================
"""

import os
import sys

# Evitar advertencias de permisos en Linux para matplotlib
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_heat2d"

import argparse
import csv
import math
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ==============================================================================
# CONFIGURACIÓN ESTÉTICA Y PALETA CIENTÍFICA ESTÁNDAR (CE 4302)
# ==============================================================================
plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.titleweight": "bold",
    "axes.labelsize": 11,
    "axes.labelweight": "semibold",
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "legend.fontsize": 10,
    "figure.titlesize": 15,
    "figure.titleweight": "bold",
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
    "axes.grid": True,
    "grid.alpha": 0.35,
    "grid.linestyle": "--",
    "axes.axisbelow": True,
})

PALETA_MODELOS = {
    "sequential": "#64748b",      # Slate Gray
    "cmp": "#1f77b4",             # Azul institucional
    "smt": "#059669",             # Verde esmeralda
    "coarse_grained": "#7c3aed",  # Púrpura
    "fine_grained": "#0891b2",    # Teal / Cian
}

NOMBRES_ESPANOL = {
    "sequential": "Secuencial",
    "cmp": "CMP",
    "smt": "SMT",
    "coarse_grained": "Coarse-Grained",
    "fine_grained": "Fine-Grained",
}

MODEL_ORDER = {
    "sequential": 0,
    "fine_grained": 1,
    "coarse_grained": 2,
    "cmp": 3,
    "smt": 4,
}


@dataclass(frozen=True)
class SummaryRow:
    model: str
    logical_threads: int
    sample_count: int

    mean_seconds: float
    std_seconds: float
    cv_percent: float
    time_ci95_low: float
    time_ci95_high: float

    mean_mupdates_per_second: float
    throughput_ci95_low: float
    throughput_ci95_high: float

    mean_speedup_vs_sequential: float
    speedup_vs_sequential_ci95_low: float
    speedup_vs_sequential_ci95_high: float

    mean_model_scalability: float
    model_scalability_ci95_low: float
    model_scalability_ci95_high: float

    mean_model_efficiency: float
    model_efficiency_ci95_low: float
    model_efficiency_ci95_high: float


@dataclass(frozen=True)
class RawSample:
    run: int
    model: str
    logical_threads: int
    elapsed_seconds: float


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Generador estandarizado de gráficas científicas Heat2D."
    )
    parser.add_argument("summary_csv", type=Path, help="CSV de resumen estadístico (summary.csv).")
    parser.add_argument("--raw", type=Path, default=None, help="CSV con muestras crudas (raw_results.csv).")
    parser.add_argument("--output-dir", type=Path, default=Path("proyecto1/problems/heat2d/metricas/graficas"),
                        help="Directorio donde se exportarán las gráficas PNG.")
    return parser.parse_args()


def load_summary(path: Path) -> list[SummaryRow]:
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            m = r["model"]
            t = int(r["logical_threads"])
            n = int(r["n"])
            mean_s = float(r["mean_seconds"])
            std_s = float(r["std_seconds"]) if "std_seconds" in r else 0.0
            cv = float(r["cv_percent"]) if "cv_percent" in r else (std_s / mean_s * 100.0 if mean_s > 0 else 0.0)

            rows.append(SummaryRow(
                model=m,
                logical_threads=t,
                sample_count=n,
                mean_seconds=mean_s,
                std_seconds=std_s,
                cv_percent=cv,
                time_ci95_low=float(r["time_ci95_low"]),
                time_ci95_high=float(r["time_ci95_high"]),
                mean_mupdates_per_second=float(r["mean_mupdates_per_second"]),
                throughput_ci95_low=float(r["throughput_ci95_low_mupdates"]),
                throughput_ci95_high=float(r["throughput_ci95_high_mupdates"]),
                mean_speedup_vs_sequential=float(r["mean_speedup_vs_sequential"]),
                speedup_vs_sequential_ci95_low=float(r["speedup_vs_sequential_ci95_low"]),
                speedup_vs_sequential_ci95_high=float(r["speedup_vs_sequential_ci95_high"]),
                mean_model_scalability=float(r["mean_model_scalability"]),
                model_scalability_ci95_low=float(r["model_scalability_ci95_low"]),
                model_scalability_ci95_high=float(r["model_scalability_ci95_high"]),
                mean_model_efficiency=float(r["mean_model_efficiency"]),
                model_efficiency_ci95_low=float(r["model_efficiency_ci95_low"]),
                model_efficiency_ci95_high=float(r["model_efficiency_ci95_high"]),
            ))
    return rows


def load_raw_samples(path: Path) -> list[RawSample]:
    samples = []
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            samples.append(RawSample(
                run=int(r["run"]),
                model=r["model"],
                logical_threads=int(r["logical_threads"]),
                elapsed_seconds=float(r["elapsed_seconds"]),
            ))
    return samples


def case_label(model: str, logical_threads: int) -> str:
    nombre = NOMBRES_ESPANOL.get(model, model)
    if model == "sequential":
        return "Secuencial\n(1 hilo)"
    hilos_str = "hilo" if logical_threads == 1 else "hilos"
    return f"{nombre}\n({logical_threads} {hilos_str})"


def create_asymmetric_errors(means, lower_bounds, upper_bounds):
    lower_errors = [max(0.0, m - low) for m, low in zip(means, lower_bounds)]
    upper_errors = [max(0.0, up - m) for m, up in zip(means, upper_bounds)]
    return [lower_errors, upper_errors]


# ==============================================================================
# 1. BOXPLOT DE TIEMPOS DE EJECUCIÓN (200 CORRIDAS)
# ==============================================================================
def save_distribution_boxplot(raw_samples: list[RawSample], output_dir: Path):
    groups = defaultdict(list)
    for s in raw_samples:
        groups[(s.model, s.logical_threads)].append(s.elapsed_seconds)

    keys = sorted(groups.keys(), key=lambda k: (MODEL_ORDER.get(k[0], 99), k[1]))
    labels = [case_label(k[0], k[1]) for k in keys]
    values = [groups[k] for k in keys]

    fig, ax = plt.subplots(figsize=(12, 6.5))

    bp = ax.boxplot(
        values,
        tick_labels=labels,
        showmeans=True,
        meanline=True,
        patch_artist=True,
        boxprops=dict(linewidth=1.2, edgecolor="#1a1a1a"),
        medianprops=dict(color="#d90429", linewidth=2.0),
        meanprops=dict(color="#003049", linewidth=1.8, linestyle=":"),
        whiskerprops=dict(color="#1a1a1a", linewidth=1.2),
        capprops=dict(color="#1a1a1a", linewidth=1.2),
        flierprops=dict(marker="o", color="#555555", alpha=0.35, markersize=3),
    )

    for patch, key in zip(bp["boxes"], keys):
        c = PALETA_MODELOS.get(key[0], "#1f77b4")
        patch.set_facecolor(c)
        patch.set_alpha(0.75)

    ax.set_title("Distribución de Tiempos de Ejecución (200 Corridas) - Heat2D", fontsize=14, fontweight="bold")
    ax.set_xlabel("Modelo de Ejecución Concurrente", fontweight="semibold")
    ax.set_ylabel("Tiempo de Ejecución (s) [Menor es mejor]", fontweight="semibold")

    # Leyenda para mediana y media
    linea_mediana = plt.Line2D([0], [0], color="#d90429", linewidth=2.0, label="Mediana")
    linea_media = plt.Line2D([0], [0], color="#003049", linewidth=1.8, linestyle=":", label="Media")
    ax.legend(handles=[linea_mediana, linea_media], loc="upper right", framealpha=0.9)

    plt.tight_layout()
    fig.savefig(output_dir / "01_tiempos_ejecucion_boxplot.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 01_tiempos_ejecucion_boxplot.png")


# ==============================================================================
# 2. SPEEDUP COMPARATIVO CON IC 95%
# ==============================================================================
def save_speedup_plot(rows: list[SummaryRow], output_dir: Path):
    labels = [case_label(r.model, r.logical_threads) for r in rows]
    means = [r.mean_speedup_vs_sequential for r in rows]
    errors = create_asymmetric_errors(
        means,
        [r.speedup_vs_sequential_ci95_low for r in rows],
        [r.speedup_vs_sequential_ci95_high for r in rows],
    )
    colors = [PALETA_MODELOS.get(r.model, "#1f77b4") for r in rows]

    fig, ax = plt.subplots(figsize=(11, 6))
    x_pos = np.arange(len(rows))
    bars = ax.bar(x_pos, means, yerr=errors, capsize=4, color=colors, edgecolor="#1a1a1a", linewidth=1.1, alpha=0.85)

    ax.axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="Línea Base Secuencial (1.0x)")
    ax.set_xticks(x_pos)
    ax.set_xticklabels(labels, rotation=10, ha="right")
    ax.set_ylabel("Speedup ($S_p = T_{sec} / T_p$) [Mayor es mejor]", fontweight="semibold")
    ax.set_title("Aceleración (Speedup) Relativo al Modelo Secuencial con IC 95% - Heat2D", fontsize=14, fontweight="bold")
    ax.legend(loc="upper left")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.2f}x", xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9, fontweight="bold")

    ax.set_ylim(0, max(means) * 1.18 if means else 2.0)
    plt.tight_layout()
    fig.savefig(output_dir / "02_speedup_comparativo.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 02_speedup_comparativo.png")


# ==============================================================================
# 3. EFICIENCIA PARALELA COMPARATIVA (BARRAS DE TODOS LOS MODELOS VS 100%)
# ==============================================================================
def save_efficiency_plot(rows: list[SummaryRow], output_dir: Path):
    labels = [case_label(r.model, r.logical_threads) for r in rows]
    # Eficiencia = (Speedup / p) * 100%
    eff_vals = []
    for r in rows:
        eff = r.mean_model_efficiency
        if eff <= 1.0:
            eff_vals.append(eff * 100.0)
        else:
            eff_vals.append(eff)

    colors = [PALETA_MODELOS.get(r.model, "#1f77b4") for r in rows]

    fig, ax = plt.subplots(figsize=(11, 6))
    x_pos = np.arange(len(rows))
    bars = ax.bar(x_pos, eff_vals, color=colors, edgecolor="#1a1a1a", linewidth=1.1, alpha=0.85)

    ax.axhline(100.0, color="#10b981", linestyle="--", linewidth=1.3, label="Ideal (100%)")
    ax.axhline(50.0, color="#d97706", linestyle=":", linewidth=1.2, label="Referencia (50%)")

    ax.set_xticks(x_pos)
    ax.set_xticklabels(labels, rotation=10, ha="right")
    ax.set_ylabel("Eficiencia Paralela (%) [Mayor es mejor]", fontweight="semibold")
    ax.set_title("Eficiencia Paralela ($E = S_p / p$) - Heat2D", fontsize=14, fontweight="bold")
    ax.legend(loc="upper right")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.1f}%", xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9, fontweight="bold")

    ax.set_ylim(0, max(115.0, max(eff_vals) * 1.15) if eff_vals else 115.0)
    plt.tight_layout()
    fig.savefig(output_dir / "03_eficiencia_paralela.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 03_eficiencia_paralela.png")


# ==============================================================================
# 4. ESCALABILIDAD FUERTE Y LEY DE AMDAHL
# ==============================================================================
def save_scalability_amdahl_plot(rows: list[SummaryRow], output_dir: Path):
    # Filtrar corridas de barrido de hilos (típicamente SMT o CMP)
    smt_rows = sorted([r for r in rows if r.model in ("smt", "cmp")], key=lambda r: r.logical_threads)
    if not smt_rows:
        smt_rows = rows

    threads = [r.logical_threads for r in smt_rows]
    speedups = [r.mean_speedup_vs_sequential for r in smt_rows]

    fig, ax = plt.subplots(figsize=(10, 6.5))

    p_dense = np.linspace(1, max(max(threads), 16), 100)

    # Curvas teóricas de la Ley de Amdahl
    f_vals = [0.98, 0.95, 0.90]
    amdahl_styles = [("-.", "#0284c7"), ("--", "#f59e0b"), (":", "#ef4444")]

    for f_p, (st, col) in zip(f_vals, amdahl_styles):
        s_teorico = 1.0 / ((1.0 - f_p) + (f_p / p_dense))
        ax.plot(p_dense, s_teorico, linestyle=st, color=col, linewidth=1.5, alpha=0.85, label=f"Amdahl $f={int(f_p*100)}\\%$")

    # Aceleración lineal ideal
    ax.plot(p_dense, p_dense, linestyle="--", color="#10b981", linewidth=1.6, alpha=0.9, label="Ideal Lineal ($S = p$)")

    # Curva experimental medida
    color_exp = PALETA_MODELOS.get(smt_rows[0].model, "#059669")
    ax.plot(threads, speedups, marker="o", markersize=7, linewidth=2.4, color=color_exp, label="Medición Experimental (SMT)")

    for x, y in zip(threads, speedups):
        ax.annotate(f"{y:.2f}x", xy=(x, y), xytext=(0, 7), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    ax.set_title("Escalabilidad Fuerte y Comparativa con la Ley de Amdahl - Heat2D", fontsize=14, fontweight="bold")
    ax.set_xlabel("Número de Hilos / Procesadores Lógicos ($p$)", fontweight="semibold")
    ax.set_ylabel("Speedup ($S_p$)", fontweight="semibold")
    ax.set_xticks(threads)
    ax.set_xlim(0.5, max(threads) + 1)
    ax.set_ylim(0, max(max(speedups) * 1.25, max(threads) * 1.05))
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    fig.savefig(output_dir / "04_escalabilidad_hilos.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 04_escalabilidad_hilos.png")


# ==============================================================================
# 8. DASHBOARD CIENTÍFICO CONSOLIDADO (2x2)
# ==============================================================================
def save_consolidated_dashboard(rows: list[SummaryRow], output_dir: Path):
    labels = [case_label(r.model, r.logical_threads) for r in rows]
    short_labels = [l.replace("\n", " ") for l in labels]
    colors = [PALETA_MODELOS.get(r.model, "#1f77b4") for r in rows]
    x_pos = np.arange(len(rows))

    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    # (0, 0) Tiempo de Cómputo (s)
    means_t = [r.mean_seconds for r in rows]
    err_t = create_asymmetric_errors(means_t, [r.time_ci95_low for r in rows], [r.time_ci95_high for r in rows])
    bars_t = axs[0, 0].bar(x_pos, means_t, yerr=err_t, capsize=4, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[0, 0].set_title("(A) Tiempo Medio de Cómputo (s) [IC 95%]")
    axs[0, 0].set_ylabel("Tiempo (s)")
    axs[0, 0].set_xticks(x_pos)
    axs[0, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[0, 0].set_ylim(0, max(means_t) * 1.18 if means_t else 1.0)
    for b in bars_t:
        axs[0, 0].annotate(f"{b.get_height():.2f}s", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (0, 1) Speedup vs Secuencial
    sp_vals = [r.mean_speedup_vs_sequential for r in rows]
    err_sp = create_asymmetric_errors(sp_vals, [r.speedup_vs_sequential_ci95_low for r in rows], [r.speedup_vs_sequential_ci95_high for r in rows])
    bars_sp = axs[0, 1].bar(x_pos, sp_vals, yerr=err_sp, capsize=4, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[0, 1].axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="Secuencial (1.0x)")
    axs[0, 1].set_title("(B) Speedup Relativo a Secuencial ($S_p$)")
    axs[0, 1].set_ylabel("Speedup ($S_p$)")
    axs[0, 1].set_xticks(x_pos)
    axs[0, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[0, 1].set_ylim(0, max(sp_vals) * 1.18 if sp_vals else 2.0)
    axs[0, 1].legend(loc="upper left", fontsize=9)
    for b in bars_sp:
        axs[0, 1].annotate(f"{b.get_height():.2f}x", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 0) Eficiencia Paralela (%)
    eff_vals = [r.mean_model_efficiency * 100.0 if r.mean_model_efficiency <= 1.0 else r.mean_model_efficiency for r in rows]
    bars_ef = axs[1, 0].bar(x_pos, eff_vals, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[1, 0].axhline(100.0, color="#10b981", linestyle="--", linewidth=1.2, label="Ideal (100%)")
    axs[1, 0].set_title("(C) Eficiencia Paralela ($E = S_p / p$)")
    axs[1, 0].set_ylabel("Eficiencia (%)")
    axs[1, 0].set_xticks(x_pos)
    axs[1, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[1, 0].set_ylim(0, max(115.0, max(eff_vals) * 1.15) if eff_vals else 115.0)
    axs[1, 0].legend(loc="upper right", fontsize=9)
    for b in bars_ef:
        axs[1, 0].annotate(f"{b.get_height():.1f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 1) Coeficiente de Variación (CV = std / mean * 100%)
    cv_vals = [r.cv_percent for r in rows]
    bars_cv = axs[1, 1].bar(x_pos, cv_vals, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[1, 1].set_title("(D) Coeficiente de Variación ($CV = \\sigma / \\mu$) [Jitter / Inestabilidad]")
    axs[1, 1].set_ylabel("CV (%) [Menor es más estable]")
    axs[1, 1].set_xticks(x_pos)
    axs[1, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[1, 1].set_ylim(0, max(cv_vals) * 1.25 if cv_vals and max(cv_vals) > 0 else 5.0)
    for b in bars_cv:
        axs[1, 1].annotate(f"{b.get_height():.2f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    fig.suptitle("Dashboard Científico Consolidado de Rendimiento - Heat2D", fontsize=16, fontweight="bold")
    plt.tight_layout()
    fig.savefig(output_dir / "08_dashboard_consolidado.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 08_dashboard_consolidado.png")


# ==============================================================================
# 9. THROUGHPUT EN MILLONES DE ACTUALIZACIONES POR SEGUNDO (MUpdates/s)
# ==============================================================================
def save_throughput_plot(rows: list[SummaryRow], output_dir: Path):
    labels = [case_label(r.model, r.logical_threads) for r in rows]
    means = [r.mean_mupdates_per_second for r in rows]
    errors = create_asymmetric_errors(
        means,
        [r.throughput_ci95_low for r in rows],
        [r.throughput_ci95_high for r in rows],
    )
    colors = [PALETA_MODELOS.get(r.model, "#1f77b4") for r in rows]

    fig, ax = plt.subplots(figsize=(11, 6))
    x_pos = np.arange(len(rows))
    bars = ax.bar(x_pos, means, yerr=errors, capsize=4, color=colors, edgecolor="#1a1a1a", linewidth=1.1, alpha=0.85)

    seq_tp = rows[0].mean_mupdates_per_second if rows else None
    if seq_tp:
        ax.axhline(seq_tp, color="#d90429", linestyle="--", linewidth=1.2, label=f"Línea Base Secuencial ({seq_tp:.0f} MUpdates/s)")

    ax.set_xticks(x_pos)
    ax.set_xticklabels(labels, rotation=10, ha="right")
    ax.set_ylabel("Throughput (MUpdates / s) [Mayor es mejor]", fontweight="semibold")
    ax.set_title("Rendimiento de Cómputo (Throughput) en MUpdates/s - Heat2D", fontsize=14, fontweight="bold")
    ax.legend(loc="upper left")

    for bar in bars:
        h = bar.get_height()
        speedup_str = f" ({h/seq_tp:.2f}x)" if seq_tp and seq_tp > 0 else ""
        ax.annotate(f"{h:.0f}{speedup_str}", xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 5), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    ax.set_ylim(0, max(means) * 1.18 if means else 100.0)
    plt.tight_layout()
    fig.savefig(output_dir / "09_throughput_mupdates.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 09_throughput_mupdates.png")


# ==============================================================================
# 11. CONVERGENCIA Y ESTABILIDAD ACUMULADA (200 CORRIDAS)
# ==============================================================================
def save_convergence_plot(raw_samples: list[RawSample], output_dir: Path):
    groups = defaultdict(list)
    for s in raw_samples:
        groups[(s.model, s.logical_threads)].append(s.elapsed_seconds)

    keys = sorted(groups.keys(), key=lambda k: (MODEL_ORDER.get(k[0], 99), k[1]))

    fig, ax = plt.subplots(figsize=(11, 6))

    for key in keys:
        raw_vals = groups[key]
        n = len(raw_vals)
        if n < 2:
            continue
        accum = np.cumsum(raw_vals) / np.arange(1, n + 1)
        color = PALETA_MODELOS.get(key[0], "#1f77b4")
        lbl = case_label(key[0], key[1]).replace("\n", " ")
        ax.plot(range(1, n + 1), accum, label=lbl, color=color, linewidth=1.5, alpha=0.9)

    ax.set_title("Convergencia de la Media Acumulada (200 Corridas) - Heat2D", fontsize=14, fontweight="bold")
    ax.set_xlabel("Número de Iteración / Corrida", fontweight="semibold")
    ax.set_ylabel("Tiempo Medio Acumulado (s)", fontweight="semibold")
    ax.legend(loc="upper right", fontsize=8.5)

    plt.tight_layout()
    fig.savefig(output_dir / "11_convergencia_estabilidad.png", dpi=300)
    plt.close(fig)
    print("  [+] Generado: 11_convergencia_estabilidad.png")


# ==============================================================================
# FUNCIÓN PRINCIPAL
# ==============================================================================
def main():
    args = parse_arguments()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    print("\n==============================================================================")
    print("      GENERACIÓN DE GRÁFICAS ESTANDARIZADAS (300 DPI) - Heat2D                ")
    print("==============================================================================")
    print(f"Resumen CSV : {args.summary_csv}")
    print(f"Directorio  : {args.output_dir}\n")

    rows = load_summary(args.summary_csv)

    # 1. Gráficas estándar de benchmarking
    save_speedup_plot(rows, args.output_dir)
    save_efficiency_plot(rows, args.output_dir)
    save_scalability_amdahl_plot(rows, args.output_dir)
    save_consolidated_dashboard(rows, args.output_dir)
    save_throughput_plot(rows, args.output_dir)

    # 2. Gráficas dependientes de raw_results (boxplot y convergencia)
    if args.raw is not None and args.raw.is_file():
        raw_samples = load_raw_samples(args.raw)
        save_distribution_boxplot(raw_samples, args.output_dir)
        save_convergence_plot(raw_samples, args.output_dir)
    else:
        print("  [!] Advertencia: No se proporcionó --raw válido. Se omitieron boxplot y convergencia.")

    print("\n[OK] Gráficas de Heat2D generadas y estandarizadas exitosamente.\n")


if __name__ == "__main__":
    main()