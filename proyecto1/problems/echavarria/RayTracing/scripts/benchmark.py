#!/usr/bin/env python3
"""
==============================================================================
Framework Experimental de Benchmarking Automatizado - Ray Tracing
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Instituto Tecnológico de Costa Rica (TEC)
==============================================================================
Autor: Emanuel Chavarría Hernández
Módulo: Ray Tracing (Path Tracing de Esferas Monte Carlo)

Este script automatiza la campaña de mediciones estadísticas (200 corridas)
para evaluar rigurosamente los 4 modelos de ejecución concurrente y la línea base:
  1. Secuencial (Baseline en 1 núcleo)
  2. CMP (Chip Multiprocessing - 8 núcleos físicos dedicados)
  3. SMT Hardware (16 hilos con SMT activo por HW)
  4. SMT Software (16 hilos sobre-suscritos con SMT inactivo en kernel)
  5. Coarse-Grained (Ráfagas ininterrumpidas y detección de stalls por rebote)
  6. Fine-Grained Cooperativo (Planificador User-Space Round-Robin Q=1 fila)
  7. Fine-Grained Paralelo Cíclico (POSIX threads intercalados Q=1 fila)

Características clave:
  - Salida estructurada incremental en CSV (sin pérdida de datos por interrupción).
  - Cálculo de estadísticas formales: Media, Desviación Estándar, Mínimo, Máximo,
    Mediana, Speedup vs Secuencial y Eficiencia Paralela.
  - Intervalos de Confianza al 95% calculados rigurosamente con t-Student.
  - Pausa de enfriamiento (--cooldown) para prevenir thermal throttling de la CPU.
  - Soporte opcional para barrido de escalabilidad fuerte (--strong-scaling).
  - Cero dependencias externas: utiliza exclusivamente la biblioteca estándar de Python.
==============================================================================
"""

import sys
import os
import subprocess
import time
import argparse
import csv
import datetime
import statistics
import math
from pathlib import Path


def compilar_binarios(scripts_dir: Path):
    """Compila todos los ejecutables del proyecto mediante make."""
    print("--> [Compilación] Verificando y construyendo binarios con make all...")
    cmd = ["make", "all"]
    res = subprocess.run(cmd, cwd=scripts_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR CRÍTICO] Falló la compilación:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
    print("--> [Compilación] Todos los binarios están actualizados y listos.\n")


def controlar_smt_kernel(estado: str) -> bool:
    """Habilita ('on') o deshabilita ('off') SMT en el kernel de Linux."""
    try:
        cmd = f"echo {estado} | sudo tee /sys/devices/system/cpu/smt/control > /dev/null"
        ret = os.system(cmd)
        return ret == 0
    except Exception as e:
        print(f"[ADVERTENCIA] No se pudo cambiar estado SMT en kernel a '{estado}': {e}", file=sys.stderr)
        return False


def obtener_t_critico_95(df: int) -> float:
    """
    Retorna el valor crítico t (dos colas, alpha=0.05) para los grados de libertad df.
    Aproximación de Student-t para muestras finitas.
    """
    if df <= 0:
        return 1.960
    t_table = {
        1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
        6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
        15: 2.131, 20: 2.086, 25: 2.060, 30: 2.042, 40: 2.021,
        50: 2.009, 60: 2.000, 80: 1.990, 100: 1.984, 120: 1.980,
        150: 1.976, 200: 1.972, 300: 1.968, 500: 1.965
    }
    if df in t_table:
        return t_table[df]
    claves = sorted(t_table.keys())
    for i in range(len(claves) - 1):
        if claves[i] <= df <= claves[i + 1]:
            x0, x1 = claves[i], claves[i + 1]
            y0, y1 = t_table[x0], t_table[x1]
            return y0 + (y1 - y0) * ((df - x0) / (x1 - x0))
    return 1.960


def ejecutar_comando(cmd: list, cwd: Path) -> tuple[float, int, int]:
    """
    Ejecuta el binario C++ y parsea la línea CSV emitida por stderr.
    Retorna (tiempo_ms, eventos_contexto, alto_imagen).
    """
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Comando falló con código {proc.returncode} ({' '.join(cmd)}):\nStderr:\n{proc.stderr}\nStdout:\n{proc.stdout}")

    stderr_lines = proc.stderr.strip().splitlines()
    if not stderr_lines:
        raise RuntimeError(f"No se detectó salida CSV en stderr:\nStdout:\n{proc.stdout}")

    # La última línea de stderr es la salida estructurada de benchmarking
    csv_line = stderr_lines[-1].strip()
    partes = csv_line.split(",")

    modelo_tag = partes[0]
    tiempo_ms = 0.0
    eventos = 0
    height = 225

    if modelo_tag == "sequential":
        # sequential,width,height,samples,depth,elapsed_ms
        height = int(partes[2])
        tiempo_ms = float(partes[5])
        eventos = 0
    elif modelo_tag == "cmp":
        # cmp,threads,width,height,samples,depth,elapsed_ms
        height = int(partes[3])
        tiempo_ms = float(partes[6])
        eventos = 0
    elif modelo_tag == "smt":
        # smt,mode,threads,width,height,samples,depth,smt_status,elapsed_ms
        height = int(partes[4])
        tiempo_ms = float(partes[8])
        eventos = 0
    elif modelo_tag == "coarse":
        # coarse,threads,width,height,samples,depth,burst_pixels,total_stalls,elapsed_ms
        height = int(partes[3])
        eventos = int(partes[7]) # stalls por rebote profundo
        tiempo_ms = float(partes[8])
    elif modelo_tag == "fine":
        # fine,mode,threads,quantum,width,height,samples,depth,switches,elapsed_ms
        height = int(partes[5])
        eventos = int(partes[8]) # conmutaciones de contexto
        tiempo_ms = float(partes[9])
    else:
        raise ValueError(f"Formato CSV no reconocido: {csv_line}")

    return tiempo_ms, eventos, height


def parsear_argumentos():
    parser = argparse.ArgumentParser(
        description="Framework Experimental de Benchmarking para Ray Tracing (200 corridas)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )

    # Parámetros del problema óptico
    parser.add_argument("--width", type=int, default=400, help="Ancho de la imagen en píxeles")
    parser.add_argument("--samples", type=int, default=10, help="Muestras por píxel (Monte Carlo)")
    parser.add_argument("--depth", type=int, default=50, help="Profundidad máxima de rebote del rayo")

    # Parámetros de concurrencia
    parser.add_argument("--threads", type=int, default=8, help="Hilos para CMP, Coarse y Fine-Grained")
    parser.add_argument("--smt-threads", type=int, default=16, help="Hilos para modelo SMT")
    parser.add_argument("--quantum", type=int, default=1, help="Cuantum mínimo para Fine-Grained (filas)")

    # Parámetros de la campaña experimental
    parser.add_argument("--runs", type=int, default=200, help="Cantidad de corridas independientes por modelo")
    parser.add_argument("--cooldown", type=float, default=0.15, help="Pausa en segundos entre corridas (previene thermal throttling)")
    parser.add_argument("--models", type=str, default="all",
                        help="Lista separada por comas: seq, cmp, smt_hw, smt_sw, coarse, fine_coop, fine_parallel o 'all'")
    parser.add_argument("--include-smt-sw", action="store_true", default=True,
                        help="Incluir SMT con apagado real en kernel (requiere sudo)")
    parser.add_argument("--strong-scaling", action="store_true", default=False,
                        help="Ejecutar barrido adicional de escalabilidad fuerte (p = 1, 2, 4, 8, 16 hilos)")

    # Archivos de salida
    parser.add_argument("--output-csv", type=str, default="../metricas/raytracing_benchmark.csv",
                        help="Ruta del CSV con mediciones individuales")
    parser.add_argument("--summary-csv", type=str, default="../metricas/resumen_estadistico.csv",
                        help="Ruta del CSV con resumen estadístico consolidado")
    parser.add_argument("--scaling-csv", type=str, default="../metricas/raytracing_strong_scaling.csv",
                        help="Ruta del CSV para escalabilidad fuerte")
    parser.add_argument("--append", action="store_true", default=False,
                        help="Anexar datos al CSV si ya existe en vez de sobreescribirlo")

    return parser.parse_args()


def main():
    args = parsear_argumentos()

    scripts_dir = Path(__file__).resolve().parent
    base_dir = scripts_dir.parent
    metricas_dir = base_dir / "metricas"
    metricas_dir.mkdir(parents=True, exist_ok=True)

    csv_output_path = (scripts_dir / args.output_csv).resolve()
    summary_output_path = (scripts_dir / args.summary_csv).resolve()
    scaling_output_path = (scripts_dir / args.scaling_csv).resolve()

    # 1. Asegurar compilación previa
    compilar_binarios(scripts_dir)

    # 2. Catálogo formal de modelos de ejecución
    catalogo_modelos = {
        "smt_sw": {
            "nombre": "SMT (Sobre-suscripción SW)",
            "modo": "software_oversubscription",
            "bin": "./raytracing_smt",
            "cmd_args": ["--mode", "sw", "--threads", str(args.smt_threads), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.smt_threads,
            "quantum": 0,
            "requiere_smt_off": True
        },
        "seq": {
            "nombre": "Secuencial",
            "modo": "baseline",
            "bin": "./raytracing_seq",
            "cmd_args": ["--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": 1,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "cmp": {
            "nombre": "CMP",
            "modo": "dedicated_cores",
            "bin": "./raytracing_cmp",
            "cmd_args": ["--threads", str(args.threads), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.threads,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "smt_hw": {
            "nombre": "SMT (HW Real)",
            "modo": "hardware_smt",
            "bin": "./raytracing_smt",
            "cmd_args": ["--mode", "hw", "--threads", str(args.smt_threads), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.smt_threads,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "coarse": {
            "nombre": "Coarse-Grained",
            "modo": "burst_block",
            "bin": "./raytracing_coarse",
            "cmd_args": ["--threads", str(args.threads), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.threads,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "fine_coop": {
            "nombre": "Fine-Grained (Cooperativo)",
            "modo": "cooperative_quantum",
            "bin": "./raytracing_fine",
            "cmd_args": ["--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "cooperative", "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.threads,
            "quantum": args.quantum,
            "requiere_smt_off": False
        },
        "fine_parallel": {
            "nombre": "Fine-Grained (Paralelo Cíclico)",
            "modo": "cyclic_threads",
            "bin": "./raytracing_fine",
            "cmd_args": ["--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "parallel", "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)],
            "hilos": args.threads,
            "quantum": args.quantum,
            "requiere_smt_off": False
        }
    }

    # Filtrar modelos solicitados
    if args.models.strip().lower() == "all":
        modelos_a_ejecutar = list(catalogo_modelos.keys())
    else:
        modelos_a_ejecutar = [m.strip() for m in args.models.split(",") if m.strip()]
        if args.include_smt_sw and "smt_sw" not in modelos_a_ejecutar:
            modelos_a_ejecutar.append("smt_sw")

    for m in modelos_a_ejecutar:
        if m not in catalogo_modelos:
            print(f"[ERROR] Modelo desconocido '{m}'. Opciones válidas: {list(catalogo_modelos.keys())}", file=sys.stderr)
            sys.exit(1)

    print("==============================================================================")
    print("          CAMPAÑA DE BENCHMARKING EXPERIMENTAL - RAY TRACING                 ")
    print("==============================================================================")
    print(f"Resolución de Render: {args.width}x{int(args.width / (16.0 / 9.0))}")
    print(f"Muestras / Píxel    : {args.samples}")
    print(f"Profundidad Rebotes : {args.depth}")
    print(f"Corridas por Modelo : {args.runs}")
    print(f"Cooldown Térmico    : {args.cooldown} s")
    print(f"Modelos Activos     : {', '.join(modelos_a_ejecutar)}")
    print(f"CSV de Medición     : {csv_output_path}")
    print(f"Resumen Estadístico : {summary_output_path}")
    print("==============================================================================\n")

    # Encabezados del CSV individual
    encabezados = [
        "run_id",
        "modelo",
        "modo",
        "hilos",
        "quantum",
        "width",
        "height",
        "samples",
        "depth",
        "eventos_contexto",
        "tiempo_ms",
        "tiempo_s",
        "timestamp"
    ]

    modo_apertura = "a" if (args.append and csv_output_path.exists()) else "w"
    csv_file = open(csv_output_path, modo_apertura, newline="", encoding="utf-8")
    writer = csv.writer(csv_file)
    if modo_apertura == "w":
        writer.writerow(encabezados)
        csv_file.flush()

    resultados_por_modelo = {m: [] for m in modelos_a_ejecutar}
    total_experimentos = len(modelos_a_ejecutar) * args.runs
    exp_global_count = 0
    t_global_inicio = time.time()

    try:
        for m_key in modelos_a_ejecutar:
            cfg = catalogo_modelos[m_key]
            nombre_mod = cfg["nombre"]
            modo_mod = cfg["modo"]
            hilos_mod = cfg["hilos"]
            quantum_mod = cfg["quantum"]
            cmd = [cfg["bin"]] + cfg["cmd_args"]
            smt_off_requerido = cfg["requiere_smt_off"]

            print(f"\n>>> Iniciando bloque de {args.runs} corridas para: [{nombre_mod}] ({modo_mod})")

            # Manejo del kernel de Linux si se requiere SMT OFF real
            if smt_off_requerido:
                print("    --> Deshabilitando SMT físicamente en el kernel (/sys/devices/system/cpu/smt/control)...")
                controlar_smt_kernel("off")
                time.sleep(0.5)

            try:
                for r in range(1, args.runs + 1):
                    exp_global_count += 1
                    t_inicio_run = time.time()

                    # Invocación del binario C++
                    tiempo_ms, eventos, height = ejecutar_comando(cmd, scripts_dir)
                    tiempo_s = tiempo_ms / 1000.0

                    resultados_por_modelo[m_key].append(tiempo_ms)

                    timestamp_iso = datetime.datetime.now().isoformat()
                    writer.writerow([
                        r,
                        nombre_mod,
                        modo_mod,
                        hilos_mod,
                        quantum_mod,
                        args.width,
                        height,
                        args.samples,
                        args.depth,
                        eventos,
                        f"{tiempo_ms:.3f}",
                        f"{tiempo_s:.4f}",
                        timestamp_iso
                    ])
                    csv_file.flush()

                    # Estimación de tiempo restante (ETA)
                    t_transcurrido = time.time() - t_global_inicio
                    t_por_exp = t_transcurrido / exp_global_count
                    exp_restantes = total_experimentos - exp_global_count
                    eta_segundos = exp_restantes * t_por_exp
                    eta_str = str(datetime.timedelta(seconds=int(eta_segundos)))

                    pct = (exp_global_count / total_experimentos) * 100.0
                    print(
                        f"    Corrida {r:3d}/{args.runs} | Tiempo: {tiempo_ms:8.2f} ms | "
                        f"Progreso Total: {pct:5.1f}% | ETA: {eta_str}",
                        end="\r"
                    )

                    if args.cooldown > 0:
                        time.sleep(args.cooldown)

                print() # Salto de línea limpio al terminar las corridas de este modelo

            finally:
                if smt_off_requerido:
                    print("    --> Restaurando SMT a 'on' en el kernel...")
                    controlar_smt_kernel("on")
                    time.sleep(0.5)

    finally:
        csv_file.close()

    # =========================================================================
    # GENERACIÓN DEL RESUMEN ESTADÍSTICO CONSOLIDADO
    # =========================================================================
    tiempo_sec_promedio = None
    if "seq" in resultados_por_modelo and len(resultados_por_modelo["seq"]) > 0:
        tiempo_sec_promedio = statistics.mean(resultados_por_modelo["seq"])

    filas_resumen = []
    print("\n==============================================================================")
    print("                    RESUMEN ESTADÍSTICO CONSOLIDADO                           ")
    print("==============================================================================")
    header_fmt = f"{'Modelo':<30} | {'Media (ms)':>10} | {'StdDev':>8} | {'IC 95% (±)':>10} | {'Speedup':>7} | {'Eficiencia':>10}"
    print(header_fmt)
    print("-" * len(header_fmt))

    for m_key in modelos_a_ejecutar:
        muestras = resultados_por_modelo[m_key]
        if not muestras:
            continue

        cfg = catalogo_modelos[m_key]
        n = len(muestras)
        media = statistics.mean(muestras)
        std_dev = statistics.stdev(muestras) if n > 1 else 0.0
        min_val = min(muestras)
        max_val = max(muestras)
        mediana = statistics.median(muestras)

        # Intervalo de confianza al 95%
        df = n - 1
        t_crit = obtener_t_critico_95(df)
        error_estandar = std_dev / math.sqrt(n) if n > 0 else 0.0
        margen_error = t_crit * error_estandar
        ci_inf = media - margen_error
        ci_sup = media + margen_error

        # Speedup y Eficiencia
        if tiempo_sec_promedio and tiempo_sec_promedio > 0:
            speedup = tiempo_sec_promedio / media
            eficiencia = (speedup / cfg["hilos"]) * 100.0
            speedup_str = f"{speedup:.2f}x"
            eficiencia_str = f"{eficiencia:.1f}%"
        else:
            speedup_str = "1.00x" if m_key == "seq" else "N/A"
            eficiencia_str = "100.0%" if m_key == "seq" else "N/A"

        print(f"{cfg['nombre']:<30} | {media:>10.2f} | {std_dev:>8.2f} | ±{margen_error:>8.2f} | {speedup_str:>7} | {eficiencia_str:>10}")

        filas_resumen.append([
            cfg["nombre"],
            cfg["modo"],
            cfg["hilos"],
            n,
            f"{media:.3f}",
            f"{std_dev:.3f}",
            f"{margen_error:.3f}",
            f"{ci_inf:.3f}",
            f"{ci_sup:.3f}",
            f"{min_val:.3f}",
            f"{max_val:.3f}",
            f"{mediana:.3f}",
            speedup_str,
            eficiencia_str
        ])

    print("==============================================================================\n")

    # Guardar resumen en CSV
    resumen_headers = [
        "modelo",
        "modo",
        "hilos",
        "corridas",
        "media_ms",
        "std_dev_ms",
        "margen_error_ms",
        "ci_95_inf_ms",
        "ci_95_sup_ms",
        "min_ms",
        "max_ms",
        "mediana_ms",
        "speedup",
        "eficiencia"
    ]

    with open(summary_output_path, "w", newline="", encoding="utf-8") as f_sum:
        sum_writer = csv.writer(f_sum)
        sum_writer.writerow(resumen_headers)
        sum_writer.writerows(filas_resumen)

    print(f"[OK] Campaña finalizada. Datos guardados en:\n  -> {csv_output_path}\n  -> {summary_output_path}\n")

    # =========================================================================
    # BARRIDO DE STRONG SCALING (OPCIONAL)
    # =========================================================================
    if args.strong_scaling:
        print("==============================================================================")
        print("           BARRIDO DE ESCALABILIDAD FUERTE (LEY DE AMDAHL)                    ")
        print("==============================================================================")
        hilos_sweep = [1, 2, 4, 8, 16]
        scaling_runs = min(10, args.runs)
        print(f"Hilos a evaluar    : {hilos_sweep}")
        print(f"Corridas por punto : {scaling_runs}")
        print(f"Archivo de Salida  : {scaling_output_path}\n")

        scaling_headers = ["modelo", "hilos", "corridas", "media_ms", "std_dev_ms", "speedup", "eficiencia"]
        scaling_rows = []

        # Línea base 1 hilo (Secuencial)
        muestras_seq = []
        for _ in range(scaling_runs):
            t_ms, _, _ = ejecutar_comando(["./raytracing_seq", "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)], scripts_dir)
            muestras_seq.append(t_ms)
        media_seq_scaling = statistics.mean(muestras_seq)
        std_seq_scaling = statistics.stdev(muestras_seq) if len(muestras_seq) > 1 else 0.0
        scaling_rows.append(["Secuencial", 1, scaling_runs, f"{media_seq_scaling:.3f}", f"{std_seq_scaling:.3f}", "1.00x", "100.0%"])

        # Sweep para CMP
        for th in [2, 4, 8]:
            m_times = []
            for _ in range(scaling_runs):
                t_ms, _, _ = ejecutar_comando(["./raytracing_cmp", "--threads", str(th), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)], scripts_dir)
                m_times.append(t_ms)
            m_avg = statistics.mean(m_times)
            m_std = statistics.stdev(m_times) if len(m_times) > 1 else 0.0
            sp = media_seq_scaling / m_avg
            eff = (sp / th) * 100.0
            scaling_rows.append(["CMP", th, scaling_runs, f"{m_avg:.3f}", f"{m_std:.3f}", f"{sp:.2f}x", f"{eff:.1f}%"])

        # Sweep para SMT
        for th in [2, 4, 8, 16]:
            m_times = []
            for _ in range(scaling_runs):
                t_ms, _, _ = ejecutar_comando(["./raytracing_smt", "--mode", "hw", "--threads", str(th), "--width", str(args.width), "--samples", str(args.samples), "--depth", str(args.depth)], scripts_dir)
                m_times.append(t_ms)
            m_avg = statistics.mean(m_times)
            m_std = statistics.stdev(m_times) if len(m_times) > 1 else 0.0
            sp = media_seq_scaling / m_avg
            eff = (sp / th) * 100.0
            scaling_rows.append(["SMT", th, scaling_runs, f"{m_avg:.3f}", f"{m_std:.3f}", f"{sp:.2f}x", f"{eff:.1f}%"])

        with open(scaling_output_path, "w", newline="", encoding="utf-8") as f_sc:
            sc_writer = csv.writer(f_sc)
            sc_writer.writerow(scaling_headers)
            sc_writer.writerows(scaling_rows)

        print(f"[OK] Barrido de Strong Scaling completado en:\n  -> {scaling_output_path}\n")


if __name__ == "__main__":
    main()
