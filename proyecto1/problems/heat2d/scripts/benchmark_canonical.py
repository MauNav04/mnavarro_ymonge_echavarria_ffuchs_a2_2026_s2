#!/usr/bin/env python3
"""
benchmark_canonical.py - Framework de Benchmarking para los 5 Modelos Canónicos de Heat2D.

Ejecuta las configuraciones canónicas estándar:
    1. Secuencial (1 hilo)
    2. CMP (8 hilos)
    3. SMT (16 hilos)
    4. Coarse-Grained (8 hilos)
    5. Fine-Grained (8 hilos)

Guarda todas las salidas de forma limpia y estandarizada en:
    proyecto1/problems/heat2d/metricas/
        ├── heat2d_benchmark.csv        (muestras crudas de las N corridas)
        ├── resumen_estadistico.csv    (resumen estadístico formal Student-t IC 95%)
        ├── heat2d_perf_metrics.csv    (contadores de hardware PMU / Linux perf)
        └── graficas/                  (TODAS las 11 figuras a 300 DPI)

Uso:
    python3 benchmark_canonical.py --runs 200    # Campaña formal completa
    python3 benchmark_canonical.py --runs 3      # Prueba de humo / smoke test
"""

import os
import sys
import csv
import re
import time
import argparse
import subprocess
from pathlib import Path

# Dimensiones estándar del problema (801 x 801, estabilidad CFL dt=5e-6)
DEFAULT_ROWS = 801
DEFAULT_COLS = 801
DEFAULT_STEPS = 1000
DEFAULT_ALPHA = 0.05
DEFAULT_DT = 0.000005

# Actualizaciones de celda interiores por corrida: (rows-2)*(cols-2)*steps = 99 * 99 * 1000 = 9,801,000
CELL_UPDATES = (DEFAULT_ROWS - 2) * (DEFAULT_COLS - 2) * DEFAULT_STEPS


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Benchmark canónico unificado de Heat2D (5 modelos de ejecución)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=200,
        help="Número de corridas independientes por modelo (defecto: 200)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=None,
        help="Directorio destino para las métricas (defecto: proyecto1/problems/heat2d/metricas)",
    )
    parser.add_argument(
        "--cmp-threads",
        type=int,
        default=8,
        help="Hilos para el modelo CMP",
    )
    parser.add_argument(
        "--smt-threads",
        type=int,
        default=16,
        help="Hilos para el modelo SMT",
    )
    parser.add_argument(
        "--coarse-threads",
        type=int,
        default=8,
        help="Hilos para el modelo Coarse-Grained",
    )
    parser.add_argument(
        "--fine-threads",
        type=int,
        default=8,
        help="Hilos / contextos para el modelo Fine-Grained",
    )
    parser.add_argument(
        "--skip-smt-sw",
        action="store_true",
        help="Omitir el modelo SMT (Sobre-suscripción SW)",
    )
    return parser.parse_args()


def controlar_smt_kernel(estado: str) -> bool:
    """Habilita ('on') o deshabilita ('off') SMT en el kernel de Linux."""
    try:
        cmd = f"echo {estado} | sudo tee /sys/devices/system/cpu/smt/control > /dev/null"
        return os.system(cmd) == 0
    except Exception as e:
        print(f"[ADVERTENCIA] No se pudo cambiar estado SMT en kernel a '{estado}': {e}", file=sys.stderr)
        return False


def extraer_tiempo(salida_stdout: str, fallback_tiempo: float) -> float:
    """Extrae 'Execution time: X s' del stdout emitido por el binario C++."""
    match = re.search(r"Execution time:\s*([0-9.]+)\s*s", salida_stdout)
    if match:
        try:
            return float(match.group(1))
        except ValueError:
            pass
    return fallback_tiempo


def main():
    args = parse_arguments()

    scripts_dir = Path(__file__).resolve().parent
    heat2d_dir = scripts_dir.parent
    repo_root = heat2d_dir.parent.parent.parent
    bin_dir = repo_root / "build" / "bin"

    # Directorio estandarizado unificado
    if args.output_dir:
        metricas_dir = Path(args.output_dir).resolve()
    else:
        metricas_dir = heat2d_dir / "metricas"

    plots_dir = metricas_dir / "graficas"
    metricas_dir.mkdir(parents=True, exist_ok=True)
    plots_dir.mkdir(parents=True, exist_ok=True)

    raw_csv_path = metricas_dir / "heat2d_benchmark.csv"
    summary_csv_path = metricas_dir / "resumen_estadistico.csv"
    perf_csv_path = metricas_dir / "heat2d_perf_metrics.csv"

    # Si hay un heat2d_perf_metrics.csv previo en benchmark_results/, copiarlo a metricas/ si no existe
    old_perf_csv = heat2d_dir / "benchmark_results" / "heat2d_perf_metrics.csv"
    if old_perf_csv.exists() and not perf_csv_path.exists():
        try:
            import shutil
            shutil.copy2(old_perf_csv, perf_csv_path)
        except Exception:
            pass

    # Catálogo canónico de modelos de ejecución (incluyendo SMT HW y SMT SW)
    modelos = [
        {
            "id": "sequential",
            "nombre": "Secuencial",
            "threads": 1,
            "cmd": [str(bin_dir / "heat2d_sequential")],
            "requiere_smt_off": False,
        },
        {
            "id": "cmp",
            "nombre": "CMP",
            "threads": args.cmp_threads,
            "cmd": [str(bin_dir / "heat2d_cmp"), str(args.cmp_threads)],
            "requiere_smt_off": False,
        },
        {
            "id": "smt_hw",
            "nombre": "SMT (HW Real)",
            "threads": args.smt_threads,
            "cmd": [str(bin_dir / "heat2d_smt"), str(args.smt_threads)],
            "requiere_smt_off": False,
        },
        {
            "id": "smt_sw",
            "nombre": "SMT (Sobre-suscripción SW)",
            "threads": args.smt_threads,
            "cmd": [str(bin_dir / "heat2d_smt"), str(args.smt_threads)],
            "requiere_smt_off": True,
        },
        {
            "id": "coarse_grained",
            "nombre": "Coarse-Grained",
            "threads": args.coarse_threads,
            "cmd": [str(bin_dir / "heat2d_coarse_grained"), str(args.coarse_threads), "100", "8"],
            "requiere_smt_off": False,
        },
        {
            "id": "fine_grained",
            "nombre": "Fine-Grained",
            "threads": args.fine_threads,
            "cmd": [str(bin_dir / "heat2d_fine_grained"), str(args.fine_threads)],
            "requiere_smt_off": False,
        },
    ]

    if args.skip_smt_sw:
        modelos = [m for m in modelos if not m["requiere_smt_off"]]

    # Verificar existencia previa de binarios
    for m in modelos:
        bin_path = Path(m["cmd"][0])
        if not bin_path.exists():
            print(f"[ERROR] Binario no encontrado: {bin_path}", file=sys.stderr)
            print("Ejecute 'make release' primero.", file=sys.stderr)
            sys.exit(1)

    print("=" * 78)
    print("      BENCHMARK CANÓNICO UNIFICADO - Heat2D")
    print("=" * 78)
    print(f"Corridas por modelo : {args.runs}")
    print(f"Grilla              : {DEFAULT_ROWS} x {DEFAULT_COLS} ({CELL_UPDATES:,} celdas/paso)")
    print(f"Pasos temporales    : {DEFAULT_STEPS}")
    print(f"Directorio métricas : {metricas_dir}")
    print(f"Directorio gráficas : {plots_dir}\n")

    raw_records = []

    # Ejecución de cada modelo
    try:
        for m in modelos:
            smt_off_requerido = m.get("requiere_smt_off", False)
            if smt_off_requerido:
                print("    --> [SMT SW] Deshabilitando SMT en el kernel (/sys/devices/system/cpu/smt/control)...")
                controlar_smt_kernel("off")
                time.sleep(0.5)

            try:
                print(f"[*] Ejecutando: {m['nombre']} ({m['threads']} hilos) [{args.runs} corridas]...")

                # 1 warmup descartado
                subprocess.run(m["cmd"], cwd=repo_root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

                for run_idx in range(1, args.runs + 1):
                    t_start = time.perf_counter()
                    proc = subprocess.run(
                        m["cmd"],
                        cwd=repo_root,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                    t_end = time.perf_counter()

                    if proc.returncode != 0:
                        print(f"[ERROR] Corrida falló para {m['nombre']}:\n{proc.stderr}", file=sys.stderr)
                        sys.exit(1)

                    elapsed_sec = extraer_tiempo(proc.stdout, t_end - t_start)
                    throughput = CELL_UPDATES / elapsed_sec if elapsed_sec > 0 else 0.0

                    raw_records.append({
                        "run": run_idx,
                        "model": m["id"],
                        "logical_threads": m["threads"],
                        "rows": DEFAULT_ROWS,
                        "cols": DEFAULT_COLS,
                        "steps": DEFAULT_STEPS,
                        "alpha": DEFAULT_ALPHA,
                        "dt": DEFAULT_DT,
                        "cell_updates": CELL_UPDATES,
                        "elapsed_seconds": elapsed_sec,
                        "cell_updates_per_second": throughput,
                    })

                    # Imprimir progreso cada 20%
                    salto = max(1, args.runs // 5)
                    if run_idx % salto == 0 or run_idx == args.runs:
                        print(f"    Progreso {m['nombre']}: {run_idx}/{args.runs} (último: {elapsed_sec:.4f} s)")

                print()
            finally:
                if smt_off_requerido:
                    print("    --> [SMT SW] Restaurando SMT a 'on' en el kernel...")
                    controlar_smt_kernel("on")
                    time.sleep(0.5)
    finally:
        # Garantizar que el sistema siempre quede con SMT encendido
        controlar_smt_kernel("on")

    # Escribir heat2d_benchmark.csv
    fieldnames = [
        "run",
        "model",
        "logical_threads",
        "rows",
        "cols",
        "steps",
        "alpha",
        "dt",
        "cell_updates",
        "elapsed_seconds",
        "cell_updates_per_second",
    ]
    with open(raw_csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(raw_records)

    print(f"[OK] Datos crudos guardados en: {raw_csv_path} ({len(raw_records)} filas)")

    # 1. Ejecutar analyze_benchmark.py
    analyze_script = scripts_dir / "analyze_benchmark.py"
    print("\n[*] Ejecutando análisis estadístico formal (Student-t, IC 95%)...")
    res_ana = subprocess.run(
        [sys.executable, str(analyze_script), str(raw_csv_path), "--output", str(summary_csv_path)],
        cwd=repo_root,
    )
    if res_ana.returncode != 0:
        print("[ERROR] Falló analyze_benchmark.py", file=sys.stderr)
        sys.exit(1)

    # 2. Ejecutar plot_benchmark.py
    plot_script = scripts_dir / "plot_benchmark.py"
    print("\n[*] Generando gráficas estandarizadas (300 DPI, paleta CE 4302)...")
    res_plot = subprocess.run(
        [
            sys.executable,
            str(plot_script),
            str(summary_csv_path),
            "--raw",
            str(raw_csv_path),
            "--output-dir",
            str(plots_dir),
        ],
        cwd=repo_root,
    )
    if res_plot.returncode != 0:
        print("[ERROR] Falló plot_benchmark.py", file=sys.stderr)
        sys.exit(1)

    # 3. Si existe métricas de PMU/perf, sincronizar las gráficas de microarquitectura (05, 06, 07, 10)
    profile_script = scripts_dir / "profile_perf.py"
    if perf_csv_path.exists():
        print("\n[*] Sincronizando gráficas de microarquitectura (05, 06, 07, 10) en graficas/...")
        subprocess.run(
            [
                sys.executable,
                str(profile_script),
                "--plot-only",
                "--output-csv",
                str(perf_csv_path),
                "--output-dir",
                str(plots_dir),
            ],
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    print("\n" + "=" * 78)
    print("      CAMPAÑA CANÓNICA FINALIZADA EXITOSAMENTE")
    print("=" * 78)
    print(f"Muestras crudas   : {raw_csv_path}")
    print(f"Resumen analítico : {summary_csv_path}")
    if perf_csv_path.exists():
        print(f"Métricas PMU      : {perf_csv_path}")
    print(f"Carpeta Gráficas  : {plots_dir}")


if __name__ == "__main__":
    main()
