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
	./$(TARGET) --size 512 --steps 16000

# Ejecución y ensamblado de video con ffmpeg
run_seq_video: $(TARGET)
	mkdir -p $(TEMP_FRAMES)
	mkdir -p $(VIDEOS_DIR)
	./$(TARGET) --size 512 --steps 16000 --save-frames --output-dir $(TEMP_FRAMES)
	ffmpeg -y -framerate 20 -i $(TEMP_FRAMES)/frame_%d.ppm -vf "scale=1024:1024:flags=neighbor,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Gray-Scott (Sequential Baseline)':fontcolor=white:fontsize=30:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=20,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Step\: %{eif\:n*100\:d}':fontcolor=lime:fontsize=26:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=65,drawtext=fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf:text='Tiempo CPU\: %{eif\:trunc((n*23)/1000)\:d}.%{eif\:trunc(mod(n*23\,1000)/10)\:d\:2} s':fontcolor=yellow:fontsize=26:box=1:boxcolor=black@0.6:boxborderw=5:x=20:y=105" -c:v libx264 -pix_fmt yuv420p $(VIDEOS_DIR)/gray-scott-secuencial.mp4
	rm -rf $(TEMP_FRAMES)

# Limpieza de binarios y temporales
clean:
	rm -f $(TARGET)
	rm -rf $(TEMP_FRAMES)