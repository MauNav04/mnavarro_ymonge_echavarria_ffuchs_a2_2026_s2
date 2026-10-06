# 1. Compilador y banderas de optimización (-O3 obligatorio para benchmarking)
CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17 -I../include -pthread

# 2. Rutas relativas desde scripts/
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

# 3. Parámetros estándar para campañas de benchmarking (200 corridas estadísticas)
# Calibrados: ~4.0s en secuencial y ~0.8s en CMP (8 hilos) para evitar thermal throttling y tiempos excesivos
SIZE ?= 512
STEPS ?= 16000
THREADS ?= 8
SMT_THREADS ?= 16
COARSE_THREADS ?= 8
FINE_THREADS ?= 8
FINE_QUANTUM ?= 1
FINE_MODE ?= cooperative

# Parámetros para generación de videos demostrativos de alta resolución
VIDEO_SIZE ?= 512
VIDEO_STEPS ?= 16000

.PHONY: all seq cmp smt coarse fine run_seq run_cmp verify_cmp run_smt run_smt_sw verify_smt run_coarse verify_coarse run_fine run_fine_parallel verify_fine run_seq_video run_cmp_video run_smt_video run_coarse_video run_fine_video clean

# Compilación por defecto: todos los ejecutables
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

# Ejecución de benchmark puro secuencial (calibrado para 200 corridas)
run_sequential: $(TARGET_SEQ)
	./$(TARGET_SEQ) --size $(SIZE) --steps $(STEPS)

# Ejecución de benchmark CMP (8 hilos por defecto, calibrado para 200 corridas)
run_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS)

# Verificación numérica cruzada CMP vs Secuencial
verify_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS) --verify

# ==============================================================================
# SMT: Ejecución en Hardware Real (SMT ON - 16 hilos compitiendo en pipeline)
# ==============================================================================
run_smt: $(TARGET_SMT)
	./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS)

# ==============================================================================
# SMT: Ejecución con Sobre-suscripción por Software (SMT OFF -> 16 hilos sobre 8 cores)
# Desactiva SMT en el kernel, corre 16 hilos, y garantiza restaurar SMT a ON con trap
# ==============================================================================
run_smt_sw: $(TARGET_SMT)
	@bash -c 'trap "echo on | sudo tee /sys/devices/system/cpu/smt/control > /dev/null" EXIT INT TERM; \
		echo "--> [SMT SW] Deshabilitando SMT en el kernel (/sys/devices/system/cpu/smt/control)..."; \
		echo off | sudo tee /sys/devices/system/cpu/smt/control > /dev/null; \
		echo "--> [SMT SW] Ejecutando 16 hilos sobre 8 núcleos físicos (sobre-suscripción por software)..."; \
		./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS); \
		echo "--> [SMT SW] Restaurando SMT a ON en el hardware..."'

# Verificación numérica cruzada SMT vs Secuencial
verify_smt: $(TARGET_SMT)
	./$(TARGET_SMT) --threads $(SMT_THREADS) --size $(SIZE) --steps $(STEPS) --verify

# ==============================================================================
# TARGET: Generar video Secuencial sincronizado con el tiempo real de CPU
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

# ==============================================================================
# TARGET: Generar video CMP sincronizado con el tiempo real de CPU (8 núcleos)
# ==============================================================================
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

# ==============================================================================
# TARGET: Generar video SMT sincronizado con el tiempo real de CPU (16 hilos)
# ==============================================================================
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

# ==============================================================================
# Coarse-Grained: Ejecución con ráfagas largas y bloqueo por sincronización/barrera
# ==============================================================================
run_coarse: $(TARGET_COARSE)
	./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(SIZE) --steps $(STEPS)

# Verificación numérica cruzada Coarse-Grained vs Secuencial
verify_coarse: $(TARGET_COARSE)
	./$(TARGET_COARSE) --threads $(COARSE_THREADS) --size $(SIZE) --steps $(STEPS) --verify

# ==============================================================================
# TARGET: Generar video Coarse-Grained sincronizado con el tiempo real de CPU
# ==============================================================================
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

# ==============================================================================
# Fine-Grained: Planificación cooperativa o cíclica con conmutación incondicional
# ==============================================================================
run_fine: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(SIZE) --steps $(STEPS)

run_fine_parallel: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode parallel --size $(SIZE) --steps $(STEPS)

# Verificación numérica cruzada Fine-Grained vs Secuencial
verify_fine: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(FINE_THREADS) --quantum $(FINE_QUANTUM) --mode $(FINE_MODE) --size $(SIZE) --steps $(STEPS) --verify

# ==============================================================================
# TARGET: Generar video Fine-Grained sincronizado con el tiempo real de CPU
# ==============================================================================
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

# Limpieza de binarios y temporales
clean:
	rm -f $(TARGET_SEQ) $(TARGET_CMP) $(TARGET_SMT) $(TARGET_COARSE) $(TARGET_FINE)
	rm -rf $(TEMP_FRAMES_SEQ) $(TEMP_FRAMES_CMP) $(TEMP_FRAMES_SMT) $(TEMP_FRAMES_COARSE) $(TEMP_FRAMES_FINE)