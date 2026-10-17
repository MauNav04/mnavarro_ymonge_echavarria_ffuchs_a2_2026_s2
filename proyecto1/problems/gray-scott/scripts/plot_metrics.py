#!/usr/bin/env python3
"""
==============================================================================
Visualización Científica y Generación de Gráficas de Rendimiento
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Problema: Simulación de Gray-Scott Multihilo y Multiprocesamiento
==============================================================================

Genera figuras científicas en alta resolución (300 DPI) a partir de los datos
experimentales recolectados por benchmark.py:
  01_tiempos_ejecucion_boxplot.png  - Boxplot de distribución de tiempos (ms) por modelo.
  02_speedup_comparativo.png        - Gráfico de barras de Speedup con IC 95%.
  03_eficiencia_paralela.png        - Eficiencia Paralela (%) con línea ideal (100%).
  04_escalabilidad_hilos.png        - Escalabilidad Fuerte (Strong Scaling) vs Ley de Amdahl.
  08_dashboard_consolidado.png      - Dashboard científico consolidado (2x2 multipanel).
  09_throughput_mupdates.png        - Throughput en Millones de Celdas / segundo (MUpdates/s).

(Las figuras 05, 06, 07 y 10 de perfilado microarquitectónico son generadas por profile_perf.py).
==============================================================================
"""

import os
import sys

# Configurar directorio de matplotlib para evitar advertencias de permisos en entornos Linux
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_gray_scott"

import csv
import math
import argparse
import numpy as np
import matplotlib
matplotlib.use("Agg")  # Backend no interactivo
import matplotlib.pyplot as plt

# ==============================================================================
# CONFIGURACIÓN ESTÉTICA Y PALETA CIENTÍFICA
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
    "Secuencial": "#64748b",                          # Slate Gray
    "CMP": "#1f77b4",                                 # Azul institucional
    "SMT (HW Real)": "#059669",                       # Verde esmeralda
    "SMT (Sobre-suscripción SW)": "#d97706",          # Ámbar / Naranja
    "Coarse-Grained": "#7c3aed",                      # Púrpura
    "Fine-Grained (Paralelo Cíclico)": "#0891b2",      # Teal / Cian
    "Fine-Grained (Cooperativo)": "#e11d48",          # Carmesí
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

HILOS_POR_DEFECTO = {
    "Secuencial": 1,
    "CMP": 8,
    "SMT (HW Real)": 16,
    "SMT (Sobre-suscripción SW)": 16,
    "Coarse-Grained": 8,
    "Fine-Grained (Paralelo Cíclico)": 8,
    "Fine-Grained (Cooperativo)": 8,
}


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


def cargar_o_calcular_resumen(summary_path, datos_raw):
    """Carga el resumen estadístico desde CSV o lo calcula a partir de los datos crudos."""
    resumen = {}
    if os.path.exists(summary_path):
        with open(summary_path, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                modelo = row["modelo"]
                sp_clean = row["speedup"].replace("x", "") if "x" in row["speedup"] else row["speedup"]
                eff_clean = row["eficiencia"].replace("%", "") if "%" in row["eficiencia"] else row["eficiencia"]
                speedup_val = float(sp_clean) if sp_clean != "N/A" else 1.0
                eficiencia_val = float(eff_clean) if eff_clean != "N/A" else 100.0

                resumen[modelo] = {
                    "modelo": modelo,
                    "modo": row.get("modo", "default"),
                    "hilos": int(row.get("hilos", HILOS_POR_DEFECTO.get(modelo, 8))),
                    "corridas": int(row.get("corridas", len(datos_raw.get(modelo, [])))),
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

    # Si no existe, calcular al vuelo a partir de datos_raw
    print("  [!] Calculando resumen estadístico a partir de los datos crudos...")
    t_seq_mean = np.mean(datos_raw["Secuencial"]) if "Secuencial" in datos_raw else None

    for m, valores in datos_raw.items():
        n = len(valores)
        media = float(np.mean(valores))
        std = float(np.std(valores, ddof=1)) if n > 1 else 0.0
        me = 1.95996 * (std / math.sqrt(n)) if n > 1 else 0.0
        hilos = HILOS_POR_DEFECTO.get(m, 8)
        sp = (t_seq_mean / media) if t_seq_mean and media > 0 else 1.0
        eff = (sp / hilos) * 100.0 if hilos > 0 else 100.0

        resumen[m] = {
            "modelo": m,
            "modo": "calculado",
            "hilos": hilos,
            "corridas": n,
            "media_ms": media,
            "std_dev_ms": std,
            "margen_error_ms": me,
            "ci_95_inf_ms": media - me,
            "ci_95_sup_ms": media + me,
            "min_ms": float(np.min(valores)),
            "max_ms": float(np.max(valores)),
            "mediana_ms": float(np.median(valores)),
            "speedup": sp,
            "eficiencia": eff,
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
    ax.set_title("Distribución Experimental de Tiempos de Cómputo por Modelo Microarquitectónico - Gray-Scott")

    # Etiquetas con la mediana exacta encima de cada caja
    max_global = max([max(v) for v in data]) if data else 1000
    for i, m in enumerate(modelos):
        mediana = resumen[m]["mediana_ms"] if m in resumen else float(np.median(datos_raw[m]))
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

    ax.set_ylim(0, max_global * 1.15)

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
    ax.set_title("Aceleración (Speedup) Relativo al Modelo Secuencial con IC 95% - Gray-Scott")

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
    ax.set_title("Eficiencia Paralela por Modelo Microarquitectónico - Gray-Scott")

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
# 4. ESCALABILIDAD FUERTE (STRONG SCALING) VS LEY DE AMDAHL
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
            h_key = "threads" if "threads" in row else "hilos"
            hilos = int(row[h_key])
            sp_str = row["speedup"].replace("x", "") if "x" in row["speedup"] else row["speedup"]
            speedup = float(sp_str)
            eff_key = "eficiencia_pct" if "eficiencia_pct" in row else "eficiencia"
            eff_str = row[eff_key].replace("%", "") if "%" in row[eff_key] else row[eff_key]
            eficiencia = float(eff_str)
            if modelo not in datos_scaling:
                datos_scaling[modelo] = []
            datos_scaling[modelo].append((hilos, speedup, eficiencia))

    fig, ax = plt.subplots(figsize=(10.5, 6.5))

    todos_hilos = sorted(list({h for m in datos_scaling.values() for h, _, _ in m}))
    if not todos_hilos:
        return False

    max_p = max(todos_hilos)
    p_ideal = np.linspace(1, max_p, 100)

    # 1. Línea Ideal Lineal
    ax.plot(p_ideal, p_ideal, "k--", linewidth=1.5, alpha=0.7, label="Ideal Lineal ($S(p) = p$)")

    # 2. Curvas teóricas de Amdahl
    for p_frac in [0.90, 0.95, 0.98]:
        s_amdahl = 1.0 / ((1.0 - p_frac) + (p_frac / p_ideal))
        ax.plot(p_ideal, s_amdahl, ":", linewidth=1.3, alpha=0.8, label=f"Amdahl ({int(p_frac*100)}% paralelo)")

    # 3. Curvas experimentales por modelo
    marcadores = {
        "Secuencial": "s",
        "CMP": "o",
        "SMT (HW Real)": "^",
        "SMT": "^",
        "Coarse-Grained": "D",
        "Fine-Grained (Paralelo Cíclico)": "v",
    }

    orden_plot = ["Secuencial", "CMP", "SMT (HW Real)", "Coarse-Grained", "Fine-Grained (Paralelo Cíclico)"]
    puntos_por_hilo = {h: [] for h in todos_hilos}

    for mod_nom in orden_plot:
        if mod_nom not in datos_scaling:
            continue
        puntos = datos_scaling[mod_nom]
        puntos_sorted = sorted(puntos, key=lambda x: x[0])
        hilos = [p[0] for p in puntos_sorted]
        sps = [p[1] for p in puntos_sorted]
        m = marcadores.get(mod_nom, "o")
        c = PALETA_MODELOS.get(mod_nom, "#333333")

        ax.plot(hilos, sps, marker=m, markersize=8, linewidth=2.2, color=c, label=f"Medido: {mod_nom}")
        for h, sp in zip(hilos, sps):
            puntos_por_hilo[h].append({"mod": mod_nom, "h": h, "sp": sp, "color": c})

    # A. Para h = 1: Un único indicador limpio para el baseline (evita superposición de 5 etiquetas)
    ax.annotate(
        "1.00x",
        xy=(1, 1.0),
        xytext=(0, 9),
        textcoords="offset points",
        ha="center",
        va="bottom",
        fontsize=8.5,
        fontweight="bold",
        color="#333333",
        bbox=dict(boxstyle="round,pad=0.2", facecolor="white", edgecolor="#888888", alpha=0.9, linewidth=0.8)
    )

    # B. Para h intermedios (p = 2, 4, 8): Dispersión inteligente anti-colisiones
    for h in [2, 4, 8]:
        if h not in puntos_por_hilo or not puntos_por_hilo[h]:
            continue
        pts = sorted(puntos_por_hilo[h], key=lambda x: x["sp"])
        n_pts = len(pts)

        if h == 2:
            # En p=2 el espacio a la izquierda es reducido. Dispersamos arriba, abajo y derecha
            pos_h = [
                {"offset": (0, -11), "ha": "center", "va": "top"},       # mínimo -> abajo
                {"offset": (14, -5), "ha": "left",   "va": "center"},    # 2do -> derecha-abajo
                {"offset": (14, 5),  "ha": "left",   "va": "center"},    # 3ro -> derecha-arriba
                {"offset": (0, 9),   "ha": "center", "va": "bottom"}     # máximo -> arriba
            ]
        elif n_pts == 4:
            # En p=4 y p=8 hay amplio espacio horizontal. Dispersión cardinal (N, S, W, E)
            pos_h = [
                {"offset": (0, -11), "ha": "center", "va": "top"},       # mínimo -> abajo
                {"offset": (14, 0),  "ha": "left",   "va": "center"},    # 2do -> derecha
                {"offset": (-14, 0), "ha": "right",  "va": "center"},    # 3ro -> izquierda
                {"offset": (0, 9),   "ha": "center", "va": "bottom"}     # máximo -> arriba
            ]
        elif n_pts == 3:
            pos_h = [
                {"offset": (0, -11), "ha": "center", "va": "top"},
                {"offset": (14, 0),  "ha": "left",   "va": "center"},
                {"offset": (0, 9),   "ha": "center", "va": "bottom"}
            ]
        elif n_pts == 2:
            pos_h = [
                {"offset": (0, -11), "ha": "center", "va": "top"},
                {"offset": (0, 9),   "ha": "center", "va": "bottom"}
            ]
        else:
            pos_h = [{"offset": (0, 8), "ha": "center", "va": "bottom"}]

        for pt, pos in zip(pts, pos_h):
            ax.annotate(
                f"{pt['sp']:.2f}x",
                xy=(pt["h"], pt["sp"]),
                xytext=pos["offset"],
                textcoords="offset points",
                ha=pos["ha"],
                va=pos["va"],
                fontsize=8.5,
                fontweight="bold",
                color=pt["color"],
                bbox=dict(boxstyle="round,pad=0.18", facecolor="white", edgecolor=pt["color"], alpha=0.9, linewidth=0.8)
            )

    # C. Resolver colisiones en el extremo p=16 con badges destacados a la derecha
    pts_16 = sorted(puntos_por_hilo.get(max_p, []), key=lambda x: x["sp"], reverse=True)
    if pts_16:
        n_p16 = len(pts_16)
        y_pos = [p["sp"] for p in pts_16]
        min_gap = 0.55
        for i in range(1, n_p16):
            if y_pos[i-1] - y_pos[i] < min_gap:
                y_pos[i] = y_pos[i-1] - min_gap
        for i, pt in enumerate(pts_16):
            ax.annotate(
                f"{pt['sp']:.2f}x",
                xy=(pt["h"], pt["sp"]),
                xytext=(pt["h"] + 0.35, y_pos[i]),
                textcoords="data",
                ha="left",
                va="center",
                fontsize=8.5,
                fontweight="bold",
                color=pt["color"],
                bbox=dict(boxstyle="round,pad=0.2", facecolor="white", edgecolor=pt["color"], alpha=0.9, linewidth=1.0),
                arrowprops=dict(arrowstyle="-", color=pt["color"], lw=0.9, shrinkA=2, shrinkB=2)
            )

    ax.set_xlabel("Número de Hilos Concurrentes ($p$)")
    ax.set_ylabel("Speedup ($S_p$)")
    ax.set_title("Escalabilidad Fuerte (Strong Scaling) vs Ley de Amdahl - Gray-Scott")
    ax.set_xticks(todos_hilos)
    ax.set_xlim(0.8, max_p + 1.8)
    max_sp = max([sp for pts in datos_scaling.values() for _, sp, _ in pts] + [1.0])
    ax.set_ylim(0.5, max(max_p * 1.08, max_sp * 1.15))
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")
    return True


# ==============================================================================
# 8. DASHBOARD CIENTÍFICO CONSOLIDADO (2x2)
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
    axs[0, 0].set_ylim(0, max(tiempos) * 1.18)
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
    axs[0, 1].set_ylim(0, max(speedups) * 1.18)
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
    axs[1, 0].set_ylim(0, max(115.0, max(eficiencias) * 1.15))
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
    axs[1, 1].set_ylim(0, max(cv_vals) * 1.18 if max(cv_vals) > 0 else 5.0)
    for b in bars_cv:
        axs[1, 1].annotate(f"{b.get_height():.2f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    fig.suptitle("Dashboard Científico Consolidado de Rendimiento - Gray-Scott Multihilo", fontsize=16, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# 9. THROUGHPUT - MILLONES DE ACTUALIZACIONES POR SEGUNDO (MUpdates/s)
# ==============================================================================
def graficar_throughput(resumen, output_path, grid_size=512, steps=16000):
    """
    Genera gráfico de barras de Throughput en MUpdates/s.
    Celdas interiores actualizadas: (grid_size - 2)^2
    Total updates: interior_cells * steps
    Throughput (MUpdates/s) = (total_updates / 1e6) / (tiempo_ms / 1000.0)
    """
    modelos = obtener_modelos_ordenados(resumen.keys())
    interior_cells = (grid_size - 2) * (grid_size - 2)
    total_mupdates = (interior_cells * steps) / 1e6

    throughputs = [total_mupdates / (resumen[m]["media_ms"] / 1000.0) if resumen[m]["media_ms"] > 0 else 0.0 for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    fig, ax = plt.subplots(figsize=(12, 6.5))
    x_pos = np.arange(len(modelos))
    bars = ax.bar(x_pos, throughputs, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)

    ax.set_xticks(x_pos)
    etiquetas_x = [f"{m}\n({resumen[m]['hilos']} hilos)" for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")
    ax.set_ylabel("Millones de Celdas Actualizadas / s (MUpdates/s)")
    ax.set_title("Rendimiento Computacional: Throughput en Simulación Gray-Scott")

    # Línea base secuencial
    if "Secuencial" in resumen:
        seq_tp = total_mupdates / (resumen["Secuencial"]["media_ms"] / 1000.0)
        ax.axhline(seq_tp, color="#d90429", linestyle="--", linewidth=1.2, label=f"Baseline Secuencial ({seq_tp:.0f} MUpdates/s)")
        ax.legend(loc="upper left")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(
            f"{h:,.0f}".replace(",", "."),
            xy=(bar.get_x() + bar.get_width() / 2, h),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold"
        )

    ax.set_ylim(0, max(throughputs) * 1.22 if max(throughputs) > 0 else 100)
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# PUNTO DE ENTRADA PRINCIPAL
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(description="Generador de figuras científicas para Gray-Scott.")
    parser.add_argument("--benchmark-csv", default="../metricas/gray_scott_benchmark.csv",
                        help="Ruta al archivo CSV con las corridas individuales.")
    parser.add_argument("--summary-csv", default="../metricas/resumen_estadistico.csv",
                        help="Ruta al archivo CSV con el resumen estadístico consolidado.")
    parser.add_argument("--scaling-csv", default="../metricas/escalabilidad_hilos.csv",
                        help="Ruta al archivo CSV de escalabilidad fuerte (opcional).")
    parser.add_argument("--output-dir", default="../metricas/graficas",
                        help="Directorio destino para guardar las figuras generadas.")
    args = parser.parse_args()

    # Normalizar rutas relativas al script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    benchmark_csv = os.path.abspath(os.path.join(script_dir, args.benchmark_csv)) if not os.path.isabs(args.benchmark_csv) else args.benchmark_csv
    summary_csv = os.path.abspath(os.path.join(script_dir, args.summary_csv)) if not os.path.isabs(args.summary_csv) else args.summary_csv
    scaling_csv = os.path.abspath(os.path.join(script_dir, args.scaling_csv)) if not os.path.isabs(args.scaling_csv) else args.scaling_csv
    output_dir = os.path.abspath(os.path.join(script_dir, args.output_dir)) if not os.path.isabs(args.output_dir) else args.output_dir

    os.makedirs(output_dir, exist_ok=True)

    print("==============================================================================")
    print("            GENERACIÓN DE FIGURAS CIENTÍFICAS (300 DPI) - GRAY-SCOTT          ")
    print("==============================================================================")
    print(f"Directorio de salida: {output_dir}\n")

    try:
        datos_raw = cargar_datos_benchmark(benchmark_csv)
        resumen = cargar_o_calcular_resumen(summary_csv, datos_raw)
    except Exception as e:
        print(f"[ERROR] No fue posible cargar los datos requeridos: {e}", file=sys.stderr)
        sys.exit(1)

    # 1. Boxplot de Tiempos
    p1 = os.path.join(output_dir, "01_tiempos_ejecucion_boxplot.png")
    graficar_boxplot(datos_raw, resumen, p1)

    # 2. Speedup con IC 95%
    p2 = os.path.join(output_dir, "02_speedup_comparativo.png")
    graficar_speedup(resumen, p2)

    # 3. Eficiencia Paralela
    p3 = os.path.join(output_dir, "03_eficiencia_paralela.png")
    graficar_eficiencia(resumen, p3)

    # 4. Escalabilidad de Hilos (Strong Scaling vs Ley de Amdahl)
    p4 = os.path.join(output_dir, "04_escalabilidad_hilos.png")
    alt_scaling = os.path.abspath(os.path.join(script_dir, "../metricas/gray_scott_strong_scaling.csv"))
    target_scaling = scaling_csv if os.path.exists(scaling_csv) else (alt_scaling if os.path.exists(alt_scaling) else None)

    if target_scaling and os.path.exists(target_scaling):
        graficar_strong_scaling_amdahl(target_scaling, p4)
    else:
        print("  [-] Info: Archivo de Strong Scaling no detectado. Para generarlo ejecute 'make scalability'.")

    # 8. Dashboard Consolidado Ejecutivo (Multipanel 2x2)
    p8 = os.path.join(output_dir, "08_dashboard_consolidado.png")
    graficar_dashboard_consolidado(resumen, p8)

    # 9. Throughput (Millones de Celdas Actualizadas / Segundo)
    p9 = os.path.join(output_dir, "09_throughput_mupdates.png")
    graficar_throughput(resumen, p9, grid_size=512, steps=16000)

    print("\n==============================================================================")
    print(f"[OK] Generación completada con éxito. Gráficas guardadas en:\n  -> {output_dir}")
    print("==============================================================================\n")


if __name__ == "__main__":
    main()
