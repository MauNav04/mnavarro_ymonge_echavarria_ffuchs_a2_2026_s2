#!/usr/bin/env python3
"""
==============================================================================
Estudio Experimental de Escalabilidad Fuerte (Strong Scaling) - Gray-Scott
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Instituto Tecnológico de Costa Rica (TEC)
==============================================================================

Metodología:
  - Carga constante: Grilla fija N=512x512, 16,000 pasos.
  - Barrido sistemático de hilos: p in {1, 2, 4, 8, 16}.
  - Evalúa los modelos paralelos:
      * CMP (Chip Multiprocessing con afinidad a núcleos físicos)
      * SMT (Simultaneous Multithreading en hardware real)
      * Coarse-Grained (Multihilo de grano grueso / ráfagas)
      * Fine-Grained (Multihilo de grano fino cíclico)
  - Mide baseline Secuencial para calcular Speedup: S(p) = T_seq / T(p).
  - Contrasta los resultados contra la Aceleración Lineal Ideal (S = p) y las
    curvas teóricas de la Ley de Amdahl (90%, 95%, 98% paralelizable).
  - Exporta:
      * metricas/escalabilidad_hilos.csv (tabla resumen)
      * metricas/gray_scott_strong_scaling.csv (resumen compatible)
      * metricas/graficas/04_escalabilidad_hilos.png (gráfico 300 DPI)
==============================================================================
"""

import sys
import os

# Configurar directorio temporal de matplotlib
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_gray_scott"

import subprocess
import time
import argparse
import csv
import statistics
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ==============================================================================
# CONFIGURACIÓN ESTÉTICA
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
    "legend.fontsize": 9.5,
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
    "SMT": "#059669",
    "Coarse-Grained": "#7c3aed",                      # Púrpura
    "Fine-Grained (Paralelo Cíclico)": "#0891b2",      # Teal / Cian
}

MARCADORES = {
    "Secuencial": "s",
    "CMP": "o",
    "SMT (HW Real)": "^",
    "SMT": "^",
    "Coarse-Grained": "D",
    "Fine-Grained (Paralelo Cíclico)": "v",
}


def compilar_binarios(scripts_dir: Path):
    """Verifica y compila los binarios requeridos."""
    print("--> [Compilación] Verificando y compilando binarios con make all...")
    res = subprocess.run(["make", "all"], cwd=scripts_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Falló compilación:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
    print("--> [Compilación] Binarios listos.\n")


def ejecutar_comando(cmd: list, cwd: Path) -> float:
    """Ejecuta el binario C++ y extrae el tiempo en ms reportado en stderr."""
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Comando falló ({' '.join(cmd)}):\n{proc.stderr}")

    stderr_lines = proc.stderr.strip().splitlines()
    if not stderr_lines:
        raise RuntimeError(f"No se detectó salida en stderr:\nStdout:\n{proc.stdout}")

    csv_line = stderr_lines[-1].strip()
    partes = csv_line.split(",")
    modelo_tag = partes[0]

    if modelo_tag == "sequential":
        return float(partes[4])
    elif modelo_tag == "cmp":
        return float(partes[5])
    elif modelo_tag == "smt":
        return float(partes[6])
    elif modelo_tag == "coarse":
        return float(partes[6])
    elif modelo_tag == "fine":
        return float(partes[8])
    else:
        return float(partes[-1])


def graficar_strong_scaling_amdahl(datos_scaling, output_path):
    """
    Genera la figura científica de Strong Scaling vs Ley de Amdahl (300 DPI)
    diseñada idéntica al estándar del proyecto.
    """
    fig, ax = plt.subplots(figsize=(11, 7))

    todos_hilos = sorted(list({h for m in datos_scaling.values() for h, _, _ in m}))
    if not todos_hilos:
        todos_hilos = [1, 2, 4, 8, 16]

    max_p = max(todos_hilos)
    p_ideal = np.linspace(1, max_p, 100)

    # 1. Línea Ideal Lineal S(p) = p
    ax.plot(p_ideal, p_ideal, "k--", linewidth=1.5, alpha=0.7, label="Ideal Lineal ($S(p) = p$)")

    # 2. Curvas teóricas de Amdahl para diferentes fracciones paralelizadas
    colores_amdahl = {0.90: "#1f77b4", 0.95: "#ff7f0e", 0.98: "#2ca02c"}
    for p_frac in [0.90, 0.95, 0.98]:
        s_amdahl = 1.0 / ((1.0 - p_frac) + (p_frac / p_ideal))
        c_amd = colores_amdahl.get(p_frac, "#666666")
        ax.plot(p_ideal, s_amdahl, ":", linewidth=1.4, alpha=0.85, color=c_amd, label=f"Amdahl ({int(p_frac*100)}% paralelo)")

    # 3. Curvas experimentales por modelo
    orden_plot = ["Secuencial", "CMP", "SMT (HW Real)", "Coarse-Grained", "Fine-Grained (Paralelo Cíclico)"]
    puntos_por_hilo = {h: [] for h in todos_hilos}

    for mod_nom in orden_plot:
        if mod_nom not in datos_scaling:
            continue
        puntos = datos_scaling[mod_nom]
        puntos_sorted = sorted(puntos, key=lambda x: x[0])
        hilos = [p[0] for p in puntos_sorted]
        sps = [p[1] for p in puntos_sorted]
        m = MARCADORES.get(mod_nom, "o")
        c = PALETA_MODELOS.get(mod_nom, "#333333")

        if len(hilos) == 1 and hilos[0] == 1:
            ax.plot(hilos, sps, marker=m, markersize=9, color=c, label=f"Medido: {mod_nom}")
            puntos_por_hilo[1].append({"mod": mod_nom, "h": 1, "sp": sps[0], "color": c})
        else:
            ax.plot(hilos, sps, marker=m, markersize=8, linewidth=2.2, color=c, label=f"Medido: {mod_nom}")
            for h, sp in zip(hilos, sps):
                puntos_por_hilo[h].append({"mod": mod_nom, "h": h, "sp": sp, "color": c})

    # A. Para h = 1: Un único indicador limpio del baseline (evita superposición de 5 etiquetas)
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
    ax.set_title("Escalabilidad Fuerte (Strong Scaling) vs Ley de Amdahl")
    ax.set_xticks(todos_hilos)
    ax.set_xlim(0.8, max_p + 1.8)
    max_sp = max([sp for pts in datos_scaling.values() for _, sp, _ in pts] + [1.0])
    ax.set_ylim(0.5, max(max_p * 1.08, max_sp * 1.15))
    ax.legend(loc="upper left", framealpha=0.9)

    plt.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path)
    plt.close()
    print(f"  [+] Generado: {output_path.name}")


def cargar_datos_csv_scaling(csv_path: Path):
    """Carga los datos de escalabilidad desde CSV existente."""
    datos_scaling = {}
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            mod = row["modelo"]
            hilos = int(row["threads"] if "threads" in row else row["hilos"])
            sp_str = row["speedup"].replace("x", "") if "speedup" in row else "1.0"
            speedup = float(sp_str)
            eff_str = row.get("eficiencia_pct", row.get("eficiencia", "100.0")).replace("%", "")
            eficiencia = float(eff_str)
            if mod not in datos_scaling:
                datos_scaling[mod] = []
            datos_scaling[mod].append((hilos, speedup, eficiencia))
    return datos_scaling


def main():
    parser = argparse.ArgumentParser(description="Estudio de Escalabilidad Fuerte para Gray-Scott (CE 4302)")
    parser.add_argument("--runs", type=int, default=5,
                        help="Corridas por cada combinación de hilos (defecto: 5 para test rápido, 200 para campaña completa)")
    parser.add_argument("--size", type=int, default=512, help="Dimensión de la grilla (defecto: 512)")
    parser.add_argument("--steps", type=int, default=16000, help="Pasos de simulación (defecto: 16000)")
    parser.add_argument("--threads", type=str, default="1,2,4,8,16", help="Lista de hilos a evaluar (defecto: '1,2,4,8,16')")
    parser.add_argument("--cooldown", type=float, default=0.05, help="Pausa en segundos entre corridas")
    parser.add_argument("--output-csv", type=str, default="../metricas/gray_scott_strong_scaling.csv", help="CSV de salida")
    parser.add_argument("--output-img", type=str, default="../metricas/graficas/04_escalabilidad_hilos.png", help="Gráfica de salida")
    parser.add_argument("--plot-only", action="store_true", help="Solo generar la gráfica 04 desde el CSV existente sin ejecutar binarios")
    args = parser.parse_args()

    scripts_dir = Path(__file__).resolve().parent
    base_dir = scripts_dir.parent
    csv_output = (scripts_dir / args.output_csv).resolve()
    img_output = (scripts_dir / args.output_img).resolve()
    compat_csv = (scripts_dir / "../metricas/escalabilidad_hilos.csv").resolve()

    # Modo solo graficar
    if args.plot_only:
        print("==============================================================================")
        print("           GENERACIÓN DE GRÁFICA DE ESCALABILIDAD DESDE CSV                   ")
        print("==============================================================================")
        csv_target = csv_output if csv_output.exists() else compat_csv
        if not csv_target.exists():
            print(f"[ERROR] No existe archivo de escalabilidad ({csv_output} ni {compat_csv})", file=sys.stderr)
            sys.exit(1)
        datos = cargar_datos_csv_scaling(csv_target)
        graficar_strong_scaling_amdahl(datos, img_output)
        print("\n[OK] Gráfica 04_escalabilidad_hilos.png generada con éxito.")
        sys.exit(0)

    # Compilar primero
    compilar_binarios(scripts_dir)

    lista_hilos = [int(p.strip()) for p in args.threads.split(",") if p.strip()]

    print("==============================================================================")
    print("      CAMPAÑA EXPERIMENTAL DE ESCALABILIDAD FUERTE (STRONG SCALING)           ")
    print("==============================================================================")
    print(f"Dimensión Grilla   : {args.size}x{args.size}")
    print(f"Pasos Temporales   : {args.steps}")
    print(f"Corridas por Punto : {args.runs}")
    print(f"Barrido de Hilos   : {lista_hilos}")
    print(f"Salida CSV         : {csv_output}")
    print(f"Salida Gráfica     : {img_output}")
    print("==============================================================================\n")

    # 1. Medir Baseline Secuencial
    print(f">>> Midiendo Baseline Secuencial ({args.runs} corridas)...")
    tiempos_seq = []
    for r in range(1, args.runs + 1):
        t = ejecutar_comando(["./gray_scott_seq", "--size", str(args.size), "--steps", str(args.steps)], cwd=scripts_dir)
        tiempos_seq.append(t)
        if args.cooldown > 0:
            time.sleep(args.cooldown)

    t_seq_promedio = statistics.mean(tiempos_seq)
    t_seq_std = statistics.stdev(tiempos_seq) if len(tiempos_seq) > 1 else 0.0
    print(f"    Baseline Secuencial: Media = {t_seq_promedio:.2f} ms | StdDev = {t_seq_std:.2f} ms\n")

    # 2. Modelos paralelos a evaluar
    modelos = [
        {
            "nombre": "CMP",
            "cmd_fn": lambda p: ["./gray_scott_cmp", "--threads", str(p), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "nombre": "SMT (HW Real)",
            "cmd_fn": lambda p: ["./gray_scott_smt", "--threads", str(p), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "nombre": "Coarse-Grained",
            "cmd_fn": lambda p: ["./gray_scott_coarse", "--threads", str(p), "--size", str(args.size), "--steps", str(args.steps)]
        },
        {
            "nombre": "Fine-Grained (Paralelo Cíclico)",
            "cmd_fn": lambda p: ["./gray_scott_fine", "--threads", str(p), "--quantum", "1", "--mode", "parallel", "--size", str(args.size), "--steps", str(args.steps)]
        }
    ]

    resumen_filas = []
    datos_scaling = {"Secuencial": [(1, 1.0, 100.0)]}

    resumen_filas.append([
        "Secuencial", 1, args.runs, f"{t_seq_promedio:.3f}", f"{t_seq_std:.3f}", "1.00x", "100.0%"
    ])

    for mod in modelos:
        nombre = mod["nombre"]
        datos_scaling[nombre] = []
        print(f">>> Evaluando Modelo: [{nombre}]")

        for p in lista_hilos:
            cmd = mod["cmd_fn"](p)
            tiempos_p = []
            print(f"    Hilos = {p:2d} | Ejecutando {args.runs} corridas...", end="", flush=True)

            for r in range(1, args.runs + 1):
                t = ejecutar_comando(cmd, cwd=scripts_dir)
                tiempos_p.append(t)
                if args.cooldown > 0:
                    time.sleep(args.cooldown)

            media = statistics.mean(tiempos_p)
            desv = statistics.stdev(tiempos_p) if len(tiempos_p) > 1 else 0.0
            sp = t_seq_promedio / media
            eff = (sp / p) * 100.0

            datos_scaling[nombre].append((p, sp, eff))
            resumen_filas.append([
                nombre, p, args.runs, f"{media:.3f}", f"{desv:.3f}", f"{sp:.2f}x", f"{eff:.1f}%"
            ])
            print(f" Media: {media:7.2f} ms | Speedup: {sp:5.2f}x | Efic: {eff:5.1f}%")

        print()

    # Guardar en CSV
    headers = ["modelo", "hilos", "corridas", "media_ms", "std_dev_ms", "speedup", "eficiencia"]
    csv_output.parent.mkdir(parents=True, exist_ok=True)
    with open(csv_output, "w", newline="", encoding="utf-8") as f_out:
        writer = csv.writer(f_out)
        writer.writerow(headers)
        writer.writerows(resumen_filas)
    print(f"--> [CSV] Resumen de escalabilidad guardado en: {csv_output}")

    # Guardar también formato compatibilidad
    compat_headers = ["modelo", "threads", "corridas", "tiempo_medio_ms", "std_dev_ms", "speedup", "eficiencia_pct"]
    with open(compat_csv, "w", newline="", encoding="utf-8") as f_comp:
        writer = csv.writer(f_comp)
        writer.writerow(compat_headers)
        for r in resumen_filas:
            writer.writerow([r[0], r[1], r[2], r[3], r[4], r[5].replace("x", ""), r[6].replace("%", "")])

    # Generar gráfica 04
    graficar_strong_scaling_amdahl(datos_scaling, img_output)
    print("\n[OK] Campaña de escalabilidad y generación de 04_escalabilidad_hilos.png finalizada.")


if __name__ == "__main__":
    main()
