# 1. Compilador y banderas de optimización (-O3 obligatorio para benchmarking)
CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17 -I../include -pthread

# 2. Rutas relativas desde scripts/
SRC_CORE = ../src/gray_scott_core.cpp
SRC_SEQ = $(SRC_CORE) ../sequential/main.cpp
SRC_CMP = $(SRC_CORE) ../CMP/main.cpp

TARGET_SEQ = gray_scott_seq
TARGET_CMP = gray_scott_cmp
VIDEOS_DIR = ../videos
TEMP_FRAMES_SEQ = $(VIDEOS_DIR)/temp_frames_seq
TEMP_FRAMES_CMP = $(VIDEOS_DIR)/temp_frames_cmp

# 3. Parámetros estándar para campañas de benchmarking (200 corridas estadísticas)
# Calibrados: ~4.0s en secuencial y ~0.8s en CMP (8 hilos) para evitar thermal throttling y tiempos excesivos
SIZE ?= 512
STEPS ?= 16000
THREADS ?= 8

# Parámetros para generación de videos demostrativos de alta resolución
VIDEO_SIZE ?= 512
VIDEO_STEPS ?= 16000

.PHONY: all seq cmp run_seq run_cmp verify_cmp run_seq_video run_cmp_video clean

# Compilación por defecto: ambos ejecutables
all: $(TARGET_SEQ) $(TARGET_CMP)

seq: $(TARGET_SEQ)
cmp: $(TARGET_CMP)

$(TARGET_SEQ): $(SRC_SEQ)
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) -o $(TARGET_SEQ)

$(TARGET_CMP): $(SRC_CMP)
	$(CXX) $(CXXFLAGS) $(SRC_CMP) -o $(TARGET_CMP)

# Ejecución de benchmark puro secuencial (calibrado para 200 corridas)
run_seq: $(TARGET_SEQ)
	./$(TARGET_SEQ) --size $(SIZE) --steps $(STEPS)

# Ejecución de benchmark CMP (8 hilos por defecto, calibrado para 200 corridas)
run_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS)

# Verificación numérica cruzada (compara CMP vs Secuencial celda a celda)
verify_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --size $(SIZE) --steps $(STEPS) --verify

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

# Limpieza de binarios y temporales
clean:
	rm -f $(TARGET_SEQ) $(TARGET_CMP)
	rm -rf $(TEMP_FRAMES_SEQ) $(TEMP_FRAMES_CMP)