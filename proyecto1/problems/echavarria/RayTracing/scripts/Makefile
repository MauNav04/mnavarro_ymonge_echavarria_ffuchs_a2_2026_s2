# ==============================================================================
# Makefile de Compilación y Ejecución - Ray Tracing
# CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
# ==============================================================================

CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17 -pthread

TARGET_SEQ = raytracing_seq
TARGET_CMP = raytracing_cmp

SRC_SEQ = ../sequential/main.cpp
SRC_CMP = ../CMP/main.cpp

IMAGES_DIR = ../images
PPM_SEQ = $(IMAGES_DIR)/output_seq.ppm
PNG_SEQ = $(IMAGES_DIR)/output_seq.png
PPM_CMP = $(IMAGES_DIR)/output_cmp.ppm
PNG_CMP = $(IMAGES_DIR)/output_cmp.png

# Parámetros estándar para pruebas y renderizado
WIDTH ?= 400
SAMPLES ?= 10
DEPTH ?= 50
THREADS ?= 8

.PHONY: all seq cmp run_sequential run_seq run_cmp verify_cmp bench_seq bench_cmp clean

# Compilar todos los binarios
all: $(TARGET_SEQ) $(TARGET_CMP)

seq: $(TARGET_SEQ)
cmp: $(TARGET_CMP)

$(TARGET_SEQ): $(SRC_SEQ) ../src/camera.h ../src/framebuffer.h ../src/scene.h ../src/rtweekend.h
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) -o $(TARGET_SEQ)

$(TARGET_CMP): $(SRC_CMP) ../src/camera.h ../src/framebuffer.h ../src/scene.h ../src/rtweekend.h
	$(CXX) $(CXXFLAGS) $(SRC_CMP) -o $(TARGET_CMP)

# ==============================================================================
# EJECUCIÓN CON GENERACIÓN DE IMAGEN
# ==============================================================================
run_sequential: run_seq
run_seq: $(TARGET_SEQ)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_SEQ) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_SEQ)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_SEQ) $(PNG_SEQ) && rm -f $(PPM_SEQ); \
		echo "--> [IMAGEN GENERADA] Abre y visualiza el render en: $(PNG_SEQ)"; \
	else \
		echo "--> [IMAGEN GENERADA] Abre y visualiza el render en: $(PPM_SEQ)"; \
	fi

run_cmp: $(TARGET_CMP)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_CMP) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_CMP)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_CMP) $(PNG_CMP) && rm -f $(PPM_CMP); \
		echo "--> [IMAGEN GENERADA] Abre y visualiza el render en: $(PNG_CMP)"; \
	else \
		echo "--> [IMAGEN GENERADA] Abre y visualiza el render en: $(PPM_CMP)"; \
	fi

# ==============================================================================
# VERIFICACIÓN NUMÉRICA Y SPEEDUP
# ==============================================================================
verify_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

# ==============================================================================
# BENCHMARK PURO EN MEMORIA RAM (Sin escribir imágenes a disco)
# ==============================================================================
bench_seq: $(TARGET_SEQ)
	./$(TARGET_SEQ) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH)

bench_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH)

# Limpieza de ejecutables e imágenes generadas temporales
clean:
	rm -f $(TARGET_SEQ) $(TARGET_CMP)
	rm -f $(IMAGES_DIR)/output_*.ppm
