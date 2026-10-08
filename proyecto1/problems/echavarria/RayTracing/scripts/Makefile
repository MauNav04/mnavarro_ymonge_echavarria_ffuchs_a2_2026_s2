# ==============================================================================
# Makefile de Compilación y Ejecución - Ray Tracing
# CE 4302 - Arquitectura de Computadores II (Semestre II - 2026)
# ==============================================================================

CXX = g++
CXXFLAGS = -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 -pthread

TARGET_SEQ = raytracing_seq
TARGET_CMP = raytracing_cmp
TARGET_SMT = raytracing_smt
TARGET_COARSE = raytracing_coarse
TARGET_FINE = raytracing_fine

SRC_SEQ = ../sequential/main.cpp
SRC_CMP = ../CMP/main.cpp
SRC_SMT = ../SMT/main.cpp
SRC_COARSE = ../coarse-grained/main.cpp
SRC_FINE = ../fine-grained/main.cpp

SRC_HEADERS = ../src/camera.h ../src/framebuffer.h ../src/scene.h ../src/rtweekend.h

IMAGES_DIR = ../images
PPM_SEQ = $(IMAGES_DIR)/output_seq.ppm
PNG_SEQ = $(IMAGES_DIR)/output_seq.png
PPM_CMP = $(IMAGES_DIR)/output_cmp.ppm
PNG_CMP = $(IMAGES_DIR)/output_cmp.png
PPM_SMT = $(IMAGES_DIR)/output_smt.ppm
PNG_SMT = $(IMAGES_DIR)/output_smt.png
PPM_COARSE = $(IMAGES_DIR)/output_coarse.ppm
PNG_COARSE = $(IMAGES_DIR)/output_coarse.png
PPM_FINE = $(IMAGES_DIR)/output_fine.ppm
PNG_FINE = $(IMAGES_DIR)/output_fine.png

# Parámetros estándar para pruebas y renderizado
WIDTH ?= 400
SAMPLES ?= 10
DEPTH ?= 50
THREADS ?= 8
SMT_THREADS ?= 16
QUANTUM ?= 1

.PHONY: all seq cmp smt coarse fine run_sequential run_seq run_cmp run_smt run_smt_sw run_coarse run_fine run_fine_parallel verify_cmp verify_smt verify_coarse verify_fine clean

# Compilar todos los modelos
all: $(TARGET_SEQ) $(TARGET_CMP) $(TARGET_SMT) $(TARGET_COARSE) $(TARGET_FINE)

seq: $(TARGET_SEQ)
cmp: $(TARGET_CMP)
smt: $(TARGET_SMT)
coarse: $(TARGET_COARSE)
fine: $(TARGET_FINE)

$(TARGET_SEQ): $(SRC_SEQ) $(SRC_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) -o $(TARGET_SEQ)

$(TARGET_CMP): $(SRC_CMP) $(SRC_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_CMP) -o $(TARGET_CMP)

$(TARGET_SMT): $(SRC_SMT) $(SRC_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_SMT) -o $(TARGET_SMT)

$(TARGET_COARSE): $(SRC_COARSE) $(SRC_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_COARSE) -o $(TARGET_COARSE)

$(TARGET_FINE): $(SRC_FINE) $(SRC_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_FINE) -o $(TARGET_FINE)

# ==============================================================================
# EJECUCIÓN CON GENERACIÓN DE IMAGEN
# ==============================================================================
run_sequential: run_seq
run_seq: $(TARGET_SEQ)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_SEQ) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_SEQ)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_SEQ) $(PNG_SEQ) && rm -f $(PPM_SEQ); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_SEQ)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_SEQ)"; \
	fi

run_cmp: $(TARGET_CMP)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_CMP) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_CMP)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_CMP) $(PNG_CMP) && rm -f $(PPM_CMP); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_CMP)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_CMP)"; \
	fi

# ==============================================================================
# SMT: ON (Hardware Real) vs OFF (Kernel o Simulado)
# ==============================================================================
# SMT Encendido (Hardware Real - 16 hilos en 16 procesadores lógicos)
run_smt_on: $(TARGET_SMT)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_SMT) --mode hw --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_SMT)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_SMT) $(PNG_SMT) && rm -f $(PPM_SMT); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_SMT)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_SMT)"; \
	fi

run_smt: run_smt_on

# SMT Apagado en Hardware Real (Kernel / sysfs con sudo y trap seguro)
run_smt_off: $(TARGET_SMT)
	@mkdir -p $(IMAGES_DIR)
	@bash -c 'trap "echo on | sudo tee /sys/devices/system/cpu/smt/control > /dev/null" EXIT INT TERM; \
		echo "--> [SMT KERNEL] Deshabilitando SMT físicamente en Linux (/sys/devices/system/cpu/smt/control)..."; \
		echo off | sudo tee /sys/devices/system/cpu/smt/control > /dev/null; \
		echo "--> [SMT KERNEL] Ejecutando 16 hilos con SMT apagado en el hardware..."; \
		./$(TARGET_SMT) --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_SMT); \
		echo "--> [SMT KERNEL] Restaurando SMT a ON en el hardware..."'
	@if [ -f $(PPM_SMT) ] && command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_SMT) $(PNG_SMT) && rm -f $(PPM_SMT); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_SMT)"; \
	fi

# SMT Apagado Simulado por Afinidad (Sin requerir sudo)
run_smt_sw: $(TARGET_SMT)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_SMT) --mode sw --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_SMT)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_SMT) $(PNG_SMT) && rm -f $(PPM_SMT); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_SMT)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_SMT)"; \
	fi

run_smt_kernel_off: run_smt_off

run_coarse: $(TARGET_COARSE)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_COARSE) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_COARSE)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_COARSE) $(PNG_COARSE) && rm -f $(PPM_COARSE); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_COARSE)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_COARSE)"; \
	fi

run_fine: $(TARGET_FINE)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_FINE) --threads $(THREADS) --quantum $(QUANTUM) --mode cooperative --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_FINE)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_FINE) $(PNG_FINE) && rm -f $(PPM_FINE); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_FINE)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_FINE)"; \
	fi

run_fine_coop: run_fine

run_fine_parallel: $(TARGET_FINE)
	@mkdir -p $(IMAGES_DIR)
	./$(TARGET_FINE) --threads $(THREADS) --quantum $(QUANTUM) --mode parallel --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --save-ppm $(PPM_FINE)
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -y -loglevel error -i $(PPM_FINE) $(PNG_FINE) && rm -f $(PPM_FINE); \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PNG_FINE)"; \
	else \
		echo "--> [IMAGEN GENERADA] Visualiza el render en: $(PPM_FINE)"; \
	fi

# ==============================================================================
# VERIFICACIÓN NUMÉRICA Y SPEEDUP
# ==============================================================================
verify_cmp: $(TARGET_CMP)
	./$(TARGET_CMP) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

verify_smt_on: $(TARGET_SMT)
	./$(TARGET_SMT) --mode hw --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

verify_smt: verify_smt_on

verify_smt_off: $(TARGET_SMT)
	@bash -c 'trap "echo on | sudo tee /sys/devices/system/cpu/smt/control > /dev/null" EXIT INT TERM; \
		echo "--> [SMT KERNEL] Deshabilitando SMT físicamente en Linux (/sys/devices/system/cpu/smt/control)..."; \
		echo off | sudo tee /sys/devices/system/cpu/smt/control > /dev/null; \
		echo "--> [SMT KERNEL] Verificando 16 hilos con SMT apagado..."; \
		./$(TARGET_SMT) --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify; \
		echo "--> [SMT KERNEL] Restaurando SMT a ON en el hardware..."'

verify_smt_sw: $(TARGET_SMT)
	./$(TARGET_SMT) --mode sw --threads $(SMT_THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

verify_coarse: $(TARGET_COARSE)
	./$(TARGET_COARSE) --threads $(THREADS) --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

verify_fine: $(TARGET_FINE)
	./$(TARGET_FINE) --threads $(THREADS) --quantum $(QUANTUM) --mode parallel --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --verify

verify_fine_parallel: verify_fine

# ==============================================================================
# CAMPAÑA DE BENCHMARKING EXPERIMENTAL (Python Suite)
# ==============================================================================
benchmark_fast: all
	python3 benchmark.py --runs 5 --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --cooldown 0.05

benchmark_full: all
	python3 benchmark.py --runs 200 --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --cooldown 0.15

benchmark_scaling: all
	python3 benchmark.py --runs 10 --width $(WIDTH) --samples $(SAMPLES) --depth $(DEPTH) --strong-scaling

benchmark_scalling: benchmark_scaling

# ==============================================================================
# LIMPIEZA
# ==============================================================================
clean:
	rm -f $(TARGET_SEQ) $(TARGET_CMP) $(TARGET_SMT) $(TARGET_COARSE) $(TARGET_FINE)
	rm -f $(IMAGES_DIR)/output_*.ppm
