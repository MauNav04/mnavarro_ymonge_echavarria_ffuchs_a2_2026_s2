#!/usr/bin/env python3
"""
==============================================================================
Visualización Científica y Generación de Gráficas de Rendimiento
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Problema: Ray Tracing Multihilo y Multiprocesamiento
Autor: Emanuel Chavarría
==============================================================================

Genera figuras científicas en alta resolución (300 DPI) a partir de los datos
experimentales recolectados por benchmark.py:
  1. Boxplot de distribución de tiempos de ejecución (ms) por modelo.
  2. Gráfico de barras de Speedup con barras de error de Intervalo de Confianza (95%).
  3. Gráfico de Eficiencia Paralela (%) con línea de referencia ideal (100%).
  4. Comparativa microarquitectónica dual (SMT Real vs SW, Coarse vs Fine).
  5. Dashboard científico consolidado (2x2 paneles ejecutivos).
  6. Escalabilidad Fuerte (Strong Scaling) vs Ley de Amdahl (si existe el archivo).
"""

import os
import sys

# Configurar directorio de matplotlib para evitar advertencias de permisos en entornos Linux
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_raytracing"

import csv
import math
import argparse
import numpy as np
import matplotlib
matplotlib.use("Agg")  # Backend no interactivo para entornos headless / scripts
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, FormatStrFormatter

# ==============================================================================
# CONFIGURACIÓN ESTÉTICA Y PALETA CIENTÍFICA
# ==============================================================================
plt.rcParams.update({
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
    "Secuencial": "#5c6b73",                          # Gris pizarra
    "CMP": "#1f77b4",                                 # Azul institucional
    "SMT (HW Real)": "#2ca02c",                       # Verde esmeralda
    "SMT (Sobre-suscripción SW)": "#e377c2",          # Magenta suave
    "Coarse-Grained": "#9467bd",                      # Púrpura
    "Fine-Grained (Paralelo Cíclico)": "#17becf",      # Cian / Turquesa
    "Fine-Grained (Cooperativo)": "#d62728",          # Rojo óxido
}

ORDEN_CANONICO = [
    "Secuencial",
    "CMP",
    "SMT (HW Real)",
    "SMT (Sobre-suscripción SW)",
    "Coarse-Grained",
    "Fine-Grained (Paralelo Cíclico)",
    "Fine-Grained (Cooperativo)",
]


def cargar_datos_benchmark(csv_path):
    """Carga los datos crudos de corridas individuales."""
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"No se encontró el archivo de benchmark: {csv_path}")

    datos_por_modelo = {}
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            modelo = row["modelo"]
            tiempo_ms = float(row["tiempo_ms"])
            if modelo not in datos_por_modelo:
                datos_por_modelo[modelo] = []
            datos_por_modelo[modelo].append(tiempo_ms)

    return datos_por_modelo


def cargar_resumen_estadistico(csv_path):
    """Carga el resumen consolidado de métricas estadísticas."""
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"No se encontró el archivo de resumen: {csv_path}")

    resumen = {}
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            modelo = row["modelo"]
            speedup_val = float(row["speedup"].replace("x", "")) if row["speedup"] != "N/A" else 1.0
            eficiencia_val = float(row["eficiencia"].replace("%", "")) if row["eficiencia"] != "N/A" else 100.0

            resumen[modelo] = {
                "modelo": modelo,
                "modo": row["modo"],
                "hilos": int(row["hilos"]),
                "corridas": int(row["corridas"]),
                "media_ms": float(row["media_ms"]),
                "std_dev_ms": float(row["std_dev_ms"]),
                "margen_error_ms": float(row["margen_error_ms"]),
                "ci_95_inf_ms": float(row["ci_95_inf_ms"]),
                "ci_95_sup_ms": float(row["ci_95_sup_ms"]),
                "min_ms": float(row["min_ms"]),
                "max_ms": float(row["max_ms"]),
                "mediana_ms": float(row["mediana_ms"]),
                "speedup": speedup_val,
                "eficiencia": eficiencia_val,
            }

    return resumen


def obtener_modelos_ordenados(disponibles):
    """Retorna la lista de modelos presentes según el orden canónico."""
    ordenados = [m for m in ORDEN_CANONICO if m in disponibles]
    for m in disponibles:
        if m not in ordenados:
            ordenados.append(m)
    return ordenados


# ==============================================================================
# 1. GRÁFICA DE BOXPLOT: TIEMPOS DE EJECUCIÓN
# ==============================================================================
def graficar_boxplot(datos_raw, resumen, output_path):
    """Genera un boxplot riguroso con la distribución de latencias de cada modelo."""
    modelos = obtener_modelos_ordenados(datos_raw.keys())
    data = [datos_raw[m] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    fig, ax = plt.subplots(figsize=(12, 6.5))

    bp = ax.boxplot(
        data,
        patch_artist=True,
        notch=False,
        showmeans=True,
        meanline=True,
        boxprops=dict(linewidth=1.2),
        whiskerprops=dict(color="#2b2d42", linewidth=1.1, linestyle="--"),
        capprops=dict(color="#2b2d42", linewidth=1.2),
        medianprops=dict(color="#d90429", linewidth=2.0),
        meanprops=dict(color="#003049", linewidth=1.8, linestyle=":"),
        flierprops=dict(marker="o", markerfacecolor="#d90429", markersize=5, alpha=0.6, markeredgecolor="none")
    )

    for patch, color in zip(bp["boxes"], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.70)
        patch.set_edgecolor("#1a1a1a")

    ax.set_xticks(range(1, len(modelos) + 1))
    etiquetas_x = [f"{m}\n({resumen[m]['hilos']} hilos)" if m in resumen else m for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")

    ax.set_ylabel("Tiempo de Ejecución (ms) [Escala Lineal]")
    ax.set_title("Distribución Experimental de Tiempos de Cómputo por Modelo Microarquitectónico")

    # Etiquetas con la mediana exacta encima de cada caja
    for i, m in enumerate(modelos):
        mediana = resumen[m]["mediana_ms"] if m in resumen else np.median(datos_raw[m])
        max_val = max(datos_raw[m])
        ax.annotate(
            f"{mediana:.1f} ms",
            xy=(i + 1, max_val),
            xytext=(0, 7),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9,
            fontweight="bold",
            color="#1a1a1a"
        )

    # Leyenda indicadora
    linea_mediana = plt.Line2D([0], [0], color="#d90429", linewidth=2.0, label="Mediana")
    linea_media = plt.Line2D([0], [0], color="#003049", linewidth=1.8, linestyle=":", label="Media")
    ax.legend(handles=[linea_mediana, linea_media], loc="upper right", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 2. GRÁFICA DE SPEEDUP CON INTERVALO DE CONFIANZA 95%
# ==============================================================================
def graficar_speedup(resumen, output_path):
    """Genera gráfico de barras de Speedup con barras de error de IC 95%."""
    modelos = obtener_modelos_ordenados(resumen.keys())
    speedups = [resumen[m]["speedup"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    t_seq_mean = resumen["Secuencial"]["media_ms"] if "Secuencial" in resumen else None

    err_low = []
    err_high = []
    for m in modelos:
        if m == "Secuencial" or not t_seq_mean:
            err_low.append(0.0)
            err_high.append(0.0)
        else:
            s_actual = resumen[m]["speedup"]
            ci_inf_p = resumen[m]["ci_95_inf_ms"]
            ci_sup_p = resumen[m]["ci_95_sup_ms"]
            s_min = t_seq_mean / ci_sup_p if ci_sup_p > 0 else s_actual
            s_max = t_seq_mean / ci_inf_p if ci_inf_p > 0 else s_actual
            err_low.append(max(0.0, s_actual - s_min))
            err_high.append(max(0.0, s_max - s_actual))

    yerr = [err_low, err_high]

    fig, ax = plt.subplots(figsize=(12, 6.5))
    x_pos = np.arange(len(modelos))
    bars = ax.bar(x_pos, speedups, yerr=yerr, capsize=5, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)

    # Línea base secuencial en y = 1.0
    ax.axhline(1.0, color="#d90429", linestyle="--", linewidth=1.4, label="Línea Base Secuencial (1.0x)")

    ax.set_xticks(x_pos)
    etiquetas_x = [f"{m}\n({resumen[m]['hilos']} hilos)" for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")
    ax.set_ylabel("Speedup ($S_p = T_{sec} / T_p$) [Mayor es mejor]")
    ax.set_title("Aceleración (Speedup) Relativo al Modelo Secuencial con IC 95%")

    for i, bar in enumerate(bars):
        h = bar.get_height()
        err_h = err_high[i]
        ax.annotate(
            f"{h:.2f}x",
            xy=(bar.get_x() + bar.get_width() / 2, h + err_h),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold"
        )

    ax.set_ylim(0, max(speedups) * 1.18)
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 3. GRÁFICA DE EFICIENCIA PARALELA
# ==============================================================================
def graficar_eficiencia(resumen, output_path):
    """Genera gráfico de barras de Eficiencia Paralela (%)."""
    modelos = obtener_modelos_ordenados(resumen.keys())
    eficiencias = [resumen[m]["eficiencia"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    fig, ax = plt.subplots(figsize=(12, 6.5))
    x_pos = np.arange(len(modelos))
    bars = ax.bar(x_pos, eficiencias, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)

    # Línea ideal del 100%
    ax.axhline(100.0, color="#2ca02c", linestyle="--", linewidth=1.5, label="Eficiencia Ideal (100%)")
    # Línea de referencia al 50%
    ax.axhline(50.0, color="#d90429", linestyle=":", linewidth=1.2, label="Umbral Típico Multihilo (50%)")

    ax.set_xticks(x_pos)
    etiquetas_x = [f"{m}\n({resumen[m]['hilos']} hilos)" for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")
    ax.set_ylabel("Eficiencia Paralela ($E = S_p / p \\times 100\\%$)")
    ax.set_title("Eficiencia Paralela por Modelo Microarquitectónico")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(
            f"{h:.1f}%",
            xy=(bar.get_x() + bar.get_width() / 2, h),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold"
        )

    ax.set_ylim(0, max(115.0, max(eficiencias) * 1.15))
    ax.legend(loc="upper right", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 4. COMPARATIVAS MICROARQUITECTÓNICAS DUALES (SMT y Granularidad)
# ==============================================================================
def graficar_comparativas_microarquitectonicas(resumen, output_path):
    """Genera figura de dos paneles contrastando SMT Real vs SW y Coarse vs Fine."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))

    # Panel 1: SMT Real (HW) vs SMT Software (Sobre-suscripción)
    smt_modelos = [m for m in ["CMP", "SMT (HW Real)", "SMT (Sobre-suscripción SW)"] if m in resumen]
    if smt_modelos:
        x1 = np.arange(len(smt_modelos))
        tiempos_smt = [resumen[m]["media_ms"] for m in smt_modelos]
        err_smt = [resumen[m]["margen_error_ms"] for m in smt_modelos]
        colors_smt = [PALETA_MODELOS.get(m, "#7f7f7f") for m in smt_modelos]

        bars1 = ax1.bar(x1, tiempos_smt, yerr=err_smt, capsize=5, color=colors_smt, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)
        ax1.set_xticks(x1)
        ax1.set_xticklabels([f"{m}\n({resumen[m]['hilos']} hilos)" for m in smt_modelos], rotation=10, ha="right")
        ax1.set_ylabel("Tiempo Medio de Ejecución (ms)")
        ax1.set_title("A. Comparativa SMT: Cores Físicos vs Lógicos")

        for bar, m in zip(bars1, smt_modelos):
            h = bar.get_height()
            sp = resumen[m]["speedup"]
            ax1.annotate(
                f"{h:.1f} ms\n({sp:.2f}x)",
                xy=(bar.get_x() + bar.get_width() / 2, h),
                xytext=(0, 6),
                textcoords="offset points",
                ha="center",
                va="bottom",
                fontsize=9,
                fontweight="bold"
            )
        ax1.set_ylim(0, max(tiempos_smt) * 1.25)

    # Panel 2: Granularidad (Coarse-Grained vs Fine-Grained)
    gran_modelos = [m for m in ["Coarse-Grained", "Fine-Grained (Paralelo Cíclico)", "Fine-Grained (Cooperativo)"] if m in resumen]
    if gran_modelos:
        x2 = np.arange(len(gran_modelos))
        tiempos_gran = [resumen[m]["media_ms"] for m in gran_modelos]
        err_gran = [resumen[m]["margen_error_ms"] for m in gran_modelos]
        colors_gran = [PALETA_MODELOS.get(m, "#7f7f7f") for m in gran_modelos]

        bars2 = ax2.bar(x2, tiempos_gran, yerr=err_gran, capsize=5, color=colors_gran, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)
        ax2.set_xticks(x2)
        ax2.set_xticklabels([f"{m}\n({resumen[m]['hilos']} hilos)" for m in gran_modelos], rotation=10, ha="right")
        ax2.set_ylabel("Tiempo Medio de Ejecución (ms)")
        ax2.set_title("B. Comparativa Granularidad de Planificación")

        for bar, m in zip(bars2, gran_modelos):
            h = bar.get_height()
            sp = resumen[m]["speedup"]
            ax2.annotate(
                f"{h:.1f} ms\n({sp:.2f}x)",
                xy=(bar.get_x() + bar.get_width() / 2, h),
                xytext=(0, 6),
                textcoords="offset points",
                ha="center",
                va="bottom",
                fontsize=9,
                fontweight="bold"
            )
        ax2.set_ylim(0, max(tiempos_gran) * 1.20)

    fig.suptitle("Análisis Microarquitectónico: Efectos de SMT y Granularidad de Planificación", fontsize=15, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 5. DASHBOARD CIENTÍFICO CONSOLIDADO (2x2)
# ==============================================================================
def graficar_dashboard_consolidado(resumen, output_path):
    """Genera un dashboard 2x2 integral resumiendo todas las métricas clave."""
    modelos = obtener_modelos_ordenados(resumen.keys())
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]
    x_pos = np.arange(len(modelos))
    short_labels = [m.replace("Fine-Grained ", "FG ").replace("SMT ", "SMT ").replace("Sobre-suscripción ", "") for m in modelos]

    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    # (0, 0) Tiempo de Ejecución Medio (ms)
    tiempos = [resumen[m]["media_ms"] for m in modelos]
    err_t = [resumen[m]["margen_error_ms"] for m in modelos]
    bars_t = axs[0, 0].bar(x_pos, tiempos, yerr=err_t, capsize=4, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[0, 0].set_title("(A) Tiempo Medio de Cómputo (ms) [IC 95%]")
    axs[0, 0].set_ylabel("Tiempo (ms)")
    axs[0, 0].set_xticks(x_pos)
    axs[0, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    for b in bars_t:
        axs[0, 0].annotate(f"{b.get_height():.0f}", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (0, 1) Speedup Relativo
    speedups = [resumen[m]["speedup"] for m in modelos]
    bars_s = axs[0, 1].bar(x_pos, speedups, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[0, 1].axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="Secuencial (1.0x)")
    axs[0, 1].set_title("(B) Speedup Relativo a Secuencial ($S_p$)")
    axs[0, 1].set_ylabel("Speedup ($S_p$)")
    axs[0, 1].set_xticks(x_pos)
    axs[0, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[0, 1].legend(loc="upper left", fontsize=9)
    for b in bars_s:
        axs[0, 1].annotate(f"{b.get_height():.2f}x", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 0) Eficiencia Paralela (%)
    eficiencias = [resumen[m]["eficiencia"] for m in modelos]
    bars_e = axs[1, 0].bar(x_pos, eficiencias, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[1, 0].axhline(100.0, color="#2ca02c", linestyle="--", linewidth=1.2, label="Ideal (100%)")
    axs[1, 0].set_title("(C) Eficiencia Paralela ($E = S_p / p$)")
    axs[1, 0].set_ylabel("Eficiencia (%)")
    axs[1, 0].set_xticks(x_pos)
    axs[1, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[1, 0].legend(loc="upper right", fontsize=9)
    for b in bars_e:
        axs[1, 0].annotate(f"{b.get_height():.1f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 1) Coeficiente de Variación (CV = std_dev / mean * 100%) - Medida de Estabilidad
    cv_vals = [(resumen[m]["std_dev_ms"] / resumen[m]["media_ms"]) * 100.0 if resumen[m]["media_ms"] > 0 else 0.0 for m in modelos]
    bars_cv = axs[1, 1].bar(x_pos, cv_vals, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[1, 1].set_title("(D) Coeficiente de Variación ($CV = \\sigma / \\mu$) [Jitter / Inestabilidad]")
    axs[1, 1].set_ylabel("CV (%) [Menor es más estable]")
    axs[1, 1].set_xticks(x_pos)
    axs[1, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    for b in bars_cv:
        axs[1, 1].annotate(f"{b.get_height():.2f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    fig.suptitle("Dashboard Científico Consolidado de Rendimiento - Ray Tracing Multihilo", fontsize=16, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 6. ESCALABILIDAD FUERTE (STRONG SCALING) VS LEY DE AMDAHL
# ==============================================================================
def graficar_strong_scaling_amdahl(scaling_csv_path, output_path):
    """Genera la curva experimental de Strong Scaling comparada con la Ley de Amdahl."""
    if not os.path.exists(scaling_csv_path):
        return False

    datos_scaling = {}
    with open(scaling_csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            modelo = row["modelo"]
            hilos = int(row["hilos"])
            speedup = float(row["speedup"].replace("x", ""))
            eficiencia = float(row["eficiencia"].replace("%", ""))
            if modelo not in datos_scaling:
                datos_scaling[modelo] = []
            datos_scaling[modelo].append((hilos, speedup, eficiencia))

    fig, ax = plt.subplots(figsize=(10, 6.5))

    todos_hilos = sorted(list({h for m in datos_scaling.values() for h, _, _ in m}))
    if not todos_hilos:
        return False

    max_p = max(todos_hilos)
    p_ideal = np.linspace(1, max_p, 100)

    # 1. Línea Ideal Lineal
    ax.plot(p_ideal, p_ideal, "k--", linewidth=1.5, alpha=0.7, label="Ideal Lineal ($S(p) = p$)")

    # 2. Curvas teóricas de Amdahl para diferentes fracciones paralelizadas p_par
    for p_frac in [0.90, 0.95, 0.98]:
        s_amdahl = 1.0 / ((1.0 - p_frac) + (p_frac / p_ideal))
        ax.plot(p_ideal, s_amdahl, ":", linewidth=1.3, alpha=0.8, label=f"Amdahl ({int(p_frac*100)}% paralelo)")

    # 3. Curvas experimentales
    marcadores = {"Secuencial": "s", "CMP": "o", "SMT": "^"}
    colores = {"Secuencial": "#5c6b73", "CMP": "#1f77b4", "SMT": "#2ca02c"}

    for modelo, puntos in datos_scaling.items():
        puntos_sorted = sorted(puntos, key=lambda x: x[0])
        hilos = [p[0] for p in puntos_sorted]
        sps = [p[1] for p in puntos_sorted]
        m = marcadores.get(modelo, "o")
        c = colores.get(modelo, "#ff7f0e")

        ax.plot(hilos, sps, marker=m, markersize=8, linewidth=2.2, color=c, label=f"Medido: {modelo}")
        for h, sp in zip(hilos, sps):
            ax.annotate(f"{sp:.2f}x", xy=(h, sp), xytext=(0, 6), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    ax.set_xlabel("Número de Hilos Concurrentes ($p$)")
    ax.set_ylabel("Speedup ($S_p$)")
    ax.set_title("Escalabilidad Fuerte (Strong Scaling) vs Ley de Amdahl")
    ax.set_xticks(todos_hilos)
    ax.set_xlim(0.8, max_p + 0.8)
    ax.set_ylim(0.5, max_p * 1.08)
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")
    return True


# ==============================================================================
# PUNTO DE ENTRADA PRINCIPAL
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(description="Generador de figuras científicas para Ray Tracing.")
    parser.add_argument("--benchmark-csv", default="../metricas/raytracing_benchmark.csv",
                        help="Ruta al archivo CSV con las corridas individuales.")
    parser.add_argument("--summary-csv", default="../metricas/resumen_estadistico.csv",
                        help="Ruta al archivo CSV con el resumen estadístico consolidado.")
    parser.add_argument("--scaling-csv", default="../metricas/raytracing_strong_scaling.csv",
                        help="Ruta al archivo CSV de escalabilidad fuerte (opcional).")
    parser.add_argument("--output-dir", default="../metricas/graficas",
                        help="Directorio destino para guardar las figuras generadas.")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    print("==============================================================================")
    print("            GENERACIÓN DE FIGURAS CIENTÍFICAS (300 DPI)                       ")
    print("==============================================================================")
    print(f"Directorio de salida: {os.path.abspath(args.output_dir)}\n")

    try:
        datos_raw = cargar_datos_benchmark(args.benchmark_csv)
        resumen = cargar_resumen_estadistico(args.summary_csv)
    except Exception as e:
        print(f"[ERROR] No fue posible cargar los datos requeridos: {e}", file=sys.stderr)
        sys.exit(1)

    # 1. Boxplot
    p1 = os.path.join(args.output_dir, "01_tiempos_ejecucion_boxplot.png")
    graficar_boxplot(datos_raw, resumen, p1)

    # 2. Speedup con IC 95%
    p2 = os.path.join(args.output_dir, "02_speedup_comparativo.png")
    graficar_speedup(resumen, p2)

    # 3. Eficiencia Paralela
    p3 = os.path.join(args.output_dir, "03_eficiencia_paralela.png")
    graficar_eficiencia(resumen, p3)

    # 4. Comparativas Microarquitectónicas
    p4 = os.path.join(args.output_dir, "04_comparativas_microarquitectonicas.png")
    graficar_comparativas_microarquitectonicas(resumen, p4)

    # 5. Dashboard Consolidado
    p5 = os.path.join(args.output_dir, "05_dashboard_consolidado.png")
    graficar_dashboard_consolidado(resumen, p5)

    # 6. Strong Scaling (si existe)
    p6 = os.path.join(args.output_dir, "06_strong_scaling_amdahl.png")
    if os.path.exists(args.scaling_csv):
        graficar_strong_scaling_amdahl(args.scaling_csv, p6)
    else:
        print("  [-] Info: Archivo de Strong Scaling no detectado. Para generarlo ejecute 'make benchmark_scaling'.")

    print("\n==============================================================================")
    print(f"[OK] Generación completada con éxito. Gráficas guardadas en:\n  -> {os.path.abspath(args.output_dir)}")
    print("==============================================================================\n")


if __name__ == "__main__":
    main()
