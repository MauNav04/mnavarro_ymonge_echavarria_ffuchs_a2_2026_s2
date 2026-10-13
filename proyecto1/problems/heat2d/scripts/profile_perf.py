#!/usr/bin/env python3
"""
==============================================================================
Perfilado Microarquitectónico de Hardware con Linux perf - Heat2D
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Problema: Difusión Térmica 2D (Heat2D)
==============================================================================

Mide contadores de hardware del procesador (PMU) y del sistema operativo:
  - Ciclos de CPU (cycles)
  - Instrucciones retiradas (instructions) -> IPC (Instructions Per Cycle)
  - L1 Data Cache (loads, misses, miss rate %)
  - Last-Level Cache / L3 (references, misses, miss rate %)
  - Predictor de saltos (branches, branch misses, miss rate %)
  - Cambios de contexto del SO (context-switches)
  - Migraciones de CPU (cpu-migrations) y Fallos de página (page-faults)

Genera:
  - Archivo CSV consolidado: heat2d/benchmark_results/heat2d_perf_metrics.csv
  - Gráficas científicas a 300 DPI:
      05_perf_ipc_comparativo.png
      06_perf_cache_miss_rates.png
      07_perf_context_switches.png
      10_perf_resumen_microarquitectura.png
==============================================================================
"""

import os
import sys

# Evitar advertencias de permisos en Linux para matplotlib
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_heat2d"

import csv
import argparse
import subprocess
import statistics
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

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
    "Secuencial": "#64748b",                  # Slate Gray
    "CMP": "#1f77b4",                         # Azul institucional
    "SMT (HW Real)": "#059669",               # Verde esmeralda
    "Coarse-Grained": "#7c3aed",              # Púrpura
    "Fine-Grained": "#0891b2",                # Teal / Cian
}

ORDEN_CANONICO = [
    "Secuencial",
    "CMP",
    "SMT (HW Real)",
    "Coarse-Grained",
    "Fine-Grained",
]

EVENTOS_PERF = [
    "cycles",
    "instructions",
    "cache-references",
    "cache-misses",
    "L1-dcache-loads",
    "L1-dcache-load-misses",
    "branches",
    "branch-misses",
    "context-switches",
    "cpu-migrations",
    "page-faults",
]


def ejecutar_comando_perf(comando, cwd, usar_sudo=False):
    """Ejecuta un comando monitoreado por perf stat y parsea la salida CSV."""
    eventos_str = ",".join(EVENTOS_PERF)
    env = os.environ.copy()
    env["LC_ALL"] = "C"

    # Intentar ejecutar sin sudo primero
    cmd_full = ["perf", "stat", "-x,", "-e", eventos_str, "--"] + comando
    proc = subprocess.run(cmd_full, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

    # Si falla y se solicitó sudo, reintentar con sudo
    if proc.returncode != 0 and usar_sudo:
        cmd_full = ["sudo", "perf", "stat", "-x,", "-e", eventos_str, "--"] + comando
        proc = subprocess.run(cmd_full, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

    if proc.returncode != 0:
        print(f"    [ADVERTENCIA perf] Código de error {proc.returncode}:\n{proc.stderr.strip()}", file=sys.stderr)

    metricas = {}

    for linea in proc.stderr.splitlines():
        linea = linea.strip()
        if not linea or linea.startswith("#"):
            continue
        partes = linea.split(",")
        if len(partes) >= 3:
            raw_val = partes[0].strip()
            evento = partes[2].strip()
            if "<not counted>" in raw_val or "<not supported>" in raw_val:
                continue
            try:
                val = float(raw_val) if "." in raw_val else int(raw_val)
                metricas[evento] = val
            except ValueError:
                pass

    return metricas


def graficar_ipc(metricas, output_path):
    """05_perf_ipc_comparativo.png: Bar chart de IPC con línea guía de 1.0."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas]
    ipcs = [metricas[m]["ipc"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#1f77b4") for m in modelos]

    fig, ax = plt.subplots(figsize=(10, 6))
    bars = ax.bar(modelos, ipcs, color=colors, edgecolor="#1a1a1a", alpha=0.85)

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.2f}", xy=(bar.get_x() + bar.get_width() / 2, h), xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9.5, fontweight="bold")

    ax.axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="IPC = 1.0 (Referencia escalar)")
    ax.set_title("Eficiencia Microarquitectónica: Instrucciones por Ciclo (IPC)")
    ax.set_xlabel("Modelo de Ejecución Concurrente")
    ax.set_ylabel("IPC (Instrucciones / Ciclo)")
    ax.legend(loc="upper left")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_cache_miss_rates(metricas, output_path):
    """06_perf_cache_miss_rates.png: Barras agrupadas de fallos de caché L1-D y LLC."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas]
    l1_rates = [metricas[m]["l1_miss_rate_pct"] for m in modelos]
    llc_rates = [metricas[m]["llc_miss_rate_pct"] for m in modelos]

    x = range(len(modelos))
    width = 0.35

    fig, ax = plt.subplots(figsize=(11, 6))
    bars1 = ax.bar([i - width/2 for i in x], l1_rates, width, label="L1-D Cache Miss Rate (%)", color="#3b82f6", edgecolor="#1a1a1a", alpha=0.85)
    bars2 = ax.bar([i + width/2 for i in x], llc_rates, width, label="LLC / L3 Cache Miss Rate (%)", color="#f97316", edgecolor="#1a1a1a", alpha=0.85)

    for bar in bars1:
        h = bar.get_height()
        ax.annotate(f"{h:.1f}%", xy=(bar.get_x() + bar.get_width() / 2, h), xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    for bar in bars2:
        h = bar.get_height()
        ax.annotate(f"{h:.1f}%", xy=(bar.get_x() + bar.get_width() / 2, h), xytext=(0, 4), textcoords="offset points", ha="center", fontsize=8.5, fontweight="bold")

    ax.set_title("Comportamiento del Subsistema de Memoria Caché")
    ax.set_xlabel("Modelo de Ejecución Concurrente")
    ax.set_ylabel("Tasa de Fallo (%)")
    ax.set_xticks(list(x))
    ax.set_xticklabels(modelos)
    ax.legend(loc="upper right")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_context_switches(metricas, output_path):
    """07_perf_context_switches.png: Sobrecarga del sistema operativo."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas]
    cs_vals = [metricas[m]["context_switches"] for m in modelos]
    colors = [PALETA_MODELOS.get(m, "#1f77b4") for m in modelos]

    fig, ax = plt.subplots(figsize=(10, 6))
    bars = ax.bar(modelos, cs_vals, color=colors, edgecolor="#1a1a1a", alpha=0.85)

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{int(h):,}", xy=(bar.get_x() + bar.get_width() / 2, h), xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9, fontweight="bold")

    ax.set_title("Sobrecarga del Sistema Operativo: Conmutaciones de Contexto")
    ax.set_xlabel("Modelo de Ejecución Concurrente")
    ax.set_ylabel("Cambios de Contexto (context-switches)")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def graficar_dashboard_microarquitectura(metricas, output_path):
    """10_perf_resumen_microarquitectura.png: Dashboard multipanel 2x2."""
    modelos = [m for m in ORDEN_CANONICO if m in metricas]
    colors = [PALETA_MODELOS.get(m, "#1f77b4") for m in modelos]

    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    # (0, 0) IPC
    ipcs = [metricas[m]["ipc"] for m in modelos]
    axs[0, 0].bar(modelos, ipcs, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[0, 0].axhline(1.0, color="#d90429", linestyle="--", linewidth=1.2, label="IPC = 1.0")
    axs[0, 0].set_title("(A) Rendimiento de Ejecución (IPC)")
    axs[0, 0].set_ylabel("Instrucciones / Ciclo")
    axs[0, 0].tick_params(axis="x", labelrotation=20)

    # (0, 1) L1-D Miss Rate
    l1_miss = [metricas[m]["l1_miss_rate_pct"] for m in modelos]
    axs[0, 1].bar(modelos, l1_miss, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[0, 1].set_title("(B) Tasa de Fallo de Caché L1-D (%)")
    axs[0, 1].set_ylabel("Fallos (%)")
    axs[0, 1].tick_params(axis="x", labelrotation=20)

    # (1, 0) LLC Miss Rate
    llc_miss = [metricas[m]["llc_miss_rate_pct"] for m in modelos]
    axs[1, 0].bar(modelos, llc_miss, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[1, 0].set_title("(C) Tasa de Fallo Last-Level Cache (L3) (%)")
    axs[1, 0].set_ylabel("Fallos (%)")
    axs[1, 0].tick_params(axis="x", labelrotation=20)

    # (1, 1) Context Switches
    cs_vals = [metricas[m]["context_switches"] for m in modelos]
    axs[1, 1].bar(modelos, cs_vals, color=colors, edgecolor="#1a1a1a", alpha=0.85)
    axs[1, 1].set_title("(D) Conmutaciones de Contexto del Kernel")
    axs[1, 1].set_ylabel("Eventos")
    axs[1, 1].tick_params(axis="x", labelrotation=20)

    fig.suptitle("Dashboard Microarquitectónico Integral - Heat2D (Linux perf / PMU)", fontsize=16, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {os.path.basename(output_path)}")


def main():
    parser = argparse.ArgumentParser(description="Perfilado microarquitectónico con Linux perf para Heat2D.")
    parser.add_argument("--runs", type=int, default=3, help="Corridas por modelo para promediar contadores PMU.")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directorio donde residen los binarios compilados.")
    parser.add_argument("--output-csv", default="../benchmark_results/heat2d_perf_metrics.csv", help="Ruta al CSV de salida.")
    parser.add_argument("--output-dir", default="../benchmark_results/ryzen_smt_200/plots", help="Directorio destino para las figuras.")
    parser.add_argument("--sudo", action="store_true", help="Forzar uso de sudo con perf.")
    parser.add_argument("--plot-only", action="store_true", help="Solo generar gráficas a partir del CSV existente.")
    args = parser.parse_args()

    scripts_dir = Path(__file__).resolve().parent
    base_dir = scripts_dir.parent
    repo_root = scripts_dir.parents[3]

    candidate_dirs = [
        Path(args.bin_dir).resolve(),
        (Path.cwd() / args.bin_dir).resolve(),
        (scripts_dir / args.bin_dir).resolve(),
        (repo_root / "build/bin").resolve(),
        (Path.cwd() / "build/bin").resolve(),
    ]
    bin_dir = next((d for d in candidate_dirs if (d / "heat2d_sequential").is_file()), None)
    if bin_dir is None:
        print("[ERROR] No se encontró el ejecutable 'heat2d_sequential' en ninguna de las rutas candidatas:", file=sys.stderr)
        for c in candidate_dirs:
            print(f"  - {c}", file=sys.stderr)
        sys.exit(1)

    output_csv = (scripts_dir / args.output_csv).resolve()
    output_dir = (scripts_dir / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    if args.plot_only:
        print("==============================================================================")
        print("           GENERACIÓN DE GRÁFICAS PERF DESDE CSV EXISTENTE (Heat2D)           ")
        print("==============================================================================")
        if not output_csv.exists():
            print(f"[ERROR] No existe el archivo CSV de métricas: {output_csv}", file=sys.stderr)
            sys.exit(1)

        metricas_consolidadas = {}
        with open(output_csv, "r", encoding="utf-8") as f_in:
            reader = csv.DictReader(f_in)
            for row in reader:
                m_nom = row["modelo"]
                metricas_consolidadas[m_nom] = {
                    "modelo": m_nom,
                    "hilos": int(row["hilos"]),
                    "cycles": float(row["cycles"]),
                    "instructions": float(row["instructions"]),
                    "ipc": float(row["ipc"]),
                    "l1_miss_rate_pct": float(row["l1_miss_rate_pct"]),
                    "llc_miss_rate_pct": float(row["llc_miss_rate_pct"]),
                    "context_switches": float(row["context_switches"]),
                }

        graficar_ipc(metricas_consolidadas, output_dir / "05_perf_ipc_comparativo.png")
        graficar_cache_miss_rates(metricas_consolidadas, output_dir / "06_perf_cache_miss_rates.png")
        graficar_context_switches(metricas_consolidadas, output_dir / "07_perf_context_switches.png")
        graficar_dashboard_microarquitectura(metricas_consolidadas, output_dir / "10_perf_resumen_microarquitectura.png")
        print("\n[OK] Gráficas de microarquitectura generadas exitosamente desde CSV.")
        return

    catalogo = [
        ("Secuencial", [str(bin_dir / "heat2d_sequential")], 1),
        ("CMP", [str(bin_dir / "heat2d_cmp"), "8"], 8),
        ("SMT (HW Real)", [str(bin_dir / "heat2d_smt"), "16"], 16),
        ("Coarse-Grained", [str(bin_dir / "heat2d_coarse_grained"), "8", "100", "8"], 8),
        ("Fine-Grained", [str(bin_dir / "heat2d_fine_grained"), "8"], 8),
    ]

    print("==============================================================================")
    print("      PERFILADO MICROARQUITECTÓNICO DE HARDWARE (Linux perf / PMU) - Heat2D   ")
    print("==============================================================================")
    print(f"Corridas / Modelo : {args.runs}")
    print(f"Salida CSV        : {output_csv}")
    print(f"Salida Gráficas   : {output_dir}\n")

    metricas_consolidadas = {}

    for nombre, cmd, hilos in catalogo:
        print(f"[*] Perfilando: {nombre} ({hilos} hilos)...")
        corridas_data = []
        for r in range(args.runs):
            data = ejecutar_comando_perf(cmd, base_dir, usar_sudo=args.sudo)
            if data:
                corridas_data.append(data)

        if not corridas_data:
            print(f"  [ERROR] No se obtuvieron métricas para {nombre}. Verifique permisos de perf.", file=sys.stderr)
            continue

        promedios = {}
        for ev in EVENTOS_PERF:
            vals = [c[ev] for c in corridas_data if ev in c]
            promedios[ev] = statistics.mean(vals) if vals else 0.0

        cycles = promedios.get("cycles", 1.0)
        instructions = promedios.get("instructions", 0.0)
        ipc = (instructions / cycles) if cycles > 0 else 0.0

        l1_loads = promedios.get("L1-dcache-loads", 1.0)
        l1_misses = promedios.get("L1-dcache-load-misses", 0.0)
        l1_miss_rate = (l1_misses / l1_loads * 100.0) if l1_loads > 0 else 0.0

        llc_refs = promedios.get("cache-references", 1.0)
        llc_misses = promedios.get("cache-misses", 0.0)
        llc_miss_rate = (llc_misses / llc_refs * 100.0) if llc_refs > 0 else 0.0

        context_switches = promedios.get("context-switches", 0.0)

        metricas_consolidadas[nombre] = {
            "modelo": nombre,
            "hilos": hilos,
            "corridas": len(corridas_data),
            "cycles": cycles,
            "instructions": instructions,
            "ipc": ipc,
            "l1_loads": l1_loads,
            "l1_misses": l1_misses,
            "l1_miss_rate_pct": l1_miss_rate,
            "llc_loads": llc_refs,
            "llc_misses": llc_misses,
            "llc_miss_rate_pct": llc_miss_rate,
            "context_switches": context_switches,
        }

        print(f"    -> IPC: {ipc:.2f} | L1-D Miss: {l1_miss_rate:.2f}% | LLC Miss: {llc_miss_rate:.2f}% | CS: {int(context_switches)}\n")

    if not metricas_consolidadas:
        print("[ERROR] No se pudo recopilar métricas para ningún modelo. Abortando guardado y graficación.", file=sys.stderr)
        sys.exit(1)

    # Guardar CSV
    headers = ["modelo", "hilos", "corridas", "cycles", "instructions", "ipc", "l1_loads", "l1_misses", "l1_miss_rate_pct", "llc_loads", "llc_misses", "llc_miss_rate_pct", "context_switches"]
    with open(output_csv, "w", newline="", encoding="utf-8") as f_out:
        writer = csv.writer(f_out)
        writer.writerow(headers)
        for m_name in ORDEN_CANONICO:
            if m_name in metricas_consolidadas:
                d = metricas_consolidadas[m_name]
                writer.writerow([
                    d["modelo"], d["hilos"], d["corridas"],
                    f"{d['cycles']:.0f}", f"{d['instructions']:.0f}", f"{d['ipc']:.3f}",
                    f"{d['l1_loads']:.0f}", f"{d['l1_misses']:.0f}", f"{d['l1_miss_rate_pct']:.3f}",
                    f"{d['llc_loads']:.0f}", f"{d['llc_misses']:.0f}", f"{d['llc_miss_rate_pct']:.3f}",
                    f"{d['context_switches']:.0f}"
                ])

    graficar_ipc(metricas_consolidadas, output_dir / "05_perf_ipc_comparativo.png")
    graficar_cache_miss_rates(metricas_consolidadas, output_dir / "06_perf_cache_miss_rates.png")
    graficar_context_switches(metricas_consolidadas, output_dir / "07_perf_context_switches.png")
    graficar_dashboard_microarquitectura(metricas_consolidadas, output_dir / "10_perf_resumen_microarquitectura.png")

    print("\n[OK] Perfilado de hardware Heat2D finalizado exitosamente.")
    print(f"  -> CSV      : {output_csv}")
    print(f"  -> Gráficas : {output_dir}\n")


if __name__ == "__main__":
    main()

