#!/usr/bin/env python3
"""
==============================================================================
Módulo de Perfilado Microarquitectónico de Hardware con perf (CE 4302)
Problema: Reacción-Difusión de Gray-Scott
Instituto Tecnológico de Costa Rica (TEC)
==============================================================================
Este script automatiza la recolección de contadores de rendimiento de hardware
(PMU) mediante Linux `perf stat` para caracterizar los modelos de ejecución:
  - Secuencial
  - CMP (8 núcleos físicos)
  - SMT (16 hilos lógicos en hardware)
  - Coarse-Grained (8 hilos / ráfagas continuas)
  - Fine-Grained Cooperativo (1 core / conmutación round-robin)
  - Fine-Grained Paralelo Cíclico (8 hilos / intercalado cíclico)

Contadores y métricas evaluadas:
  - cycles (Ciclos de CPU)
  - instructions (Instrucciones retiradas)
  - IPC (Instructions Per Cycle = instructions / cycles)
  - cache-references y cache-misses (Tasa de fallo de caché)
  - branches y branch-misses (Tasa de fallo de saltos)
  - context-switches y cpu-migrations
  - task-clock y CPUs efectivas utilizadas (Saturación de núcleos)
==============================================================================
"""

import sys
import os
import subprocess
import argparse
import csv
import time
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns


# Configuración estética para gráficos científicos (300 DPI)
plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
plt.rcParams.update({
    "font.size": 11,
    "font.family": "sans-serif",
    "axes.labelsize": 12,
    "axes.titlesize": 13,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "figure.titlesize": 15,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "savefig.bbox": "tight"
})

PALETA_MODELOS = {
    "Secuencial": "#64748b",
    "CMP": "#2563eb",
    "SMT (HW Real)": "#059669",
    "Coarse-Grained": "#7c3aed",
    "Fine-Grained (Cooperativo)": "#e11d48",
    "Fine-Grained (Paralelo Cíclico)": "#0891b2"
}

EVENTOS_PERF = [
    "task-clock",
    "context-switches",
    "cpu-migrations",
    "page-faults",
    "cycles",
    "instructions",
    "branches",
    "branch-misses",
    "cache-references",
    "cache-misses"
]


def verificar_permisos_perf() -> bool:
    """Verifica si perf_event_paranoid permite contadores de hardware."""
    try:
        with open("/proc/sys/kernel/perf_event_paranoid", "r") as f:
            val = int(f.read().strip())
            return val <= 0
    except Exception:
        return False


def configurar_perf_paranoid():
    """Intenta ajustar kernel.perf_event_paranoid a -1 si es necesario."""
    if not verificar_permisos_perf():
        print("--> [perf] Configurando kernel.perf_event_paranoid=-1 para habilitar PMU...")
        os.system("echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid > /dev/null")


def ejecutar_perf_stat(cmd: list, cwd: Path) -> dict:
    """Ejecuta un comando bajo perf stat y parsea la salida CSV."""
    eventos_str = ",".join(EVENTOS_PERF)
    perf_cmd = [
        "perf", "stat",
        "-x,",  # Salida CSV delimitada por comas
        "-e", eventos_str
    ] + cmd

    env = os.environ.copy()
    env["LC_ALL"] = "C"
    res = subprocess.run(perf_cmd, cwd=cwd, capture_output=True, text=True, env=env)
    if res.returncode != 0:
        print(f"    [ADVERTENCIA perf] Código {res.returncode}: {res.stderr.strip()}", file=sys.stderr)
    
    # perf stat escribe sus estadísticas en stderr
    lineas = res.stderr.strip().split("\n")
    metricas = {}

    for linea in lineas:
        partes = linea.split(",")
        if len(partes) >= 3:
            valor_raw = partes[0].strip()
            evento = partes[2].strip()
            try:
                # Convertir valor numérico
                if "." in valor_raw:
                    val = float(valor_raw)
                else:
                    val = int(valor_raw)
                metricas[evento] = val
            except ValueError:
                metricas[evento] = 0

    return metricas


def graficar_perfilado_hardware(df: pd.DataFrame, output_img: Path):
    """Genera la figura de 4 cuadrantes con el análisis microarquitectónico de perf."""
    print(f"--> [Gráficas] Generando visualización: {output_img.name}...")
    fig, axs = plt.subplots(2, 2, figsize=(16, 12))

    orden = [m for m in PALETA_MODELOS.keys() if m in df["modelo"].values]
    df_ordenado = df.set_index("modelo").loc[orden].reset_index()
    colores = [PALETA_MODELOS[m] for m in df_ordenado["modelo"]]

    # Helper para formatear etiquetas de los ejes
    def fmt_label(name):
        return name.replace(" (", "\n(")

    etiquetas_x = [fmt_label(m) for m in df_ordenado["modelo"]]

    # -------------------------------------------------------------------------
    # Cuadrante 1: IPC (Instructions Per Cycle)
    # -------------------------------------------------------------------------
    ax1 = axs[0, 0]
    bars1 = ax1.bar(range(len(df_ordenado)), df_ordenado["ipc"], color=colores, width=0.55, edgecolor="#1e293b")
    ax1.set_title("A. Eficiencia de Instrucciones por Ciclo (IPC)\n$\\text{IPC} = \\text{Instrucciones Retiradas} / \\text{Ciclos Totales}$", fontweight="bold")
    ax1.set_ylabel("IPC (Instrucciones / Ciclo)", fontweight="bold")
    ax1.set_xticks(range(len(df_ordenado)))
    ax1.set_xticklabels(etiquetas_x, rotation=20, ha="right")
    ax1.set_ylim(0, max(df_ordenado["ipc"]) * 1.25)
    for bar in bars1:
        h = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width() / 2.0, h + 0.05, f"{h:.2f}", ha="center", va="bottom", fontweight="bold", fontsize=10)

    # -------------------------------------------------------------------------
    # Cuadrante 2: Saturación de Hilos / CPUs Efectivas Utilizadas
    # -------------------------------------------------------------------------
    ax2 = axs[0, 1]
    bars2 = ax2.bar(range(len(df_ordenado)), df_ordenado["cpus_utilizadas"], color=colores, width=0.55, edgecolor="#1e293b")
    ax2.axhline(8.0, color="#2563eb", linestyle="--", linewidth=1.2, label="Límite 8 Núcleos Físicos")
    ax2.axhline(16.0, color="#059669", linestyle=":", linewidth=1.2, label="Límite 16 Hilos Lógicos SMT")
    ax2.set_title("B. Nivel de Concurrencia y Saturación de CPU\n$\\text{CPUs Efectivas} = \\text{task-clock} / \\text{tiempo real}$", fontweight="bold")
    ax2.set_ylabel("CPUs Activas Equivalentes", fontweight="bold")
    ax2.set_xticks(range(len(df_ordenado)))
    ax2.set_xticklabels(etiquetas_x, rotation=20, ha="right")
    ax2.set_ylim(0, 18)
    ax2.legend(loc="upper left")
    for bar in bars2:
        h = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width() / 2.0, h + 0.25, f"{h:.1f}", ha="center", va="bottom", fontweight="bold", fontsize=10)

    # -------------------------------------------------------------------------
    # Cuadrante 3: Tasa de Fallos de Caché (%)
    # -------------------------------------------------------------------------
    ax3 = axs[1, 0]
    bars3 = ax3.bar(range(len(df_ordenado)), df_ordenado["cache_miss_rate_pct"], color=colores, width=0.55, edgecolor="#1e293b")
    ax3.set_title("C. Tasa de Fallos de Caché de Memoria (%)\n$\\text{Cache Miss Rate} = (\\text{Cache Misses} / \\text{References}) \\times 100\\%$", fontweight="bold")
    ax3.set_ylabel("Fallos de Caché (%)", fontweight="bold")
    ax3.set_xticks(range(len(df_ordenado)))
    ax3.set_xticklabels(etiquetas_x, rotation=20, ha="right")
    ax3.set_ylim(0, max(df_ordenado["cache_miss_rate_pct"]) * 1.35 if max(df_ordenado["cache_miss_rate_pct"]) > 0 else 10)
    for bar in bars3:
        h = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width() / 2.0, h + 0.2, f"{h:.2f}%", ha="center", va="bottom", fontweight="bold", fontsize=10)

    # -------------------------------------------------------------------------
    # Cuadrante 4: Ciclos Totales de CPU (Millones) vs Instrucciones
    # -------------------------------------------------------------------------
    ax4 = axs[1, 1]
    ciclos_m = df_ordenado["cycles"] / 1e9
    bars4 = ax4.bar(range(len(df_ordenado)), ciclos_m, color=colores, width=0.55, edgecolor="#1e293b")
    ax4.set_title("D. Carga Total de Ciclos de CPU (Miles de Millones - Gcycles)\n$\\text{Giga-ciclos Acumulados por todos los hilos}$", fontweight="bold")
    ax4.set_ylabel("Giga-Ciclos (10^9 ciclos)", fontweight="bold")
    ax4.set_xticks(range(len(df_ordenado)))
    ax4.set_xticklabels(etiquetas_x, rotation=20, ha="right")
    ax4.set_ylim(0, max(ciclos_m) * 1.25 if max(ciclos_m) > 0 else 10)
    for bar in bars4:
        h = bar.get_height()
        ax4.text(bar.get_x() + bar.get_width() / 2.0, h + 0.2, f"{h:.2f} G", ha="center", va="bottom", fontweight="bold", fontsize=10)

    plt.suptitle("Perfilado Microarquitectónico de Hardware (Linux perf) — Gray-Scott", fontsize=16, fontweight="bold", y=0.995)
    plt.tight_layout()
    output_img.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_img)
    plt.close()
    print(f"--> [Gráficas] Guardado exitoso: {output_img}")


def main():
    parser = argparse.ArgumentParser(description="Perfilado Microarquitectónico con Linux perf (CE 4302)")
    parser.add_argument("--size", type=int, default=512, help="Dimensión de la grilla (defecto: 512)")
    parser.add_argument("--steps", type=int, default=16000, help="Pasos de simulación (defecto: 16000)")
    parser.add_argument("--threads", type=int, default=8, help="Hilos para CMP/Coarse/Fine (defecto: 8)")
    parser.add_argument("--smt-threads", type=int, default=16, help="Hilos para SMT (defecto: 16)")
    parser.add_argument("--quantum", type=int, default=1, help="Cuantum para Fine-Grained (defecto: 1)")
    parser.add_argument("--output-csv", type=str, default="../metricas/perf_hardware_counters.csv", help="Ruta CSV de salida")
    parser.add_argument("--output-img", type=str, default="../metricas/graficas/06_perfilado_hardware_perf.png", help="Ruta de la imagen de salida")
    args = parser.parse_args()

    scripts_dir = Path(__file__).resolve().parent
    base_dir = scripts_dir.parent
    csv_output = (scripts_dir / args.output_csv).resolve()
    img_output = (scripts_dir / args.output_img).resolve()

    # Compilar primero
    print("--> [Compilación] Verificando binarios antes del perfilado...")
    subprocess.run(["make", "all"], cwd=scripts_dir, check=True)

    configurar_perf_paranoid()

    catalogo = [
        {
            "modelo": "Secuencial",
            "cmd": ["./gray_scott_seq", "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "modelo": "CMP",
            "cmd": ["./gray_scott_cmp", "--threads", str(args.threads), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "modelo": "SMT (HW Real)",
            "cmd": ["./gray_scott_smt", "--threads", str(args.smt_threads), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "modelo": "Coarse-Grained",
            "cmd": ["./gray_scott_coarse", "--threads", str(args.threads), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "modelo": "Fine-Grained (Cooperativo)",
            "cmd": ["./gray_scott_fine", "--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "cooperative", "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "modelo": "Fine-Grained (Paralelo Cíclico)",
            "cmd": ["./gray_scott_fine", "--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "parallel", "--size", str(args.size), "--steps", str(args.steps)]
        }
    ]

    print("\n==============================================================================")
    print("       INICIANDO CAMPAÑA DE PERFILADO MICROARQUITECTÓNICO CON PERF            ")
    print("==============================================================================")
    print(f"Dimensión Grilla   : {args.size}x{args.size}")
    print(f"Pasos Temporales   : {args.steps}")
    print(f"CSV de Salida      : {csv_output}")
    print(f"Gráfica de Salida  : {img_output}\n")

    filas_resultados = []

    for item in catalogo:
        modelo = item["modelo"]
        cmd = item["cmd"]
        print(f">>> Perfilando modelo: [{modelo}]...")
        t0 = time.time()
        metricas = ejecutar_perf_stat(cmd, scripts_dir)
        t_wall = time.time() - t0

        cycles = metricas.get("cycles", 0)
        instructions = metricas.get("instructions", 0)
        task_clock_ms = metricas.get("task-clock", 0.0)
        cache_refs = metricas.get("cache-references", 0)
        cache_misses = metricas.get("cache-misses", 0)
        branches = metricas.get("branches", 0)
        branch_misses = metricas.get("branch-misses", 0)
        c_switches = metricas.get("context-switches", 0)
        cpu_migr = metricas.get("cpu-migrations", 0)

        ipc = (instructions / cycles) if cycles > 0 else 0.0
        cache_miss_rate = (cache_misses / cache_refs * 100.0) if cache_refs > 0 else 0.0
        branch_miss_rate = (branch_misses / branches * 100.0) if branches > 0 else 0.0
        cpus_utilizadas = (task_clock_ms / (t_wall * 1000.0)) if t_wall > 0 else 0.0

        print(f"    Wall Time: {t_wall:.2f} s | IPC: {ipc:.2f} | CPUs: {cpus_utilizadas:.1f} | Cache Miss: {cache_miss_rate:.2f}% | Context Switches: {c_switches}")

        filas_resultados.append({
            "modelo": modelo,
            "wall_time_s": round(t_wall, 3),
            "cycles": cycles,
            "instructions": instructions,
            "ipc": round(ipc, 3),
            "task_clock_ms": round(task_clock_ms, 2),
            "cpus_utilizadas": round(cpus_utilizadas, 2),
            "cache_references": cache_refs,
            "cache_misses": cache_misses,
            "cache_miss_rate_pct": round(cache_miss_rate, 2),
            "branches": branches,
            "branch_misses": branch_misses,
            "branch_miss_rate_pct": round(branch_miss_rate, 2),
            "context_switches": c_switches,
            "cpu_migrations": cpu_migr
        })

    df_res = pd.DataFrame(filas_resultados)
    csv_output.parent.mkdir(parents=True, exist_ok=True)
    df_res.to_csv(csv_output, index=False)
    print(f"\n--> [CSV] Datos de contadores de hardware guardados en: {csv_output}")

    graficar_perfilado_hardware(df_res, img_output)
    print("\n==============================================================================")
    print("             ¡PERFILADO CON PERF COMPLETADO EXITOSAMENTE!                     ")
    print("==============================================================================\n")


if __name__ == "__main__":
    main()
