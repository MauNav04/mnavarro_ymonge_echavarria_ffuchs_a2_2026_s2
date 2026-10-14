# ==============================================================================
# Makefile de Compilación, Benchmarking y Perfilado - Gray-Scott
# CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
# ==============================================================================

# 1. Compilador y banderas de optimización (-O3 obligatorio para benchmarking)
CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17 -I../include -pthread

# 2. Detección automática del entorno virtual de Python con dependencias científicas
VENV_PYTHON ?= $(shell if [ -f ../../../venv/bin/python3 ]; then echo "$$(realpath ../../../venv)/bin/python3"; elif [ -f ../../../../venv/bin/python3 ]; then echo "$$(realpath ../../../../venv)/bin/python3"; else echo python3; fi)

# 3. Rutas relativas desde scripts/
SRC_CORE = ../src/gray_scott_core.cpp
SRC_SEQ = $(SRC_CORE) ../sequential/main.cpp
SRC_CMP = $(SRC_CORE) ../CMP/main.cpp
SRC_SMT = $(SRC_CORE) ../SMT/main.cpp
SRC_COARSE = $(SRC_CORE) ../coarse-grained/main.cpp
SRC_FINE = $(SRC_CORE) ../fine-grained/main.cpp

TARGET_SEQ = gray_scott_seq
TARGET_CMP = gray_scott_cmp
TARGET_SMT = gray_scott_smt
TARGET_COARSE = gray_scott_coarse
TARGET_FINE = gray_scott_fine

VIDEOS_DIR = ../videos
TEMP_FRAMES_SEQ = $(VIDEOS_DIR)/temp_frames_seq
TEMP_FRAMES_CMP = $(VIDEOS_DIR)/temp_frames_cmp
TEMP_FRAMES_SMT = $(VIDEOS_DIR)/temp_frames_smt
TEMP_FRAMES_COARSE = $(VIDEOS_DIR)/temp_frames_coarse
TEMP_FRAMES_FINE = $(VIDEOS_DIR)/temp_frames_fine

# 4. Parámetros estándar para campañas de benchmarking
SIZE ?= 512
STEPS ?= 16000
THREADS ?= 8
SMT_THREADS ?= 16
COARSE_THREADS ?= 8
FINE_THREADS ?= 8
FINE_QUANTUM ?= 1
FINE_MODE ?= cooperative
RUNS ?= 200
RUNS_SCALING ?= 200
THREADS_SCALING ?= 1,2,4,8,16

# Modelos a incluir en el benchmark ('all' o lista separada por comas)
BENCHMARK_MODELS ?= all

# Parámetros para generación de videos demostrativos
VIDEO_SIZE ?= 512
VIDEO_STEPS ?= 16000

.PHONY: all seq cmp smt coarse fine run_sequential run_seq run_cmp verify_cmp run_smt run_smt_sw verify_smt run_coarse verify_coarse run_fine run_fine_parallel verify_fine run_seq_video run_cmp_video run_smt_video run_coarse_video run_fine_video benchmark benchmark_fast benchmark_full scalability escalabilidad scalability_fast scalability_plot profile perfilado profile_perf profile_fast profile_plot setup_perf plot_metrics plot graficas plot_all campaign_smoke campaign_200 clean

# ==============================================================================
# COMPILACIÓN DE EJECUTABLES
# ==============================================================================
all: $(TARGET_SEQ) $(TARGET_CMP) $(TARGET_SMT) $(TARGET_COARSE) $(TARGET_FINE)

seq: $(TARGET_SEQ)
cmp: $(TARGET_CMP)
smt: $(TARGET_SMT)
coarse: $(TARGET_COARSE)
fine: $(TARGET_FINE)

$(TARGET_SEQ): $(SRC_SEQ)
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) -o $(TARGET_SEQ)

$(TARGET_CMP): $(SRC_CMP)
	$(CXX) $(CXXFLAGS) $(SRC_CMP) -o $(TARGET_CMP)

$(TARGET_SMT): $(SRC_SMT)
	$(CXX) $(CXXFLAGS) $(SRC_SMT) -o $(TARGET_SMT)

$(TARGET_COARSE): $(SRC_COARSE)
	$(CXX) $(CXXFLAGS) $(SRC_COARSE) -o $(TARGET_COARSE)

$(TARGET_FINE): $(SRC_FINE)
	$(CXX) $(CXXFLAGS) $(SRC_FINE) -o $(TARGET_FINE)

# ==============================================================================
# EJECUCIÓN DIRECTA Y VERIFICACIÓN NUMÉRICA
# ==============================================================================
run_seq: $(TARGET_SEQ)
	./$(TARGET_SEQ) --size $(SIZE) --steps $(STEPS)

run_sequential: run_seq

run_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS)

verify_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS) --verify

run_smt: $(TARGET_SMT)
	./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS)

run_smt_sw: $(TARGET_SMT)
	@bash -c 'trap "echo on | sudo tee /sys/devices/system/cpu/smt/control > /dev/null" EXIT INT TERM; \
		echo "--> [SMT SW] Deshabilitando SMT en el kernel (/sys/devices/system/cpu/smt/control)..."; \
		echo off | sudo tee /sys/devices/system/cpu/smt/control > /dev/null; \
		echo "--> [SMT SW] Ejecutando 16 hilos sobre 8 núcleos físicos (sobre-suscripción por software)..."; \
		./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS); \
		echo "--> [SMT SW] Restaurando SMT a ON en el hardware..."'

verify_smt: $(TARGET_SMT)
	./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS) --verify

run_coarse: $(TARGET_COARSE)
	./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(SIZE) --steps $(STEPS)

verify_coarse: $(TARGET_COARSE)
	./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(SIZE) --steps $(STEPS) --verify

run_fine: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(SIZE) --steps $(STEPS)

run_fine_parallel: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode parallel --size $(SIZE) --steps $(STEPS)

verify_fine: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(SIZE) --steps $(STEPS) --verify

# ==============================================================================
# GENERACIÓN DE VIDEOS DEMOSTRATIVOS MP4 (Sincronizados con tiempo real de CPU)
# ==============================================================================
run_seq_video: $(TARGET_SEQ)
	@mkdir -p $(TEMP_FRAMES_SEQ) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU (Secuencial)..."
	@TIME_MS=$$(./$(TARGET_SEQ) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) 2>&1 >/dev/null | cut -d',' -f5) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación..." ; \
	./$(TARGET_SEQ) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) --save-frames --output-dir $(TEMP_FRAMES_SEQ) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES_SEQ)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES_SEQ)/frame_%d.ppm \
		-vf "scale=$(VIDEO_SIZE):$(VIDEO_SIZE):flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott (Sequential Baseline)':fontcolor=white:fontsize=18:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=15,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=45,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=72" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-secuencial.mp4
	@rm -rf $(TEMP_FRAMES_SEQ)
	@echo "--> Video secuencial generado con éxito en $(VIDEOS_DIR)/gray-scott-secuencial.mp4"

run_cmp_video: $(TARGET_CMP)
	@mkdir -p $(TEMP_FRAMES_CMP) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU (CMP 8 hilos)..."
	@TIME_MS=$$(./$(TARGET_CMP) --threads $(THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) 2>&1 >/dev/null | cut -d',' -f6) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación con hilos persistentes..." ; \
	./$(TARGET_CMP) --threads $(THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) --save-frames --output-dir $(TEMP_FRAMES_CMP) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES_CMP)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES_CMP)/frame_%d.ppm \
		-vf "scale=$(VIDEO_SIZE):$(VIDEO_SIZE):flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott CMP (8 Cores Físicos)':fontcolor=white:fontsize=18:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=15,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=45,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU (CMP)\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=72" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-cmp.mp4
	@rm -rf $(TEMP_FRAMES_CMP)
	@echo "--> Video CMP generado con éxito en $(VIDEOS_DIR)/gray-scott-cmp.mp4"

run_smt_video: $(TARGET_SMT)
	@mkdir -p $(TEMP_FRAMES_SMT) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU (SMT 16 hilos)..."
	@TIME_MS=$$(./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) 2>&1 >/dev/null | cut -d',' -f7) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación con hilos persistentes..." ; \
	./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) --save-frames --output-dir $(TEMP_FRAMES_SMT) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES_SMT)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES_SMT)/frame_%d.ppm \
		-vf "scale=$(VIDEO_SIZE):$(VIDEO_SIZE):flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott SMT (16 Hilos Lógicos)':fontcolor=white:fontsize=18:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=15,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=45,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU (SMT)\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=72" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-smt.mp4
	@rm -rf $(TEMP_FRAMES_SMT)
	@echo "--> Video SMT generado con éxito en $(VIDEOS_DIR)/gray-scott-smt.mp4"

run_coarse_video: $(TARGET_COARSE)
	@mkdir -p $(TEMP_FRAMES_COARSE) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU (Coarse-Grained)..."
	@TIME_MS=$$(./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) 2>&1 >/dev/null | cut -d',' -f7) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación con ráfagas continuas..." ; \
	./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) --save-frames --output-dir $(TEMP_FRAMES_COARSE) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES_COARSE)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES_COARSE)/frame_%d.ppm \
		-vf "scale=$(VIDEO_SIZE):$(VIDEO_SIZE):flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott Coarse-Grained (Ráfagas)':fontcolor=white:fontsize=18:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=15,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=45,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU (Coarse)\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=72" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-coarse.mp4
	@rm -rf $(TEMP_FRAMES_COARSE)
	@echo "--> Video Coarse-Grained generado con éxito en $(VIDEOS_DIR)/gray-scott-coarse.mp4"

run_fine_video: $(TARGET_FINE)
	@mkdir -p $(TEMP_FRAMES_FINE) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU (Fine-Grained)..."
	@TIME_MS=$$(./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) 2>&1 >/dev/null | cut -d',' -f9) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación con conmutación fina..." ; \
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(VIDEO_SIZE) --steps $(VIDEO_STEPS) --save-frames --output-dir $(TEMP_FRAMES_FINE) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES_FINE)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES_FINE)/frame_%d.ppm \
		-vf "scale=$(VIDEO_SIZE):$(VIDEO_SIZE):flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott Fine-Grained (Q=$(FINE_QUANTUM))':fontcolor=white:fontsize=18:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=15,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=45,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU (Fine)\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=15:box=1:boxcolor=black@0.6:boxborderw=4:x=15:y=72" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-fine.mp4
	@rm -rf $(TEMP_FRAMES_FINE)
	@echo "--> Video Fine-Grained generado con éxito en $(VIDEOS_DIR)/gray-scott-fine.mp4"

# ==============================================================================
# BENCHMARKING ESTADÍSTICO (200 CORRIDAS Y VALIDACIÓN RÁPIDA)
# ==============================================================================
# Campaña completa de 200 corridas para los modelos con hilos estáticos
benchmark: all
	$(VENV_PYTHON) benchmark.py --runs $(RUNS) --size $(SIZE) --steps $(STEPS) --models "$(BENCHMARK_MODELS)"

benchmark_full: benchmark

# Prueba rápida (5 corridas) para validar el pipeline de punta a punta
benchmark_fast: all
	$(VENV_PYTHON) benchmark.py --runs 5 --size $(SIZE) --steps $(STEPS) --models "$(BENCHMARK_MODELS)"

# ==============================================================================
# ESTUDIO DE ESCALABILIDAD FUERTE (STRONG SCALING: 200 CORRIDAS POR HILO)
# ==============================================================================
# Campaña completa: 200 corridas por punto de hilo (1,000 corridas en total)
scalability: all
	$(VENV_PYTHON) scalability.py --runs $(RUNS_SCALING) --threads "$(THREADS_SCALING)" --size $(SIZE) --steps $(STEPS)

escalabilidad: scalability

# Prueba rápida de escalabilidad (3 corridas por punto de hilo)
scalability_fast: all
	$(VENV_PYTHON) scalability.py --runs 3 --threads "$(THREADS_SCALING)" --size $(SIZE) --steps $(STEPS)

# Generar figura 04 directamente desde CSV existente sin re-ejecutar binarios
scalability_plot:
	$(VENV_PYTHON) scalability.py --plot-only

# ==============================================================================
# PERFILADO MICROARQUITECTÓNICO DE HARDWARE (Linux perf / PMU)
# ==============================================================================
setup_perf:
	@echo "--> Habilitando acceso a contadores PMU de hardware (/proc/sys/kernel/perf_event_paranoid)..."
	sudo sysctl -w kernel.perf_event_paranoid=-1

# Campaña completa de perfilado (3 corridas por modelo para promedio)
profile_perf: all
	$(VENV_PYTHON) profile_perf.py --runs 3 --size $(SIZE) --steps $(STEPS)

profile: profile_perf
perfilado: profile_perf

# Perfilado rápido (1 corrida por modelo para validación)
profile_fast: all
	$(VENV_PYTHON) profile_perf.py --runs 1 --size $(SIZE) --steps $(STEPS)

# Generar figuras 05, 06 y 07 directamente desde CSV existente
profile_plot:
	$(VENV_PYTHON) profile_perf.py --plot-only

# ==============================================================================
# GENERACIÓN DE GRÁFICAS DE RENDIMIENTO (300 DPI)
# Genera estrictamente el estándar de las 8 figuras científicas:
#   01_tiempos_ejecucion_boxplot.png
#   02_speedup_comparativo.png
#   03_eficiencia_paralela.png
#   04_escalabilidad_hilos.png
#   05_perf_ipc_comparativo.png
#   06_perf_cache_miss_rates.png
#   07_perf_context_switches.png
#   08_dashboard_consolidado.png
# ==============================================================================
plot_metrics:
	$(VENV_PYTHON) -W ignore plot_metrics.py

plot: plot_metrics
graficas: plot_all

# Regenera las 8 figuras científicas completas a partir de los datos consolidados
plot_all: plot_metrics profile_plot

# ==============================================================================
# PIPELINES DE EJECUCIÓN CONSOLIDADA
# ==============================================================================
# Validación rápida / Smoke test: verifica la generación completa de las 8 figuras
campaign_smoke: all benchmark_fast scalability_fast profile_plot plot_all
	@echo "\n[OK] Smoke test completado. Las 8 figuras científicas han sido generadas y verificadas.\n"

# Campaña experimental masiva completa (200 corridas y 200 corridas/hilo)
campaign_200: all benchmark scalability profile_plot plot_all
	@echo "\n[OK] Campaña completa de 200 corridas finalizada y 8 gráficas generadas exitosamente.\n"

# ==============================================================================
# LIMPIEZA
# ==============================================================================
clean:
	rm -f $(TARGET_SEQ) $(TARGET_CMP) $(TARGET_SMT) $(TARGET_COARSE) $(TARGET_FINE)
	rm -rf $(TEMP_FRAMES_SEQ) $(TEMP_FRAMES_CMP) $(TEMP_FRAMES_SMT) $(TEMP_FRAMES_COARSE) $(TEMP_FRAMES_FINE)