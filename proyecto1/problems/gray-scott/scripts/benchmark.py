#!/usr/bin/env python3
"""
==============================================================================
Framework Experimental de Benchmarking Automatizado - Gray-Scott
CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
Instituto Tecnológico de Costa Rica (TEC)
==============================================================================
Este script automatiza la campaña de mediciones estadísticas (200 corridas)
para evaluar rigurosamente los modelos de ejecución concurrente:
  1. Secuencial (Baseline)
  2. CMP (Chip Multiprocessing - 8 hilos fijados a cores físicos)
  3. SMT Hardware (16 hilos con SMT activo por HW)
  4. SMT Software (16 hilos sobre-suscritos con SMT inactivo en kernel)
  5. Coarse-Grained (Ráfagas ininterrumpidas de bloques grandes)
  6. Fine-Grained Cooperativo (Round-Robin incondicional Q=1 fila)
  7. Fine-Grained Paralelo Cíclico (POSIX threads intercalados Q=1 fila)

Características clave:
  - Salida estructurada incremental en formato CSV (sin pérdida de datos).
  - Cálculo de estadísticas: Media, Desviación Estándar, Mín, Máx, Speedup y Eficiencia.
  - Pausa de enfriamiento (--cooldown) para prevenir thermal throttling de la CPU.
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
from pathlib import Path


def compilar_binarios(scripts_dir: Path):
    """Compila todos los ejecutables del proyecto mediante make."""
    print("--> [Compilación] Verificando y compilando binarios requeridos...")
    cmd = ["make", "all"]
    res = subprocess.run(cmd, cwd=scripts_dir, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Falló la compilación:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
    print("--> [Compilación] Todos los binarios están listos.\n")


def controlar_smt_kernel(estado: str) -> bool:
    """Habilita ('on') o deshabilita ('off') SMT en el kernel de Linux."""
    try:
        cmd = f"echo {estado} | sudo tee /sys/devices/system/cpu/smt/control > /dev/null"
        ret = os.system(cmd)
        return ret == 0
    except Exception as e:
        print(f"[ADVERTENCIA] No se pudo cambiar estado SMT a '{estado}': {e}", file=sys.stderr)
        return False


def ejecutar_comando(cmd: list, cwd: Path) -> tuple[float, int]:
    """
    Ejecuta el binario C++ y parsea la línea CSV emitida por stderr.
    Retorna (tiempo_ms, eventos_contexto).
    """
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Comando falló ({' '.join(cmd)}):\n{proc.stderr}")

    stderr_lines = proc.stderr.strip().splitlines()
    if not stderr_lines:
        raise RuntimeError(f"No se detectó salida CSV en stderr:\nStdout:\n{proc.stdout}")

    # La última línea de stderr es la salida estructurada de benchmarking
    csv_line = stderr_lines[-1].strip()
    partes = csv_line.split(",")

    # Según el modelo, el tiempo en ms y los eventos se ubican en posiciones conocidas:
    modelo_tag = partes[0]
    tiempo_ms = 0.0
    eventos = 0

    if modelo_tag == "sequential":
        # sequential,width,height,steps,time_ms
        tiempo_ms = float(partes[4])
        eventos = 0
    elif modelo_tag == "cmp":
        # cmp,threads,width,height,steps,time_ms
        tiempo_ms = float(partes[5])
        eventos = 0
    elif modelo_tag == "smt":
        # smt,mode,threads,width,height,steps,time_ms
        tiempo_ms = float(partes[6])
        eventos = 0
    elif modelo_tag == "coarse":
        # coarse,threads,width,height,steps,stall_events,time_ms
        eventos = int(partes[5])
        tiempo_ms = float(partes[6])
    elif modelo_tag == "fine":
        # fine,mode,threads,quantum,width,height,steps,context_switches,time_ms
        eventos = int(partes[7])
        tiempo_ms = float(partes[8])
    else:
        # Fallback genérico: el último valor es el tiempo en ms
        tiempo_ms = float(partes[-1])

    return tiempo_ms, eventos


def main():
    parser = argparse.ArgumentParser(
        description="Framework de Benchmarking Masivo Gray-Scott (200 Corridas)"
    )
    parser.add_argument("--runs", type=int, default=200, help="Número de corridas por modelo (defecto: 200)")
    parser.add_argument("--size", type=int, default=512, help="Dimensión de la grilla NxN (defecto: 512)")
    parser.add_argument("--steps", type=int, default=16000, help="Pasos de tiempo (defecto: 16000)")
    parser.add_argument("--threads", type=int, default=8, help="Hilos para CMP/Coarse/Fine (defecto: 8)")
    parser.add_argument("--smt-threads", type=int, default=16, help="Hilos para SMT (defecto: 16)")
    parser.add_argument("--quantum", type=int, default=1, help="Cuantum mínimo para Fine-Grained (defecto: 1 fila)")
    parser.add_argument("--cooldown", type=float, default=0.05, help="Pausa en segundos entre corridas para evitar throttling térmico (defecto: 0.05)")
    parser.add_argument(
        "--models",
        type=str,
        default="seq,cmp,smt_hw,coarse,fine_coop,fine_parallel",
        help="Modelos a evaluar separados por coma: seq,cmp,smt_hw,smt_sw,coarse,fine_coop,fine_parallel (o 'all')"
    )
    parser.add_argument("--include-smt-sw", action="store_true", help="Incluye la prueba de SMT por software (requiere sudo para desactivar SMT)")
    parser.add_argument(
        "--append",
        action="store_true",
        help="Si se especifica, anexa las nuevas corridas al CSV existente. Por defecto (sin este flag), inicia un archivo limpio sobrescribiendo datos previos."
    )
    parser.add_argument(
        "--output-csv",
        type=str,
        default="../metricas/gray_scott_benchmark.csv",
        help="Ruta del CSV de salida para datos crudos"
    )
    parser.add_argument(
        "--summary-csv",
        type=str,
        default="../metricas/resumen_estadistico.csv",
        help="Ruta del CSV para el resumen estadístico consolidado"
    )

    args = parser.parse_args()

    # Rutas base
    script_path = Path(__file__).resolve()
    scripts_dir = script_path.parent
    base_dir = scripts_dir.parent
    metricas_dir = base_dir / "metricas"
    metricas_dir.mkdir(parents=True, exist_ok=True)

    csv_output_path = (scripts_dir / args.output_csv).resolve()
    summary_output_path = (scripts_dir / args.summary_csv).resolve()

    # Asegurar compilación
    compilar_binarios(scripts_dir)

    # Definición de configuraciones de prueba (smt_sw primero para pedir sudo al inicio)
    catalogo_modelos = {
        "smt_sw": {
            "nombre": "SMT (Sobre-suscripción SW)",
            "modo": "software_oversubscription",
            "bin": "./gray_scott_smt",
            "cmd_args": ["--threads", str(args.smt_threads), "--size", str(args.size), "--steps", str(args.steps)],
            "hilos": args.smt_threads,
            "quantum": 0,
            "requiere_smt_off": True
        },
        "seq": {
            "nombre": "Secuencial",
            "modo": "baseline",
            "bin": "./gray_scott_seq",
            "cmd_args": ["--size", str(args.size), "--steps", str(args.steps)],
            "hilos": 1,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "cmp": {
            "nombre": "CMP",
            "modo": "dedicated_cores",
            "bin": "./gray_scott_cmp",
            "cmd_args": ["--threads", str(args.threads), "--size", str(args.size), "--steps", str(args.steps)],
            "hilos": args.threads,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "smt_hw": {
            "nombre": "SMT (HW Real)",
            "modo": "hardware_smt",
            "bin": "./gray_scott_smt",
            "cmd_args": ["--threads", str(args.smt_threads), "--size", str(args.size), "--steps", str(args.steps)],
            "hilos": args.smt_threads,
            "quantum": 0,
            "requiere_smt_off": False
        },

        "coarse": {
            "nombre": "Coarse-Grained",
            "modo": "burst_block",
            "bin": "./gray_scott_coarse",
            "cmd_args": ["--threads", str(args.threads), "--size", str(args.size), "--steps", str(args.steps)],
            "hilos": args.threads,
            "quantum": 0,
            "requiere_smt_off": False
        },
        "fine_coop": {
            "nombre": "Fine-Grained (Cooperativo)",
            "modo": "cooperative_quantum",
            "bin": "./gray_scott_fine",
            "cmd_args": ["--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "cooperative", "--size", str(args.size), "--steps", str(args.steps)],
            "hilos": args.threads,
            "quantum": args.quantum,
            "requiere_smt_off": False
        },
        "fine_parallel": {
            "nombre": "Fine-Grained (Paralelo Cíclico)",
            "modo": "cyclic_threads",
            "bin": "./gray_scott_fine",
            "cmd_args": ["--threads", str(args.threads), "--quantum", str(args.quantum), "--mode", "parallel", "--size", str(args.size), "--steps", str(args.steps)],
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
            print(f"[ERROR] Modelo desconocido: '{m}'. Opciones: {list(catalogo_modelos.keys())}", file=sys.stderr)
            sys.exit(1)

    print("==============================================================================")
    print("           CAMPAÑA DE BENCHMARKING EXPERIMENTAL - GRAY-SCOTT                  ")
    print("==============================================================================")
    print(f"Dimensión Grilla   : {args.size}x{args.size}")
    print(f"Pasos Temporales   : {args.steps}")
    print(f"Corridas por Modelo: {args.runs}")
    print(f"Modelos Seleccionados: {', '.join(modelos_a_ejecutar)}")
    print(f"Archivo de Salida  : {csv_output_path}")
    print(f"Resumen Consolidado: {summary_output_path}")
    print("==============================================================================\n")

    # Inicializar archivo CSV
    encabezados = [
        "run_id",
        "modelo",
        "modo",
        "hilos",
        "quantum",
        "size",
        "steps",
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

            # Manejo de SMT en Linux si el modelo lo requiere
            if smt_off_requerido:
                print("    --> Deshabilitando SMT en el kernel para sobre-suscripción limpia...")
                controlar_smt_kernel("off")
                time.sleep(0.5)

            try:
                for r in range(1, args.runs + 1):
                    exp_global_count += 1
                    t_inicio_run = time.time()

                    # Ejecución del binario
                    tiempo_ms, eventos = ejecutar_comando(cmd, scripts_dir)
                    tiempo_s = tiempo_ms / 1000.0

                    resultados_por_modelo[m_key].append(tiempo_ms)

                    timestamp_iso = datetime.datetime.now().isoformat()
                    writer.writerow([
                        r,
                        nombre_mod,
                        modo_mod,
                        hilos_mod,
                        quantum_mod,
                        args.size,
                        args.steps,
                        eventos,
                        f"{tiempo_ms:.3f}",
                        f"{tiempo_s:.4f}",
                        timestamp_iso
                    ])
                    csv_file.flush()

                    # Estimación de progreso y tiempo restante
                    elapsed_global = time.time() - t_global_inicio
                    avg_per_exp = elapsed_global / exp_global_count
                    remaining_exps = total_experimentos - exp_global_count
                    eta_seconds = avg_per_exp * remaining_exps
                    eta_str = str(datetime.timedelta(seconds=int(eta_seconds)))

                    pct = (exp_global_count / total_experimentos) * 100.0
                    print(
                        f"    Corrida {r:3d}/{args.runs} | Tiempo: {tiempo_ms:8.2f} ms | "
                        f"Progreso Total: {pct:5.1f}% | ETA: {eta_str}",
                        end="\r"
                    )

                    if args.cooldown > 0:
                        time.sleep(args.cooldown)

                print() # Salto de línea al terminar el bloque

            finally:
                if smt_off_requerido:
                    print("    --> Restaurando SMT en el kernel a 'on'...")
                    controlar_smt_kernel("on")
                    time.sleep(0.5)

    finally:
        csv_file.close()

    # =========================================================================
    # GENERACIÓN DEL RESUMEN ESTADÍSTICO
    # =========================================================================
    tiempo_sec_promedio = None
    if "seq" in resultados_por_modelo and len(resultados_por_modelo["seq"]) > 0:
        tiempo_sec_promedio = statistics.mean(resultados_por_modelo["seq"])

    filas_resumen = []
    print("\n==============================================================================")
    print("                         RESUMEN ESTADÍSTICO CONSOLIDADO                      ")
    print("==============================================================================")
    print(f"{'Modelo':<28} | {'N':<4} | {'Media (ms)':<10} | {'StdDev':<9} | {'Mín (ms)':<9} | {'Máx (ms)':<9} | {'Speedup':<8} | {'Eficiencia':<10}")
    print("-" * 102)

    for m_key in modelos_a_ejecutar:
        datos = resultados_por_modelo[m_key]
        if not datos:
            continue

        cfg = catalogo_modelos[m_key]
        nombre = cfg["nombre"]
        n_count = len(datos)
        media = statistics.mean(datos)
        desv = statistics.stdev(datos) if n_count > 1 else 0.0
        min_v = min(datos)
        max_v = max(datos)
        mediana = statistics.median(datos)
        hilos = cfg["hilos"]

        if tiempo_sec_promedio and tiempo_sec_promedio > 0:
            speedup = tiempo_sec_promedio / media
            eficiencia = (speedup / hilos) * 100.0 if hilos > 0 else 0.0
            speedup_str = f"{speedup:.2f}x"
            eficiencia_str = f"{eficiencia:.1f}%"
        else:
            speedup_str = "N/A"
            eficiencia_str = "N/A"

        print(
            f"{nombre:<28} | {n_count:<4} | {media:10.2f} | {desv:9.2f} | {min_v:9.2f} | {max_v:9.2f} | {speedup_str:<8} | {eficiencia_str:<10}"
        )

        filas_resumen.append([
            nombre,
            cfg["modo"],
            hilos,
            n_count,
            round(media, 3),
            round(desv, 3),
            round(min_v, 3),
            round(max_v, 3),
            round(mediana, 3),
            speedup_str,
            eficiencia_str
        ])

    print("==============================================================================\n")

    # Guardar resumen consolidado en CSV
    with open(summary_output_path, "w", newline="", encoding="utf-8") as f_sum:
        w_sum = csv.writer(f_sum)
        w_sum.writerow([
            "modelo", "modo", "hilos", "corridas", "media_ms", "std_dev_ms",
            "min_ms", "max_ms", "mediana_ms", "speedup", "eficiencia"
        ])
        w_sum.writerows(filas_resumen)

    print(f"--> [Métricas] Datos crudos guardados en: {csv_output_path}")
    print(f"--> [Métricas] Resumen estadístico guardado en: {summary_output_path}\n")


if __name__ == "__main__":
    main()
