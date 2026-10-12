#!/usr/bin/env python3
"""
==============================================================================
Perfilado Microarquitectónico de Hardware con Linux perf
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Problema: Ray Tracing Multihilo y Multiprocesamiento
Autor: Emanuel Chavarría
==============================================================================

Mide contadores de hardware del procesador (PMU) y del sistema operativo:
  - Ciclos de CPU (cycles)
  - Instrucciones retiradas (instructions) -> IPC (Instructions Per Cycle)
  - L1 Data Cache (loads, misses, miss rate %)
  - Last-Level Cache / Cache General (references, misses, miss rate %)
  - Predictor de saltos (branches, branch misses, miss rate %)
  - Cambios de contexto del SO (context-switches)
  - Migraciones de CPU (cpu-migrations) y Fallos de página (page-faults)

Genera:
  - Archivo CSV consolidado: metricas/raytracing_perf_metrics.csv
  - Gráficas científicas a 300 DPI:
      07_perf_ipc_comparativo.png
      08_perf_cache_miss_rates.png
      09_perf_context_switches.png
      10_perf_resumen_microarquitectura.png
"""

import os
import sys

# Evitar advertencias de permisos en Linux para matplotlib
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_raytracing"

import csv
import math
import shutil
import argparse
import subprocess
import statistics
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ==============================================================================
# CONFIGURACIÓN ESTÉTICA
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
    "Secuencial": "#5c6b73",
    "CMP": "#1f77b4",
    "SMT (HW Real)": "#2ca02c",
    "SMT (Sobre-suscripción SW)": "#e377c2",
    "Coarse-Grained": "#9467bd",
    "Fine-Grained (Paralelo Cíclico)": "#17becf",
    "Fine-Grained (Cooperativo)": "#d62728",
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

# Lista de eventos PMU a consultar
EVENTOS_PERF = [
    "cycles",
    "instructions",
    "cache-references",
    "cache-misses",
    "L1-dcache-loads",
    "L1-dcache-load-misses",
    "LLC-loads",
    "LLC-load-misses",
    "branches",
    "branch-misses",
    "context-switches",
    "cpu-migrations",
    "page-faults",
]

CATALOGO_MODELOS = {
    "seq": {
        "nombre": "Secuencial",
        "modo": "baseline",
        "hilos": 1,
        "binario": "./raytracing_seq",
        "args_extra": [],
    },
    "cmp": {
        "nombre": "CMP",
        "modo": "dedicated_cores",
        "hilos": 8,
        "binario": "./raytracing_cmp",
        "args_extra": ["--threads", "8"],
    },
    "smt_hw": {
        "nombre": "SMT (HW Real)",
        "modo": "hardware_smt",
        "hilos": 16,
        "binario": "./raytracing_smt",
        "args_extra": ["--mode", "hw", "--threads", "16"],
    },
    "smt_sw": {
        "nombre": "SMT (Sobre-suscripción SW)",
        "modo": "software_oversubscription",
        "hilos": 16,
        "binario": "./raytracing_smt",
        "args_extra": ["--mode", "sw", "--threads", "16"],
    },
    "coarse": {
        "nombre": "Coarse-Grained",
        "modo": "burst_block",
        "hilos": 8,
        "binario": "./raytracing_coarse",
        "args_extra": ["--threads", "8"],
    },
    "fine_parallel": {
        "nombre": "Fine-Grained (Paralelo Cíclico)",
        "modo": "cyclic_threads",
        "hilos": 8,
        "binario": "./raytracing_fine",
        "args_extra": ["--mode", "parallel", "--threads", "8", "--quantum", "1"],
    },
    "fine_coop": {
        "nombre": "Fine-Grained (Cooperativo)",
        "modo": "cooperative_quantum",
        "hilos": 8,
        "binario": "./raytracing_fine",
        "args_extra": ["--mode", "cooperative", "--threads", "8", "--quantum", "1"],
    },
}


def verificar_acceso_perf():
    """
    Verifica si 'perf' puede acceder a los contadores de rendimiento.
    Retorna (disponible: bool, usar_sudo: bool, mensaje_error: str)
    """
    if not shutil.which("perf"):
        return False, False, "La herramienta 'perf' de Linux no está instalada en el sistema."

    # Prueba directa sin sudo
    try:
        proc = subprocess.run(
            ["perf", "stat", "-x,", "-e", "cycles", "--", "true"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5
        )
        if proc.returncode == 0:
            return True, False, ""
    except Exception:
        pass

    # Prueba con sudo no interactivo (-n)
    try:
        proc = subprocess.run(
            ["sudo", "-n", "perf", "stat", "-x,", "-e", "cycles", "--", "true"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5
        )
        if proc.returncode == 0:
            return True, True, ""
    except Exception:
        pass

    # Si ambos fallaron, verificar paranoid
    msg = (
        "Acceso limitado a contadores de rendimiento (/proc/sys/kernel/perf_event_paranoid).\n"
        "Para habilitar el perfilador en su máquina, ejecute una vez en su terminal:\n\n"
        "    sudo sysctl -w kernel.perf_event_paranoid=-1\n\n"
        "O bien ejecute la regla de Make:\n"
        "    make setup_perf\n"
    )
    return False, False, msg


def ejecutar_perf_stat(comando, cwd, usar_sudo=False):
    """
    Ejecuta el comando bajo perf stat con formato CSV (-x,).
    Retorna un diccionario {nombre_evento: valor_float}.
    """
    eventos_str = ",".join(EVENTOS_PERF)
    perf_cmd = ["perf", "stat", "-x,", "-e", eventos_str, "--"] + comando
    if usar_sudo:
        perf_cmd = ["sudo", "-n"] + perf_cmd

    proc = subprocess.run(
        perf_cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    if proc.returncode != 0:
        raise RuntimeError(f"Error al ejecutar perf stat ({proc.returncode}):\n{proc.stderr}")

    # perf stat escribe sus métricas en stderr con delimitador coma
    resultados = {}
    for line in proc.stderr.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 3:
            val_raw = parts[0]
            evt_raw = parts[2]

            # Si el contador no fue soportado o no contó
            if val_raw in ["<not counted>", "<not supported>"]:
                resultados[evt_raw] = 0.0
                continue

            try:
                # Quitar posibles puntos o caracteres de formato numérico
                val_clean = val_raw.replace(".", "").replace(",", ".") if "." in val_raw and "," in val_raw else val_raw
                # Si val_raw es un número entero/flotante directo
                val_float = float(val_raw)
                resultados[evt_raw] = val_float
            except ValueError:
                pass

    return resultados


# ==============================================================================
# GENERACIÓN DE GRÁFICAS DE MICROARQUITECTURA
# ==============================================================================
def graficar_ipc(metricas_resumen, output_path):
    """Genera gráfico comparativo de IPC (Instrucciones por Ciclo)."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas_resumen]
    ipcs = [metricas_resumen[m]["ipc"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    fig, ax = plt.subplots(figsize=(12, 6.5))
    x_pos = np.arange(len(modelos))
    bars = ax.bar(x_pos, ipcs, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)

    # Línea de referencia típica (IPC = 1.0)
    ax.axhline(1.0, color="#d90429", linestyle="--", linewidth=1.3, label="IPC Base (1.0)")

    ax.set_xticks(x_pos)
    etiquetas_x = [f"{m}\n({metricas_resumen[m]['hilos']} hilos)" for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")
    ax.set_ylabel("IPC ($Instrucciones / Ciclo$) [Mayor es mejor]")
    ax.set_title("Instrucciones Retiradas por Ciclo de Reloj (IPC) por Modelo")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(
            f"{h:.2f}",
            xy=(bar.get_x() + bar.get_width() / 2, h),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold"
        )

    ax.set_ylim(0, max(ipcs) * 1.25)
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_cache_miss_rates(metricas_resumen, output_path):
    """Genera panel dual de tasas de fallos en L1 Data Cache y Last-Level Cache."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
    modelos = [m for m in ORDEN_CANONICO if m in metricas_resumen]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]
    x_pos = np.arange(len(modelos))
    short_labels = [f"{m}\n({metricas_resumen[m]['hilos']} h)" for m in modelos]

    # Panel 1: L1 Data Cache Miss Rate
    l1_rates = [metricas_resumen[m]["l1_miss_rate_pct"] for m in modelos]
    bars1 = ax1.bar(x_pos, l1_rates, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)
    ax1.set_xticks(x_pos)
    ax1.set_xticklabels(short_labels, rotation=15, ha="right", fontsize=9)
    ax1.set_ylabel("Tasa de Fallos L1-D (%) [Menor es mejor]")
    ax1.set_title("A. Tasa de Fallos en L1 Data Cache")
    for bar in bars1:
        h = bar.get_height()
        ax1.annotate(f"{h:.2f}%", xy=(bar.get_x() + bar.get_width() / 2, h),
                     xytext=(0, 5), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")
    ax1.set_ylim(0, max(max(l1_rates) * 1.25, 1.0))

    # Panel 2: Last Level Cache (LLC o Cache General) Miss Rate
    llc_rates = [metricas_resumen[m]["llc_miss_rate_pct"] for m in modelos]
    bars2 = ax2.bar(x_pos, llc_rates, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)
    ax2.set_xticks(x_pos)
    ax2.set_xticklabels(short_labels, rotation=15, ha="right", fontsize=9)
    ax2.set_ylabel("Tasa de Fallos LLC (%) [Menor es mejor]")
    ax2.set_title("B. Tasa de Fallos en Last-Level Cache (L3)")
    for bar in bars2:
        h = bar.get_height()
        ax2.annotate(f"{h:.2f}%", xy=(bar.get_x() + bar.get_width() / 2, h),
                     xytext=(0, 5), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")
    ax2.set_ylim(0, max(max(llc_rates) * 1.25, 1.0))

    fig.suptitle("Comportamiento de la Jerarquía de Memoria Caché en Ray Tracing", fontsize=15, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_context_switches(metricas_resumen, output_path):
    """Genera gráfico comparativo de Cambios de Contexto y Migraciones."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas_resumen]
    cs_vals = [metricas_resumen[m]["context_switches"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]

    fig, ax = plt.subplots(figsize=(12, 6.5))
    x_pos = np.arange(len(modelos))
    bars = ax.bar(x_pos, cs_vals, color=colors, alpha=0.85, edgecolor="#1a1a1a", linewidth=1.1)

    ax.set_xticks(x_pos)
    etiquetas_x = [f"{m}\n({metricas_resumen[m]['hilos']} hilos)" for m in modelos]
    ax.set_xticklabels(etiquetas_x, rotation=15, ha="right")
    ax.set_ylabel("Cambios de Contexto Totales (Eventos SO)")
    ax.set_title("Sobrecarga de Planificación del Sistema Operativo: Context Switches")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(
            f"{int(h):,}".replace(",", "."),
            xy=(bar.get_x() + bar.get_width() / 2, h),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold"
        )

    ax.set_ylim(0, max(cs_vals) * 1.25 if max(cs_vals) > 0 else 10)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_dashboard_microarquitectura(metricas_resumen, output_path):
    """Genera panel 2x2 consolidado con las 4 dimensiones microarquitectónicas clave."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas_resumen]
    colors = [PALETA_MODELOS.get(m, "#7f7f7f") for m in modelos]
    x_pos = np.arange(len(modelos))
    short_labels = [m.replace("Fine-Grained ", "FG ").replace("SMT ", "SMT ").replace("Sobre-suscripción ", "") for m in modelos]

    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    # (0, 0) IPC
    ipcs = [metricas_resumen[m]["ipc"] for m in modelos]
    bars_ipc = axs[0, 0].bar(x_pos, ipcs, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[0, 0].axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="Ref. 1.0 IPC")
    axs[0, 0].set_title("(A) Instrucciones por Ciclo (IPC)")
    axs[0, 0].set_ylabel("IPC")
    axs[0, 0].set_xticks(x_pos)
    axs[0, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    axs[0, 0].legend(loc="upper left", fontsize=8.5)
    for b in bars_ipc:
        axs[0, 0].annotate(f"{b.get_height():.2f}", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (0, 1) L1 Miss Rate
    l1_rates = [metricas_resumen[m]["l1_miss_rate_pct"] for m in modelos]
    bars_l1 = axs[0, 1].bar(x_pos, l1_rates, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[0, 1].set_title("(B) Tasa de Fallos L1 Data Cache (%)")
    axs[0, 1].set_ylabel("Fallos L1-D (%)")
    axs[0, 1].set_xticks(x_pos)
    axs[0, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    for b in bars_l1:
        axs[0, 1].annotate(f"{b.get_height():.2f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 0) Branch Miss Rate
    br_rates = [metricas_resumen[m]["branch_miss_rate_pct"] for m in modelos]
    bars_br = axs[1, 0].bar(x_pos, br_rates, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[1, 0].set_title("(C) Tasa de Fallos en Predicción de Saltos (%)")
    axs[1, 0].set_ylabel("Fallos de Salto (%)")
    axs[1, 0].set_xticks(x_pos)
    axs[1, 0].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    for b in bars_br:
        axs[1, 0].annotate(f"{b.get_height():.2f}%", xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    # (1, 1) Context Switches
    cs_vals = [metricas_resumen[m]["context_switches"] for m in modelos]
    bars_cs = axs[1, 1].bar(x_pos, cs_vals, color=colors, alpha=0.85, edgecolor="#1a1a1a")
    axs[1, 1].set_title("(D) Cambios de Contexto del SO")
    axs[1, 1].set_ylabel("Context Switches")
    axs[1, 1].set_xticks(x_pos)
    axs[1, 1].set_xticklabels(short_labels, rotation=20, ha="right", fontsize=9)
    for b in bars_cs:
        axs[1, 1].annotate(f"{int(b.get_height()):,}".replace(",", "."), xy=(b.get_x() + b.get_width()/2, b.get_height()),
                           xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    fig.suptitle("Dashboard Microarquitectónico Completo (Linux perf PMU)", fontsize=16, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


# ==============================================================================
# PUNTO DE ENTRADA PRINCIPAL
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(description="Perfilador microarquitectónico de hardware con perf.")
    parser.add_argument("--runs", type=int, default=3, help="Número de corridas para promediar contadores (def: 3).")
    parser.add_argument("--width", type=int, default=400, help="Ancho de la imagen (def: 400).")
    parser.add_argument("--samples", type=int, default=10, help="Muestras por píxel (def: 10).")
    parser.add_argument("--depth", type=int, default=50, help="Profundidad de rebote (def: 50).")
    parser.add_argument("--models", nargs="+", default=list(CATALOGO_MODELOS.keys()),
                        help="Modelos a perfilar (def: todos).")
    parser.add_argument("--output-csv", default="../metricas/raytracing_perf_metrics.csv",
                        help="Ruta al CSV de salida.")
    parser.add_argument("--output-dir", default="../metricas/graficas",
                        help="Directorio destino para las figuras.")
    parser.add_argument("--sudo", action="store_true", help="Forzar uso de sudo con perf.")
    parser.add_argument("--plot-only", action="store_true", help="Solo generar gráficas a partir del CSV existente sin ejecutar perf.")
    args = parser.parse_args()

    scripts_dir = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(args.output_dir, exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.output_csv)), exist_ok=True)

    # Modo exclusivo de graficación a partir de CSV previo
    if args.plot_only:
        print("==============================================================================")
        print("           GENERACIÓN DE GRÁFICAS PERF DESDE CSV EXISTENTE                    ")
        print("==============================================================================")
        if not os.path.exists(args.output_csv):
            print(f"[ERROR] No existe el archivo CSV de métricas: {args.output_csv}", file=sys.stderr)
            sys.exit(1)

        metricas_consolidadas = {}
        with open(args.output_csv, "r", encoding="utf-8") as f_in:
            reader = csv.DictReader(f_in)
            for row in reader:
                m_nom = row["modelo"]
                metricas_consolidadas[m_nom] = {
                    "modelo": m_nom,
                    "modo": row["modo"],
                    "hilos": int(row["hilos"]),
                    "corridas": int(row["corridas"]),
                    "cycles": float(row["cycles"]),
                    "instructions": float(row["instructions"]),
                    "ipc": float(row["ipc"]),
                    "l1_loads": float(row["l1_loads"]),
                    "l1_misses": float(row["l1_misses"]),
                    "l1_miss_rate_pct": float(row["l1_miss_rate_pct"]),
                    "llc_loads": float(row["llc_loads"]),
                    "llc_misses": float(row["llc_misses"]),
                    "llc_miss_rate_pct": float(row["llc_miss_rate_pct"]),
                    "branches": float(row["branches"]),
                    "branch_misses": float(row["branch_misses"]),
                    "branch_miss_rate_pct": float(row["branch_miss_rate_pct"]),
                    "context_switches": float(row["context_switches"]),
                    "cpu_migrations": float(row["cpu_migrations"]),
                    "page_faults": float(row["page_faults"]),
                }

        p_ipc = os.path.join(args.output_dir, "07_perf_ipc_comparativo.png")
        graficar_ipc(metricas_consolidadas, p_ipc)

        p_cache = os.path.join(args.output_dir, "08_perf_cache_miss_rates.png")
        graficar_cache_miss_rates(metricas_consolidadas, p_cache)

        p_cs = os.path.join(args.output_dir, "09_perf_context_switches.png")
        graficar_context_switches(metricas_consolidadas, p_cs)

        p_dash = os.path.join(args.output_dir, "10_perf_resumen_microarquitectura.png")
        graficar_dashboard_microarquitectura(metricas_consolidadas, p_dash)

        print("\n[OK] Gráficas de microarquitectura generadas exitosamente desde CSV.")
        sys.exit(0)

    print("==============================================================================")
    print("      PERFILADO MICROARQUITECTÓNICO DE HARDWARE (Linux perf / PMU)            ")
    print("==============================================================================")
    print(f"Resolución        : {args.width}x{int(args.width * 9 / 16)}")
    print(f"Muestras / Píxel  : {args.samples}")
    print(f"Profundidad       : {args.depth}")
    print(f"Corridas / Modelo : {args.runs}")
    print(f"Salida CSV        : {os.path.abspath(args.output_csv)}")
    print(f"Salida Gráficas   : {os.path.abspath(args.output_dir)}")
    print("==============================================================================\n")

    # 1. Comprobación de acceso a perf
    disponible, usar_sudo, msg_error = verificar_acceso_perf()
    if args.sudo:
        usar_sudo = True
        disponible = True

    if not disponible:
        print("[ERROR DE PERMISOS LINUX / PMU]", file=sys.stderr)
        print(msg_error, file=sys.stderr)
        sys.exit(1)

    # 2. Campaña de perfilado
    metricas_consolidadas = {}

    for m_key in args.models:
        if m_key not in CATALOGO_MODELOS:
            print(f"[!] Modelo desconocido: {m_key}, omitiendo...")
            continue

        cfg = CATALOGO_MODELOS[m_key]
        print(f"--> Perfilando: {cfg['nombre']} ({cfg['hilos']} hilos)...")

        cmd = [cfg["binario"], "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)] + cfg["args_extra"]

        corridas_data = []
        for r in range(1, args.runs + 1):
            try:
                stats = ejecutar_perf_stat(cmd, scripts_dir, usar_sudo=usar_sudo)
                corridas_data.append(stats)
                print(f"    [Corrida {r}/{args.runs}] Completada.")
            except Exception as e:
                print(f"    [ERROR] Falló corrida {r}: {e}", file=sys.stderr)

        if not corridas_data:
            print(f"    [!] No se pudieron recolectar métricas para {cfg['nombre']}.")
            continue

        # Promediar contadores
        promedios = {}
        for evt in EVENTOS_PERF:
            valores = [c.get(evt, 0.0) for c in corridas_data]
            promedios[evt] = statistics.mean(valores) if valores else 0.0

        # Derivar métricas arquitectónicas clave
        cycles = promedios.get("cycles", 0.0)
        instructions = promedios.get("instructions", 0.0)
        ipc = (instructions / cycles) if cycles > 0 else 0.0

        l1_loads = promedios.get("L1-dcache-loads", 0.0)
        l1_misses = promedios.get("L1-dcache-load-misses", 0.0)
        l1_miss_rate = (l1_misses / l1_loads * 100.0) if l1_loads > 0 else 0.0

        llc_loads = promedios.get("LLC-loads", 0.0) or promedios.get("cache-references", 0.0)
        llc_misses = promedios.get("LLC-load-misses", 0.0) or promedios.get("cache-misses", 0.0)
        llc_miss_rate = (llc_misses / llc_loads * 100.0) if llc_loads > 0 else 0.0

        branches = promedios.get("branches", 0.0)
        branch_misses = promedios.get("branch-misses", 0.0)
        branch_miss_rate = (branch_misses / branches * 100.0) if branches > 0 else 0.0

        context_switches = promedios.get("context-switches", 0.0)
        cpu_migrations = promedios.get("cpu-migrations", 0.0)
        page_faults = promedios.get("page-faults", 0.0)

        metricas_consolidadas[cfg["nombre"]] = {
            "modelo": cfg["nombre"],
            "modo": cfg["modo"],
            "hilos": cfg["hilos"],
            "corridas": len(corridas_data),
            "cycles": cycles,
            "instructions": instructions,
            "ipc": ipc,
            "l1_loads": l1_loads,
            "l1_misses": l1_misses,
            "l1_miss_rate_pct": l1_miss_rate,
            "llc_loads": llc_loads,
            "llc_misses": llc_misses,
            "llc_miss_rate_pct": llc_miss_rate,
            "branches": branches,
            "branch_misses": branch_misses,
            "branch_miss_rate_pct": branch_miss_rate,
            "context_switches": context_switches,
            "cpu_migrations": cpu_migrations,
            "page_faults": page_faults,
        }

        print(f"    -> IPC: {ipc:.2f} | L1-D Miss: {l1_miss_rate:.2f}% | LLC Miss: {llc_miss_rate:.2f}% | CS: {int(context_switches)}\n")

    # 3. Guardar en CSV
    headers = [
        "modelo", "modo", "hilos", "corridas",
        "cycles", "instructions", "ipc",
        "l1_loads", "l1_misses", "l1_miss_rate_pct",
        "llc_loads", "llc_misses", "llc_miss_rate_pct",
        "branches", "branch_misses", "branch_miss_rate_pct",
        "context_switches", "cpu_migrations", "page_faults"
    ]

    with open(args.output_csv, "w", newline="", encoding="utf-8") as f_out:
        writer = csv.writer(f_out)
        writer.writerow(headers)
        for m_name in ORDEN_CANONICO:
            if m_name in metricas_consolidadas:
                d = metricas_consolidadas[m_name]
                writer.writerow([
                    d["modelo"], d["modo"], d["hilos"], d["corridas"],
                    f"{d['cycles']:.0f}", f"{d['instructions']:.0f}", f"{d['ipc']:.3f}",
                    f"{d['l1_loads']:.0f}", f"{d['l1_misses']:.0f}", f"{d['l1_miss_rate_pct']:.3f}",
                    f"{d['llc_loads']:.0f}", f"{d['llc_misses']:.0f}", f"{d['llc_miss_rate_pct']:.3f}",
                    f"{d['branches']:.0f}", f"{d['branch_misses']:.0f}", f"{d['branch_miss_rate_pct']:.3f}",
                    f"{d['context_switches']:.0f}", f"{d['cpu_migrations']:.0f}", f"{d['page_faults']:.0f}"
                ])

    print("==============================================================================")
    print("                 GENERANDO GRÁFICAS DE MICROARQUITECTURA                      ")
    print("==============================================================================")
    p_ipc = os.path.join(args.output_dir, "07_perf_ipc_comparativo.png")
    graficar_ipc(metricas_consolidadas, p_ipc)

    p_cache = os.path.join(args.output_dir, "08_perf_cache_miss_rates.png")
    graficar_cache_miss_rates(metricas_consolidadas, p_cache)

    p_cs = os.path.join(args.output_dir, "09_perf_context_switches.png")
    graficar_context_switches(metricas_consolidadas, p_cs)

    p_dash = os.path.join(args.output_dir, "10_perf_resumen_microarquitectura.png")
    graficar_dashboard_microarquitectura(metricas_consolidadas, p_dash)

    print("\n[OK] Perfilado y visualización microarquitectónica completada exitosamente.")
    print(f"  -> CSV      : {os.path.abspath(args.output_csv)}")
    print(f"  -> Gráficas : {os.path.abspath(args.output_dir)}\n")


if __name__ == "__main__":
    main()
