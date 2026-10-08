#!/usr/bin/env python3
"""
==============================================================================
Estudio Experimental de Escalabilidad Fuerte (Strong Scaling) - Gray-Scott
==============================================================================

Metodología:
  - Fija el tamaño del problema: N=512x512, 16,000 pasos (carga constante).
  - Realiza un barrido sistemático de hilos: p in {1, 2, 4, 8, 16}.
  - Evalúa los 4 modelos de paralelismo:
      * CMP (Chip Multiprocessing con afinidad a núcleos físicos)
      * SMT (Simultaneous Multithreading en hardware real)
      * Coarse-Grained (Multihilo de grano grueso / ráfagas)
      * Fine-Grained (Multihilo de grano fino cíclico)
  - Mide el baseline Secuencial para calcular Speedup empírico: S(p) = T_seq / T(p).
  - Contrasta los resultados contra la Aceleración Lineal Ideal (S = p) y la
    Ley de Amdahl (asumiendo f = 0.98 fracción paralelizable).
  - Exporta:
      * metricas/escalabilidad_hilos.csv (tabla resumen)
      * metricas/escalabilidad_hilos_crudo.csv (todas las corridas)
      * metricas/graficas/07_escalabilidad_hilos.png (gráfico 300 DPI)
==============================================================================
"""

import sys
import os
import subprocess
import time
import argparse
import csv
import statistics
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Paleta cromática corporativa estándar del proyecto (CE 4302)
PALETA_MODELOS = {
    "CMP": "#2563eb",                          # Azul eléctrico
    "SMT (HW Real)": "#059669",                # Verde esmeralda
    "Coarse-Grained": "#7c3aed",               # Morado
    "Fine-Grained (Paralelo Cíclico)": "#0891b2" # Cian oscuro
}

MARCADORES = {
    "CMP": "o",
    "SMT (HW Real)": "s",
    "Coarse-Grained": "^",
    "Fine-Grained (Paralelo Cíclico)": "D"
}


def compilar_binarios(scripts_dir: Path):
    """Verifica y compila los binarios requeridos."""
    print("--> [Compilación] Verificando binarios antes del barrido de escalabilidad...")
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

    # Última línea de stderr es la salida CSV
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
        raise ValueError(f"Etiqueta de modelo desconocida: {modelo_tag}")


def anotar_puntos_finales(ax, puntos, es_porcentaje=False):
    """
    Anota los valores en el extremo derecho (p=16) resolviendo cualquier colisión o solapamiento.
    Utiliza un algoritmo de relajación 1D en coordenadas de datos para asegurar una separación
    vertical mínima absoluta entre cualquier par de etiquetas adyacentes, junto con líneas guía (leader lines).
    """
    if not puntos:
        return

    # Ordenar puntos de mayor a menor según el valor de 'y'
    pts_ordenados = sorted(puntos, key=lambda p: p["y"], reverse=True)
    n = len(pts_ordenados)
    
    y_targets = [p["y"] for p in pts_ordenados]
    # Separación vertical mínima generosa para evitar solapamiento entre cajas
    min_gap = 7.0 if es_porcentaje else 0.90
    
    y_pos = list(y_targets)
    # Pase hacia adelante: empujar hacia abajo si hay solapamiento
    for i in range(1, n):
        if y_pos[i-1] - y_pos[i] < min_gap:
            y_pos[i] = y_pos[i-1] - min_gap

    # Pase hacia atrás para suavizar si el fondo fue empujado
    for i in range(n - 2, -1, -1):
        if y_pos[i] - y_pos[i+1] < min_gap:
            y_pos[i] = y_pos[i+1] + min_gap

    # Ajuste de centrado para minimizar el desplazamiento respecto a los puntos reales
    shift = np.mean(y_targets) - np.mean(y_pos)
    y_pos = [y + shift for y in y_pos]

    for i, pt in enumerate(pts_ordenados):
        y_label = y_pos[i]
        fmt_val = f"{pt['y']:.1f}%" if es_porcentaje else f"{pt['y']:.2f}x"

        ax.annotate(
            fmt_val,
            xy=(pt["x"], pt["y"]),
            xytext=(pt["x"] + 0.65, y_label),
            textcoords="data",
            ha="left",
            va="center",
            fontweight="bold",
            color=pt["color"],
            fontsize=9.5,
            bbox=dict(
                boxstyle="round,pad=0.28",
                facecolor="white",
                edgecolor=pt["color"],
                alpha=0.95,
                linewidth=1.2
            ),
            arrowprops=dict(
                arrowstyle="-",
                color=pt["color"],
                lw=1.1,
                alpha=0.85,
                shrinkA=3,
                shrinkB=3
            )
        )


def graficar_escalabilidad(df_resumen: pd.DataFrame, t_seq_promedio: float, output_img: Path, f_amdahl: float = 0.98):
    """Genera la figura de 2 paneles (Speedup vs Hilos y Eficiencia vs Hilos) en 300 DPI."""
    print(f"--> [Gráficas] Generando visualización de escalabilidad: {output_img.name}...")

    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update({
        "font.family": "sans-serif",
        "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
        "font.size": 11,
        "axes.labelsize": 12,
        "axes.titlesize": 13,
        "axes.titleweight": "bold",
        "xtick.labelsize": 10,
        "ytick.labelsize": 10,
        "legend.fontsize": 10.0,
        "figure.titlesize": 15,
        "figure.titleweight": "bold",
        "figure.dpi": 300,
    })

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))

    hilos_unicos = sorted(df_resumen["threads"].unique())
    x_hilos = np.array(hilos_unicos)

    # -------------------------------------------------------------------------
    # Panel 1: Curvas de Speedup S(p) vs Hilos
    # -------------------------------------------------------------------------
    # Referencia 1: Escalabilidad Lineal Ideal (S = p)
    ax1.plot(x_hilos, x_hilos, linestyle=":", color="#64748b", linewidth=2.0, label="Aceleración Lineal Ideal (S = p)")

    # Referencia 2: Ley de Amdahl teórica: S(p) = 1 / ((1 - f) + f / p)
    s_amdahl = 1.0 / ((1.0 - f_amdahl) + (f_amdahl / x_hilos))
    ax1.plot(x_hilos, s_amdahl, linestyle="--", color="#dc2626", linewidth=2.2, label=f"Ley de Amdahl (f = {f_amdahl*100:.0f}%)")

    # Curvas experimentales por modelo
    modelos_orden = ["CMP", "SMT (HW Real)", "Coarse-Grained", "Fine-Grained (Paralelo Cíclico)"]
    puntos_1 = []
    for modelo in modelos_orden:
        df_mod = df_resumen[df_resumen["modelo"] == modelo].sort_values("threads")
        if df_mod.empty:
            continue
        ult_fila = df_mod.iloc[-1]
        lbl = f"{modelo} (16h: {ult_fila['speedup']:.2f}x)"
        ax1.plot(
            df_mod["threads"],
            df_mod["speedup"],
            marker=MARCADORES.get(modelo, "o"),
            markersize=8,
            linewidth=2.2,
            color=PALETA_MODELOS.get(modelo, "#333333"),
            label=lbl
        )
        puntos_1.append({
            "modelo": modelo,
            "x": ult_fila["threads"],
            "y": ult_fila["speedup"],
            "color": PALETA_MODELOS.get(modelo, "#333333")
        })

    # Anotar valores en p=16 evitando colisiones
    anotar_puntos_finales(ax1, puntos_1, es_porcentaje=False)

    # Marcador de límite de núcleos físicos (8 núcleos)
    ax1.axvline(8, color="#2563eb", linestyle="-.", alpha=0.5, linewidth=1.2)
    ax1.text(8.2, 0.8, "Límite 8 Cores Físicos", color="#1e40af", fontsize=9, fontstyle="italic")

    ax1.set_title("A. Escalabilidad Fuerte: Speedup Relativo $S(p) = T_{seq} / T(p)$\n(Grilla fija 512x512, 16,000 pasos)")
    ax1.set_xlabel("Número de Hilos de Ejecución ($p$)", fontweight="bold")
    ax1.set_ylabel("Speedup Aceleración ($S$)", fontweight="bold")
    ax1.set_xticks(hilos_unicos)
    ax1.set_xlim(0.5, 19.2)
    ax1.set_ylim(0, max(max(x_hilos), max(df_resumen["speedup"])) * 1.1)
    ax1.legend(loc="upper left", frameon=True)

    # -------------------------------------------------------------------------
    # Panel 2: Eficiencia Paralela E(p) = S(p)/p * 100%
    # -------------------------------------------------------------------------
    ax2.axhline(100.0, linestyle=":", color="#16a34a", linewidth=1.8, label="Eficiencia Ideal (100%)")
    
    # Eficiencia de Amdahl
    e_amdahl = (s_amdahl / x_hilos) * 100.0
    ax2.plot(x_hilos, e_amdahl, linestyle="--", color="#dc2626", linewidth=2.0, label=f"Eficiencia Amdahl (f = {f_amdahl*100:.0f}%)")

    puntos_2 = []
    for modelo in modelos_orden:
        df_mod = df_resumen[df_resumen["modelo"] == modelo].sort_values("threads")
        if df_mod.empty:
            continue
        ult_fila = df_mod.iloc[-1]
        lbl = f"{modelo} (16h: {ult_fila['eficiencia_pct']:.1f}%)"
        ax2.plot(
            df_mod["threads"],
            df_mod["eficiencia_pct"],
            marker=MARCADORES.get(modelo, "o"),
            markersize=8,
            linewidth=2.2,
            color=PALETA_MODELOS.get(modelo, "#333333"),
            label=lbl
        )
        puntos_2.append({
            "modelo": modelo,
            "x": ult_fila["threads"],
            "y": ult_fila["eficiencia_pct"],
            "color": PALETA_MODELOS.get(modelo, "#333333")
        })

    # Anotar valores en p=16 evitando colisiones
    anotar_puntos_finales(ax2, puntos_2, es_porcentaje=True)

    ax2.axvline(8, color="#2563eb", linestyle="-.", alpha=0.5, linewidth=1.2)
    ax2.text(8.2, 15, "Límite 8 Cores Físicos", color="#1e40af", fontsize=9, fontstyle="italic")

    ax2.set_title("B. Eficiencia Paralela $E(p) = \\frac{S(p)}{p} \\times 100\\%$\n(Pérdida por contención y barreras)")
    ax2.set_xlabel("Número de Hilos de Ejecución ($p$)", fontweight="bold")
    ax2.set_ylabel("Eficiencia Paralela (%)", fontweight="bold")
    ax2.set_xticks(hilos_unicos)
    ax2.set_xlim(0.5, 19.2)
    ax2.set_ylim(0, 115)
    ax2.legend(loc="upper right", frameon=True)

    fig.suptitle("Estudio de Escalabilidad Fuerte y Ley de Amdahl — Gray-Scott", fontsize=16, fontweight="bold", y=0.99)
    plt.tight_layout()
    output_img.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_img)
    plt.close()
    print(f"--> [Gráficas] Guardado exitoso: {output_img}\n")


def main():
    parser = argparse.ArgumentParser(description="Estudio de Escalabilidad Fuerte variando hilos (CE 4302)")
    parser.add_argument("--runs", type=int, default=5, help="Corridas por cada punto de prueba (defecto: 5)")
    parser.add_argument("--size", type=int, default=512, help="Dimensión de la grilla (defecto: 512)")
    parser.add_argument("--steps", type=int, default=16000, help="Pasos temporales (defecto: 16000)")
    parser.add_argument("--threads", type=str, default="1,2,4,8,16", help="Lista de hilos a barrer (defecto: '1,2,4,8,16')")
    parser.add_argument("--cooldown", type=float, default=0.2, help="Pausa en segundos entre corridas")
    parser.add_argument("--output-csv", type=str, default="../metricas/escalabilidad_hilos.csv", help="CSV resumen")
    parser.add_argument("--output-raw", type=str, default="../metricas/escalabilidad_hilos_crudo.csv", help="CSV crudo")
    parser.add_argument("--output-img", type=str, default="../metricas/graficas/07_escalabilidad_hilos.png", help="Gráfica")
    args = parser.parse_args()

    scripts_dir = Path(__file__).resolve().parent
    base_dir = scripts_dir.parent
    csv_resumen_path = (scripts_dir / args.output_csv).resolve()
    csv_raw_path = (scripts_dir / args.output_raw).resolve()
    img_path = (scripts_dir / args.output_img).resolve()

    compilar_binarios(scripts_dir)

    lista_hilos = [int(p.strip()) for p in args.threads.split(",") if p.strip()]

    print("==============================================================================")
    print("      CAMPAÑA EXPERIMENTAL DE ESCALABILIDAD FUERTE (STRONG SCALING)           ")
    print("==============================================================================")
    print(f"Dimensión Grilla   : {args.size}x{args.size}")
    print(f"Pasos Temporales   : {args.steps}")
    print(f"Corridas por Punto : {args.runs}")
    print(f"Barrido de Hilos   : {lista_hilos}")
    print(f"CSV Resumen        : {csv_resumen_path}")
    print(f"Gráfica Salida     : {img_path}")
    print("==============================================================================\n")

    # 1. Medir Baseline Secuencial
    print(f">>> Midiendo Baseline Secuencial ({args.runs} corridas)...")
    tiempos_seq = []
    for r in range(1, args.runs + 1):
        t = ejecutar_comando(["./gray_scott_seq", "--size", str(args.size), "--steps", str(args.steps)], cwd=scripts_dir)
        tiempos_seq.append(t)
        time.sleep(args.cooldown)

    t_seq_promedio = statistics.mean(tiempos_seq)
    t_seq_mediana = statistics.median(tiempos_seq)
    print(f"    Baseline Secuencial: Media = {t_seq_promedio:.2f} ms | Mediana = {t_seq_mediana:.2f} ms\n")

    # 2. Configurar catálogo de modelos paralelos a evaluar en el barrido
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

    registros_raw = []
    resumen_filas = []

    # Registrar el baseline secuencial en los resultados crudos
    for idx, t in enumerate(tiempos_seq, 1):
        registros_raw.append({
            "modelo": "Secuencial",
            "threads": 1,
            "run_id": idx,
            "tiempo_ms": t
        })

    total_configs = len(modelos) * len(lista_hilos)
    progreso = 0

    for mod in modelos:
        nombre = mod["nombre"]
        print(f">>> Evaluando Modelo: [{nombre}]")

        for p in lista_hilos:
            progreso += 1
            cmd = mod["cmd_fn"](p)
            tiempos_p = []
            print(f"    [{progreso}/{total_configs}] Hilos = {p:2d} | Ejecutando {args.runs} corridas...", end="", flush=True)

            for r in range(1, args.runs + 1):
                t = ejecutar_comando(cmd, cwd=scripts_dir)
                tiempos_p.append(t)
                registros_raw.append({
                    "modelo": nombre,
                    "threads": p,
                    "run_id": r,
                    "tiempo_ms": t
                })
                time.sleep(args.cooldown)

            media = statistics.mean(tiempos_p)
            mediana = statistics.median(tiempos_p)
            desv = statistics.stdev(tiempos_p) if len(tiempos_p) > 1 else 0.0
            speedup = t_seq_promedio / media
            eficiencia = (speedup / p) * 100.0

            # Ley de Amdahl teórica con f = 0.98
            f_amdahl = 0.98
            s_amdahl = 1.0 / ((1.0 - f_amdahl) + (f_amdahl / p))

            print(f" Media: {media:7.2f} ms | Speedup: {speedup:5.2f}x | Efic: {eficiencia:5.1f}%")

            resumen_filas.append({
                "modelo": nombre,
                "threads": p,
                "corridas": args.runs,
                "tiempo_medio_ms": round(media, 3),
                "tiempo_mediana_ms": round(mediana, 3),
                "std_dev_ms": round(desv, 3),
                "speedup": round(speedup, 3),
                "eficiencia_pct": round(eficiencia, 2),
                "amdahl_teorico": round(s_amdahl, 3),
                "lineal_ideal": p
            })

        print()

    # Guardar CSV Crudo
    df_raw = pd.DataFrame(registros_raw)
    csv_raw_path.parent.mkdir(parents=True, exist_ok=True)
    df_raw.to_csv(csv_raw_path, index=False)
    print(f"--> [CSV] Datos crudos guardados en: {csv_raw_path}")

    # Guardar CSV Resumen
    df_resumen = pd.DataFrame(resumen_filas)
    df_resumen.to_csv(csv_resumen_path, index=False)
    print(f"--> [CSV] Resumen de escalabilidad guardado en: {csv_resumen_path}")

    # Generar Visualización 300 DPI
    graficar_escalabilidad(df_resumen, t_seq_promedio, img_path, f_amdahl=0.98)

    print("==============================================================================")
    print("      ¡ESTUDIO DE ESCALABILIDAD FUERTE COMPLETADO EXITOSAMENTE!               ")
    print("==============================================================================\n")


if __name__ == "__main__":
    main()
