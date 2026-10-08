#!/usr/bin/env python3
"""
==============================================================================
Generador de Gráficos de Rendimiento y Análisis Estadístico - Gray-Scott
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Instituto Tecnológico de Costa Rica (TEC)
==============================================================================
Este script procesa los resultados experimentales de las 200 corridas:
  - metricas/gray_scott_benchmark.csv
  - metricas/resumen_estadistico.csv
y genera un conjunto completo de gráficos con calidad de publicación científica:
  1. Boxplot de Tiempos de Ejecución y Dispersión
  2. Gráfico de Barras de Speedup Comparativo
  3. Gráfico de Barras de Eficiencia Paralela
  4. Comparativas Microarquitectónicas Focalizadas (SMT HW vs SW, Coarse vs Fine)
  5. Dashboard General Consolidado (2x2)

Salida: Guardados en alta resolución (300 DPI) en 'metricas/graficas/'.
==============================================================================
"""

import sys
import os
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

# ==============================================================================
# CONFIGURACIÓN DE RUTAS Y ESTILO VISUAL
# ==============================================================================
script_dir = Path(__file__).resolve().parent
base_dir = script_dir.parent
metricas_dir = base_dir / "metricas"
graficas_dir = metricas_dir / "graficas"
graficas_dir.mkdir(parents=True, exist_ok=True)

csv_crudo = metricas_dir / "gray_scott_benchmark.csv"
csv_resumen = metricas_dir / "resumen_estadistico.csv"

if not csv_crudo.exists():
    print(f"[ERROR] No se encontró el archivo de datos crudos: {csv_crudo}", file=sys.stderr)
    sys.exit(1)

# Estilo global de gráficos
plt.style.use("seaborn-v0_8-whitegrid")
plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
    "font.size": 11,
    "axes.labelsize": 12,
    "axes.titlesize": 14,
    "axes.titleweight": "bold",
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "legend.fontsize": 11,
    "figure.titlesize": 16,
    "figure.titleweight": "bold",
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "savefig.bbox": "tight"
})

# Paleta cromática coordinada por modelo
PALETA_MODELOS = {
    "SMT (HW Real)": "#059669",               # Esmeralda
    "CMP": "#2563eb",                         # Azul Royal
    "Coarse-Grained": "#7c3aed",              # Púrpura
    "SMT (Sobre-suscripción SW)": "#d97706",   # Ámbar
    "Fine-Grained (Paralelo Cíclico)": "#0891b2", # Cian / Teal
    "Fine-Grained (Cooperativo)": "#e11d48",   # Carmesí / Rosa
    "Secuencial": "#64748b"                   # Gris Grafito
}

# Orden de presentación lógico (de más rápido a más lento)
ORDEN_MODELOS = [
    "SMT (HW Real)",
    "CMP",
    "Coarse-Grained",
    "SMT (Sobre-suscripción SW)",
    "Fine-Grained (Paralelo Cíclico)",
    "Secuencial",
    "Fine-Grained (Cooperativo)"
]


def cargar_datos():
    df = pd.read_csv(csv_crudo)
    # Filtrar solo los modelos conocidos en el orden establecido
    df = df[df["modelo"].isin(ORDEN_MODELOS)].copy()
    df["modelo"] = pd.Categorical(df["modelo"], categories=ORDEN_MODELOS, ordered=True)
    return df


# ==============================================================================
# GRÁFICA 1: BOXPLOT DE TIEMPOS DE EJECUCIÓN (200 CORRIDAS)
# ==============================================================================
def graficar_boxplot(df: pd.DataFrame):
    print("--> Generando: 01_tiempos_ejecucion_boxplot.png...")
    fig, ax = plt.subplots(figsize=(12, 7))

    # Boxplot principal
    bp = sns.boxplot(
        data=df,
        x="modelo",
        y="tiempo_ms",
        palette=PALETA_MODELOS,
        width=0.55,
        fliersize=3.5,
        linewidth=1.4,
        ax=ax,
        hue="modelo",
        legend=False
    )

    # Anotaciones con la media de cada modelo
    medias = df.groupby("modelo", observed=True)["tiempo_ms"].mean()
    desvs = df.groupby("modelo", observed=True)["tiempo_ms"].std()

    for i, modelo in enumerate(ORDEN_MODELOS):
        if modelo in medias:
            m_val = medias[modelo]
            s_val = desvs[modelo]
            ax.text(
                i, m_val + (df["tiempo_ms"].max() * 0.03),
                f"μ = {m_val:.1f} ms\n± {s_val:.1f}",
                ha="center", va="bottom",
                fontsize=9.5, fontweight="bold",
                color="#1e293b",
                bbox=dict(boxstyle="round,pad=0.25", facecolor="white", edgecolor="#cbd5e1", alpha=0.9)
            )

    # Elemento explicativo para los puntos atípicos (outliers)
    ax.scatter([], [], color="#475569", edgecolors="black", s=30, label="Puntos aislados = Valores atípicos (outliers por interrupciones del SO)")
    ax.legend(loc="upper left", frameon=True, fontsize=9.5)

    ax.set_title("Distribución de Tiempos de Ejecución (200 Corridas)\nSimulación de Gray-Scott (512x512, 16,000 pasos)")
    ax.set_xlabel("Modelo de Ejecución Concurrente", labelpad=10, fontweight="bold")
    ax.set_ylabel("Tiempo de Cómputo CPU (ms)", labelpad=10, fontweight="bold")
    ax.tick_params(axis="x", rotation=15)
    ax.set_ylim(500, df["tiempo_ms"].max() * 1.18)

    plt.tight_layout()
    output_file = graficas_dir / "01_tiempos_ejecucion_boxplot.png"
    plt.savefig(output_file)
    plt.close()
    print(f"    Guardado en: {output_file}")


# ==============================================================================
# GRÁFICA 2: SPEEDUP COMPARATIVO (S = T_sec / T_par)
# ==============================================================================
def graficar_speedup(df: pd.DataFrame):
    print("--> Generando: 02_speedup_comparativo.png...")
    medias = df.groupby("modelo", observed=True)["tiempo_ms"].mean()
    t_sec = medias["Secuencial"]
    speedups = t_sec / medias

    # Ordenar por speedup descendente
    speedups_sorted = speedups.sort_values(ascending=False)
    colores = [PALETA_MODELOS.get(m, "#475569") for m in speedups_sorted.index]

    fig, ax = plt.subplots(figsize=(11, 6.5))
    bars = ax.bar(
        speedups_sorted.index,
        speedups_sorted.values,
        color=colores,
        width=0.55,
        edgecolor="#1e293b",
        linewidth=1.2
    )

    # Línea de referencia base (1.0x)
    ax.axhline(1.0, color="#dc2626", linestyle="--", linewidth=1.5, label="Línea Base Secuencial (1.0x)")

    # Etiquetas de valor sobre cada barra
    for bar in bars:
        h = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            h + 0.1,
            f"{h:.2f}x",
            ha="center", va="bottom",
            fontsize=11, fontweight="bold",
            color="#0f172a"
        )

    ax.set_title("Aceleración Relativa (Speedup) vs. Secuencial\n$S = \\bar{T}_{secuencial} / \\bar{T}_{modelo}$ (200 Corridas)")
    ax.set_xlabel("Modelo de Ejecución", labelpad=10, fontweight="bold")
    ax.set_ylabel("Speedup Obtenido", labelpad=10, fontweight="bold")
    ax.set_ylim(0, max(speedups_sorted.values) * 1.18)
    ax.tick_params(axis="x", rotation=15)
    ax.legend(loc="upper right", frameon=True)

    plt.tight_layout()
    output_file = graficas_dir / "02_speedup_comparativo.png"
    plt.savefig(output_file)
    plt.close()
    print(f"    Guardado en: {output_file}")


# ==============================================================================
# GRÁFICA 3: EFICIENCIA PARALELA (E = S / p * 100%)
# ==============================================================================
def graficar_eficiencia(df: pd.DataFrame):
    print("--> Generando: 03_eficiencia_paralela.png...")
    medias = df.groupby("modelo", observed=True)["tiempo_ms"].mean()
    t_sec = medias["Secuencial"]
    speedups = t_sec / medias

    # Mapeo de hilos por modelo
    hilos_map = {
        "Secuencial": 1,
        "CMP": 8,
        "SMT (HW Real)": 16,
        "SMT (Sobre-suscripción SW)": 16,
        "Coarse-Grained": 8,
        "Fine-Grained (Cooperativo)": 8,
        "Fine-Grained (Paralelo Cíclico)": 8
    }

    # Evaluar la eficiencia paralela exclusivamente en los modelos concurrentes (p >= 8)
    modelos_paralelos = [m for m in ORDEN_MODELOS if m != "Secuencial"]
    eficiencias = {}
    for m in modelos_paralelos:
        p = hilos_map[m]
        eficiencias[m] = (speedups[m] / p) * 100.0

    ef_series = pd.Series(eficiencias).sort_values(ascending=False)
    colores = [PALETA_MODELOS.get(m, "#475569") for m in ef_series.index]

    fig, ax = plt.subplots(figsize=(11, 6.5))
    bars = ax.bar(
        range(len(ef_series)),
        ef_series.values,
        color=colores,
        width=0.55,
        edgecolor="#1e293b",
        linewidth=1.2
    )

    ax.set_xticks(range(len(ef_series)))
    ax.set_xticklabels(ef_series.index)

    ax.axhline(100.0, color="#16a34a", linestyle=":", linewidth=1.5, label="Eficiencia Ideal (100%)")
    ax.axhline(50.0, color="#eab308", linestyle="--", linewidth=1.2, label="Umbral 50% de Eficiencia")

    for bar, m in zip(bars, ef_series.index):
        h = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            h + 1.5,
            f"{h:.1f}%",
            ha="center", va="bottom",
            fontsize=11, fontweight="bold",
            color="#0f172a"
        )

    ax.set_title("Eficiencia de los Modelos Paralelos y Concurrentes\n$E = (\\text{Speedup} / p) \\times 100\\%$ (Normalizado vs. Secuencial $p=1$)")
    ax.set_xlabel("Modelo de Ejecución Concurrente", labelpad=10, fontweight="bold")
    ax.set_ylabel("Eficiencia Paralela (%)", labelpad=10, fontweight="bold")
    ax.set_ylim(0, 115)
    ax.tick_params(axis="x", rotation=15)
    ax.legend(loc="upper right", frameon=True)

    # Cuadro explicativo de diseño
    ax.text(
        0.03, 0.82,
        "Nota Arquitectónica:\n"
        "• CMP lidera con 66.9% de aprovechamiento efectivo por núcleo físico.\n"
        "• SMT duplica los hilos lógicos compartiendo el pipeline, resultando en 34.6% por hilo.",
        transform=ax.transAxes,
        fontsize=9.5,
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#f8fafc", edgecolor="#cbd5e1", alpha=0.95)
    )

    plt.tight_layout()
    output_file = graficas_dir / "03_eficiencia_paralela.png"
    plt.savefig(output_file)
    plt.close()
    print(f"    Guardado en: {output_file}")


# ==============================================================================
# GRÁFICA 4: COMPARATIVAS MICROARQUITECTÓNICAS FOCALIZADAS
# ==============================================================================
def graficar_comparativas_microarquitectonicas(df: pd.DataFrame):
    print("--> Generando: 04_comparativas_microarquitectonicas.png (3 Paneles)...")
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(18.5, 6.2))

    # -------------------------------------------------------------------------
    # PANEL 1: Aislamiento SMT (Hardware vs Software - 16 Hilos)
    # -------------------------------------------------------------------------
    smt_modelos = ["SMT (HW Real)", "SMT (Sobre-suscripción SW)"]
    df_smt = df[df["modelo"].isin(smt_modelos)]
    medias_smt = df_smt.groupby("modelo", observed=True)["tiempo_ms"].mean()
    desvs_smt = df_smt.groupby("modelo", observed=True)["tiempo_ms"].std()

    colores_smt = [PALETA_MODELOS[m] for m in smt_modelos]
    bars1 = ax1.bar(
        ["SMT Hardware\n(16 hilos HW)", "SMT Software\n(16 hilos / SMT OFF)"],
        [medias_smt["SMT (HW Real)"], medias_smt["SMT (Sobre-suscripción SW)"]],
        yerr=[desvs_smt["SMT (HW Real)"], desvs_smt["SMT (Sobre-suscripción SW)"]],
        capsize=6,
        color=colores_smt,
        edgecolor="#1e293b",
        width=0.48
    )

    diff_smt = medias_smt["SMT (Sobre-suscripción SW)"] - medias_smt["SMT (HW Real)"]
    pct_smt = (diff_smt / medias_smt["SMT (HW Real)"]) * 100.0

    for bar, val in zip(bars1, [medias_smt["SMT (HW Real)"], medias_smt["SMT (Sobre-suscripción SW)"]]):
        ax1.text(
            bar.get_x() + bar.get_width() / 2.0,
            val * 0.45,
            f"{val:.1f} ms",
            ha="center", va="center",
            fontsize=11.5, fontweight="bold",
            color="white"
        )

    ax1.set_title("A. Aislamiento SMT: HW vs. SW\n(16 Hilos Concurrentes)", fontsize=13)
    ax1.set_ylabel("Tiempo Medio de Cómputo (ms)", fontweight="bold")
    ax1.set_ylim(0, 1150)
    ax1.text(
        0.5, 0.88,
        f"Penalización Time-Slicing en SO:\n+{diff_smt:.1f} ms (+{pct_smt:.1f}%) más lento",
        ha="center", va="center", transform=ax1.transAxes,
        fontsize=9.5, fontweight="bold", color="#991b1b",
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#fee2e2", edgecolor="#f87171")
    )

    # -------------------------------------------------------------------------
    # PANEL 2: Aislamiento Fine-Grained (Hardware Cíclico vs Software Cooperativo)
    # -------------------------------------------------------------------------
    fine_modelos = ["Fine-Grained (Paralelo Cíclico)", "Fine-Grained (Cooperativo)"]
    df_fine = df[df["modelo"].isin(fine_modelos)]
    medias_fine = df_fine.groupby("modelo", observed=True)["tiempo_ms"].mean()
    desvs_fine = df_fine.groupby("modelo", observed=True)["tiempo_ms"].std()

    colores_fine = [PALETA_MODELOS[m] for m in fine_modelos]
    bars2 = ax2.bar(
        ["Fine-Grained Cíclico\n(8 hilos en HW)", "Fine-Grained Cooperativo\n(1 core / 8.16M switches)"],
        [medias_fine["Fine-Grained (Paralelo Cíclico)"], medias_fine["Fine-Grained (Cooperativo)"]],
        yerr=[desvs_fine["Fine-Grained (Paralelo Cíclico)"], desvs_fine["Fine-Grained (Cooperativo)"]],
        capsize=6,
        color=colores_fine,
        edgecolor="#1e293b",
        width=0.48
    )

    diff_fine = medias_fine["Fine-Grained (Cooperativo)"] - medias_fine["Fine-Grained (Paralelo Cíclico)"]
    pct_fine = (diff_fine / medias_fine["Fine-Grained (Paralelo Cíclico)"]) * 100.0

    for bar, val in zip(bars2, [medias_fine["Fine-Grained (Paralelo Cíclico)"], medias_fine["Fine-Grained (Cooperativo)"]]):
        ax2.text(
            bar.get_x() + bar.get_width() / 2.0,
            val * 0.45,
            f"{val:.1f} ms",
            ha="center", va="center",
            fontsize=11.5, fontweight="bold",
            color="white"
        )

    ax2.set_title("B. Fine-Grained: HW vs. Emulación SW\n(8 Hilos Lógicos / Q=1 fila)", fontsize=13)
    ax2.set_ylabel("Tiempo Medio de Cómputo (ms)", fontweight="bold")
    ax2.set_ylim(0, 4700)
    ax2.text(
        0.5, 0.88,
        f"Penalización Simulación en SW:\n+{diff_fine:.1f} ms (+{pct_fine:.1f}%) más lento",
        ha="center", va="center", transform=ax2.transAxes,
        fontsize=9.5, fontweight="bold", color="#991b1b",
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#fee2e2", edgecolor="#f87171")
    )

    # -------------------------------------------------------------------------
    # PANEL 3: Efecto de Localidad de Caché (Coarse vs Fine Cíclico - Ambos con 8 hilos)
    # -------------------------------------------------------------------------
    gran_modelos = ["Coarse-Grained", "Fine-Grained (Paralelo Cíclico)"]
    df_gran = df[df["modelo"].isin(gran_modelos)]
    medias_gran = df_gran.groupby("modelo", observed=True)["tiempo_ms"].mean()
    desvs_gran = df_gran.groupby("modelo", observed=True)["tiempo_ms"].std()

    colores_gran = [PALETA_MODELOS[m] for m in gran_modelos]
    bars3 = ax3.bar(
        ["Coarse-Grained\n(Bloques contiguos)", "Fine-Grained Cíclico\n(Intercalado / Q=1 fila)"],
        [medias_gran["Coarse-Grained"], medias_gran["Fine-Grained (Paralelo Cíclico)"]],
        yerr=[desvs_gran["Coarse-Grained"], desvs_gran["Fine-Grained (Paralelo Cíclico)"]],
        capsize=6,
        color=colores_gran,
        edgecolor="#1e293b",
        width=0.48
    )

    diff_gran = medias_gran["Fine-Grained (Paralelo Cíclico)"] - medias_gran["Coarse-Grained"]
    pct_gran = (diff_gran / medias_gran["Coarse-Grained"]) * 100.0

    for bar, val in zip(bars3, [medias_gran["Coarse-Grained"], medias_gran["Fine-Grained (Paralelo Cíclico)"]]):
        ax3.text(
            bar.get_x() + bar.get_width() / 2.0,
            val * 0.45,
            f"{val:.1f} ms",
            ha="center", va="center",
            fontsize=11.5, fontweight="bold",
            color="white"
        )

    ax3.set_title("C. Efecto de Localidad de Caché\n(Ambos con 8 Hilos de CPU)", fontsize=13)
    ax3.set_ylabel("Tiempo Medio de Cómputo (ms)", fontweight="bold")
    ax3.set_ylim(0, 1350)
    ax3.text(
        0.5, 0.88,
        f"Penalización Pérdida Localidad:\n+{diff_gran:.1f} ms (+{pct_gran:.1f}%) más lento",
        ha="center", va="center", transform=ax3.transAxes,
        fontsize=9.5, fontweight="bold", color="#991b1b",
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#fee2e2", edgecolor="#f87171")
    )

    plt.tight_layout()
    output_file = graficas_dir / "04_comparativas_microarquitectonicas.png"
    plt.savefig(output_file)
    plt.close()
    print(f"    Guardado en: {output_file}")


# ==============================================================================
# GRÁFICA 5: DASHBOARD CONSOLIDADO GENERAL (2x2)
# ==============================================================================
def graficar_dashboard_consolidado(df: pd.DataFrame):
    print("--> Generando: 05_dashboard_consolidado.png...")
    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    medias = df.groupby("modelo", observed=True)["tiempo_ms"].mean()
    desvs = df.groupby("modelo", observed=True)["tiempo_ms"].std()
    t_sec = medias["Secuencial"]
    speedups = t_sec / medias

    hilos_map = {
        "Secuencial": 1,
        "CMP": 8,
        "SMT (HW Real)": 16,
        "SMT (Sobre-suscripción SW)": 16,
        "Coarse-Grained": 8,
        "Fine-Grained (Cooperativo)": 8,
        "Fine-Grained (Paralelo Cíclico)": 8
    }
    eficiencias = {m: (speedups[m] / hilos_map[m]) * 100.0 for m in ORDEN_MODELOS}

    # Helper para formatear nombres en ejes compactos
    def formatear_etiqueta(nombre):
        return nombre.replace(" (", "\n(")

    # Cuadrante (0,0): Tiempos medios con barras de error
    ax_t = axs[0, 0]
    colores = [PALETA_MODELOS[m] for m in ORDEN_MODELOS]
    bars_t = ax_t.bar(
        range(len(ORDEN_MODELOS)),
        [medias[m] for m in ORDEN_MODELOS],
        yerr=[desvs[m] for m in ORDEN_MODELOS],
        capsize=4,
        color=colores,
        edgecolor="#1e293b",
        width=0.6
    )
    ax_t.set_title("A. Tiempo Medio de Cómputo CPU (ms)")
    ax_t.set_ylabel("Milisegundos (ms)", fontweight="bold")
    ax_t.set_xticks(range(len(ORDEN_MODELOS)))
    ax_t.set_xticklabels([formatear_etiqueta(m) for m in ORDEN_MODELOS], rotation=25, ha="right", fontsize=9)
    for bar in bars_t:
        h = bar.get_height()
        ax_t.text(bar.get_x() + bar.get_width() / 2.0, h + 80, f"{h:.0f} ms", ha="center", va="bottom", fontsize=8.5, fontweight="bold")

    # Cuadrante (0,1): Speedup
    ax_s = axs[0, 1]
    sp_series = pd.Series({m: speedups[m] for m in ORDEN_MODELOS}).sort_values(ascending=False)
    colores_sp = [PALETA_MODELOS[m] for m in sp_series.index]
    bars_s = ax_s.bar(
        range(len(sp_series)),
        sp_series.values,
        color=colores_sp,
        edgecolor="#1e293b",
        width=0.6
    )
    ax_s.axhline(1.0, color="#dc2626", linestyle="--", linewidth=1.2)
    ax_s.set_title("B. Speedup Relativo vs. Secuencial")
    ax_s.set_ylabel("Multiplicador (x)", fontweight="bold")
    ax_s.set_xticks(range(len(sp_series)))
    ax_s.set_xticklabels([formatear_etiqueta(m) for m in sp_series.index], rotation=25, ha="right", fontsize=9)
    for bar in bars_s:
        h = bar.get_height()
        ax_s.text(bar.get_x() + bar.get_width() / 2.0, h + 0.1, f"{h:.2f}x", ha="center", va="bottom", fontsize=9, fontweight="bold")

    modelos_paralelos = [m for m in ORDEN_MODELOS if m != "Secuencial"]
    eficiencias = {m: (speedups[m] / hilos_map[m]) * 100.0 for m in modelos_paralelos}

    # Cuadrante (1,0): Eficiencia Paralela
    ax_e = axs[1, 0]
    ef_series = pd.Series(eficiencias).sort_values(ascending=False)
    colores_ef = [PALETA_MODELOS[m] for m in ef_series.index]
    bars_e = ax_e.bar(
        range(len(ef_series)),
        ef_series.values,
        color=colores_ef,
        edgecolor="#1e293b",
        width=0.6
    )
    ax_e.axhline(100.0, color="#16a34a", linestyle=":", linewidth=1.2)
    ax_e.set_title("C. Eficiencia de Modelos Paralelos (%)")
    ax_e.set_ylabel("Porcentaje (%)", fontweight="bold")
    ax_e.set_xticks(range(len(ef_series)))
    etiquetas_ef = [formatear_etiqueta(m) for m in ef_series.index]
    ax_e.set_xticklabels(etiquetas_ef, rotation=25, ha="right", fontsize=9)
    for bar in bars_e:
        h = bar.get_height()
        ax_e.text(bar.get_x() + bar.get_width() / 2.0, h + 1.5, f"{h:.1f}%", ha="center", va="bottom", fontsize=8.5, fontweight="bold")

    # Cuadrante (1,1): Variabilidad a lo largo de las 200 corridas
    ax_v = axs[1, 1]
    for m in ["SMT (HW Real)", "CMP", "Coarse-Grained", "SMT (Sobre-suscripción SW)", "Fine-Grained (Paralelo Cíclico)"]:
        df_sub = df[df["modelo"] == m]
        ax_v.plot(df_sub["run_id"].values, df_sub["tiempo_ms"].values, label=m, color=PALETA_MODELOS[m], alpha=0.85, linewidth=1.2)
    ax_v.set_title("D. Estabilidad Temporal a lo largo de 200 Corridas")
    ax_v.set_xlabel("Número de Corrida (Run ID)", fontweight="bold")
    ax_v.set_ylabel("Tiempo CPU (ms)", fontweight="bold")
    ax_v.legend(loc="upper right", fontsize=8.5, frameon=True)

    fig.suptitle("Dashboard Consolidado de Rendimiento Concurrente — Gray-Scott\n(Evaluación Experimental de 1,400 Corridas Totales)", y=0.995)
    plt.tight_layout()
    output_file = graficas_dir / "05_dashboard_consolidado.png"
    plt.savefig(output_file)
    plt.close()
    print(f"    Guardado en: {output_file}")


def main():
    print("==============================================================================")
    print("       GENERADOR DE GRÁFICOS EXPERIMENTALES DE BENCHMARKING (CE 4302)         ")
    print("==============================================================================")
    df = cargar_datos()
    print(f"--> Datos cargados exitosamente: {len(df)} registros totales.")
    print(f"--> Carpeta destino: {graficas_dir}\n")

    graficar_boxplot(df)
    graficar_speedup(df)
    graficar_eficiencia(df)
    graficar_comparativas_microarquitectonicas(df)
    graficar_dashboard_consolidado(df)

    # Perfilado de hardware (perf) si existe el CSV
    csv_perf = metricas_dir / "perf_hardware_counters.csv"
    if csv_perf.exists():
        try:
            from profile_perf import graficar_perfilado_hardware
            df_perf = pd.read_csv(csv_perf)
            output_perf = graficas_dir / "06_perfilado_hardware_perf.png"
            graficar_perfilado_hardware(df_perf, output_perf)
        except Exception as e:
            print(f"[AVISO] No se pudo procesar gráfica de perf: {e}")

    # Escalabilidad de hilos si existe el CSV
    csv_scalability = metricas_dir / "escalabilidad_hilos.csv"
    if csv_scalability.exists():
        try:
            from scalability import graficar_escalabilidad
            df_scale = pd.read_csv(csv_scalability)
            output_scale = graficas_dir / "07_escalabilidad_hilos.png"
            t_seq_ref = df[df["modelo"] == "Secuencial"]["tiempo_ms"].mean() if not df[df["modelo"] == "Secuencial"].empty else 3929.0
            graficar_escalabilidad(df_scale, t_seq_ref, output_scale)
        except Exception as e:
            print(f"[AVISO] No se pudo procesar gráfica de escalabilidad: {e}")

    print("\n==============================================================================")
    print(" ¡Todas las gráficas fueron generadas con éxito en alta resolución (300 DPI)! ")
    print("==============================================================================\n")


if __name__ == "__main__":
    main()
