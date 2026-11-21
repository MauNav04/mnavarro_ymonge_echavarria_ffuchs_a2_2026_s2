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
	MKDIR_P  = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
	RM_F     = if exist "$(subst /,\,$(1))" del /Q "$(subst /,\,$(1))"
	RUN_BIN  = $(subst /,\,$(1))
else
	EXE     :=
	MKDIR_P  = mkdir -p $(1)
	RM_F     = rm -f $(1)
	RUN_BIN  = ./$(1)
endif

CORE_OBJS := $(OUT_DIR)/image_rgba.o $(OUT_DIR)/sobel_sequential.o

SEQ_BIN  := $(OUT_DIR)/sobel_seq$(EXE)
TEST_BIN := $(OUT_DIR)/test_sobel_sequential$(EXE)

.PHONY: all build seq seq-save test clean help

all: build

build: $(SEQ_BIN) $(TEST_BIN)

$(OUT_DIR):
	$(call MKDIR_P,$(OUT_DIR))

$(OUT_DIR)/image_rgba.o: src/image_rgba.cpp include/image_rgba.hpp | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT_DIR)/sobel_sequential.o: src/sobel_sequential.cpp include/sobel_sequential.hpp include/image_rgba.hpp | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SEQ_BIN): src/main.cpp $(CORE_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $< $(CORE_OBJS) -o $@

$(TEST_BIN): tests/test_sobel_sequential.cpp $(CORE_OBJS) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $< $(CORE_OBJS) -o $@

test: $(TEST_BIN)
	$(call RUN_BIN,$(TEST_BIN))

seq: $(SEQ_BIN)
	$(call RUN_BIN,$(SEQ_BIN))

seq-save: $(SEQ_BIN)
	$(call RUN_BIN,$(SEQ_BIN)) --width 512 --height 512 --runs 5 --save $(OUT_DIR)/demo

clean:
	$(call RM_F,$(OUT_DIR)/*.o)
	$(call RM_F,$(SEQ_BIN))
	$(call RM_F,$(TEST_BIN))
	$(call RM_F,$(OUT_DIR)/demo_*.png)

help:
	@echo Reglas disponibles (desde proyecto_2/):
	@echo   make all       - Compila el ejecutable secuencial y las pruebas
	@echo   make test      - Ejecuta las pruebas unitarias
	@echo   make seq       - Ejecuta el baseline secuencial (512x512, 1920x1080, 3840x2160)
	@echo   make seq-save  - Ejecuta 512x512 y guarda PNGs en bin/demo_*
	@echo   make clean     - Limpia binarios e imagenes generadas
