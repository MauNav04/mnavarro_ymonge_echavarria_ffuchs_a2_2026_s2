# ============================================================
# Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
# CE 4302 - Arquitectura de Computadores II
# Compatible con GNU Make (Linux) y MinGW Make (Windows)
# ============================================================

CXX      ?= g++
CXXFLAGS := -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude

OUT_DIR := bin
ifeq ($(OS),Windows_NT)
	EXE     := .exe
	LDFLAGS := -static
	MKDIR_P  = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
	RM_F     = if exist "$(subst /,\,$(1))" del /Q "$(subst /,\,$(1))"
	RUN_BIN  = $(subst /,\,$(1))
else
	EXE     :=
	LDFLAGS :=
	MKDIR_P  = mkdir -p $(1)
	RM_F     = rm -f $(1)
	RUN_BIN  = ./$(1)
endif

ifeq ($(OS),Windows_NT)
	ARCH := $(PROCESSOR_ARCHITECTURE)
else
	ARCH := $(shell uname -m)
endif

ifneq (,$(filter $(ARCH),AMD64 x86_64 amd64 i686))
	SIMD_FLAGS ?= -mavx2
else
	SIMD_FLAGS ?=
endif

CORE_OBJS := $(OUT_DIR)/image_rgba.o $(OUT_DIR)/sobel_sequential.o
SIMD_OBJS := $(OUT_DIR)/sobel_simd.o

SEQ_BIN       := $(OUT_DIR)/sobel_seq$(EXE)
SIMD_BIN      := $(OUT_DIR)/sobel_simd$(EXE)
TEST_BIN      := $(OUT_DIR)/test_sobel_sequential$(EXE)
TEST_SIMD_BIN := $(OUT_DIR)/test_sobel_simd$(EXE)

.PHONY: all build seq seq-save simd simd-save test test-simd clean help

all: build

build: $(SEQ_BIN) $(SIMD_BIN) $(TEST_BIN) $(TEST_SIMD_BIN)

$(OUT_DIR):
	$(call MKDIR_P,$(OUT_DIR))

$(OUT_DIR)/image_rgba.o: src/image_rgba.cpp include/image_rgba.hpp | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT_DIR)/sobel_sequential.o: src/sobel_sequential.cpp include/sobel_sequential.hpp include/image_rgba.hpp | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT_DIR)/sobel_simd.o: src/simd/sobel_simd.cpp include/sobel_simd.hpp include/image_rgba.hpp | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(SIMD_FLAGS) -c $< -o $@

$(SEQ_BIN): src/main.cpp $(CORE_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(SIMD_BIN): src/simd/main_simd.cpp $(CORE_OBJS) $(SIMD_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(SIMD_FLAGS) $< $(CORE_OBJS) $(SIMD_OBJS) $(LDFLAGS) -o $@

$(TEST_BIN): tests/test_sobel_sequential.cpp $(CORE_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(TEST_SIMD_BIN): tests/test_sobel_simd.cpp $(CORE_OBJS) $(SIMD_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(SIMD_FLAGS) $< $(CORE_OBJS) $(SIMD_OBJS) $(LDFLAGS) -o $@

test: $(TEST_BIN) $(TEST_SIMD_BIN)
	$(call RUN_BIN,$(TEST_BIN))
	$(call RUN_BIN,$(TEST_SIMD_BIN))

test-simd: $(TEST_SIMD_BIN)
	$(call RUN_BIN,$(TEST_SIMD_BIN))

seq: $(SEQ_BIN)
	$(call RUN_BIN,$(SEQ_BIN))

seq-save: $(SEQ_BIN)
	$(call RUN_BIN,$(SEQ_BIN)) --width 512 --height 512 --runs 5 --save $(OUT_DIR)/demo

simd: $(SIMD_BIN)
	$(call RUN_BIN,$(SIMD_BIN))

simd-save: $(SIMD_BIN)
	$(call RUN_BIN,$(SIMD_BIN)) --width 512 --height 512 --runs 5 --save $(OUT_DIR)/demo

clean:
	$(call RM_F,$(OUT_DIR)/*.o)
	$(call RM_F,$(SEQ_BIN))
	$(call RM_F,$(SIMD_BIN))
	$(call RM_F,$(TEST_BIN))
	$(call RM_F,$(TEST_SIMD_BIN))
	$(call RM_F,$(OUT_DIR)/demo_*.png)

help:
	@echo Reglas disponibles (desde proyecto_2/):
	@echo   make all       - Compila ejecutables (secuencial + SIMD) y pruebas
	@echo   make test      - Ejecuta todas las pruebas unitarias (secuencial + SIMD)
	@echo   make test-simd - Ejecuta las pruebas unitarias SIMD
	@echo   make seq       - Ejecuta el baseline secuencial (512x512, 1920x1080, 3840x2160)
	@echo   make simd      - Ejecuta el benchmark SIMD y compara speedup vs secuencial
	@echo   make seq-save  - Ejecuta secuencial 512x512 y guarda PNGs en bin/demo_*
	@echo   make simd-save - Ejecuta SIMD 512x512 y guarda PNGs en bin/demo_*
	@echo   make clean     - Limpia binarios e imagenes generadas
