# 1. Compilador y banderas de optimización (-O3 obligatorio para benchmarking)
CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17 -I../include

# 2. Rutas relativas desde scripts/
SRC = ../src/gray_scott_core.cpp ../sequential/main.cpp
TARGET = gray_scott_seq
VIDEOS_DIR = ../videos
TEMP_FRAMES = $(VIDEOS_DIR)/temp_frames

.PHONY: all run_seq run_seq_video clean

# Compilación por defecto
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Ejecución de benchmark puro (sin I/O a disco, ideal para perf y métricas)
run_seq: $(TARGET)
	./$(TARGET) --size 1024 --steps 128000

# ==============================================================================
# TARGET 2: Generar video sincronizado con el tiempo real de benchmark

run_seq_video: $(TARGET)
	@mkdir -p $(TEMP_FRAMES) $(VIDEOS_DIR)
	@echo "--> [1/3] Midiendo tiempo de cómputo puro en CPU..."
	@TIME_MS=$$(./$(TARGET) --size 1024 --steps 128000 2>&1 >/dev/null | cut -d',' -f5) ; \
	echo "--> Tiempo CPU obtenido: $$TIME_MS ms" ; \
	echo "--> [2/3] Exportando fotogramas de la simulación..." ; \
	./$(TARGET) --size 1024 --steps 128000 --save-frames --output-dir $(TEMP_FRAMES) > /dev/null ; \
	TOTAL_FRAMES=$$(ls -1 $(TEMP_FRAMES)/frame_*.ppm | wc -l) ; \
	FACTOR=$$(LC_ALL=C awk -v t="$$TIME_MS" -v f="$$TOTAL_FRAMES" 'BEGIN { printf "%.2f", t / (f > 1 ? f - 1 : 1) }' | tr ',' '.') ; \
	echo "--> Fotogramas: $$TOTAL_FRAMES | Factor temporal: $$FACTOR ms/frame" ; \
	echo "--> [3/3] Compilando video con ffmpeg..." ; \
	ffmpeg -y -loglevel error -framerate 20 -i $(TEMP_FRAMES)/frame_%d.ppm \
		-vf "scale=1024:1024:flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott (Sequential Baseline)':fontcolor=white:fontsize=30:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=20,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=26:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=65,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU\: %{eif\:trunc((n*'$$FACTOR')/1000)\:d}.%{eif\:trunc(mod(n*'$$FACTOR'\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=26:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=105" \
		-c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-secuencial.mp4
	@rm -rf $(TEMP_FRAMES)
	@echo "--> Video generado con éxito en $(VIDEOS_DIR)/gray-scott-secuencial.mp4"

# Limpieza de binarios y temporales
clean:
	rm -f $(TARGET)
	rm -rf $(TEMP_FRAMES)