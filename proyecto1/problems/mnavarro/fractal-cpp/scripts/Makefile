# Automatización Fractal — CE4302. CMake conserva el núcleo único compartido.
SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_DIR ?= $(ROOT)/build
RESULTS_DIR ?= $(ROOT)/results
PYTHON ?= python3
JOBS ?= 2
BUILD_TYPE ?= Release
INPUT ?= $(ROOT)/sequential/examples/color128.png
RANGE ?= 8
STRIDE ?= 8
ITERATIONS ?= 80
TEAMS ?= 4
THREADS_PER_TEAM ?= 2
SMT_TEAMS ?= 4
SMT_THREADS_PER_TEAM ?= 4
SMT_STATE ?= on
QUANTUM ?= 1
TIMEOUT ?= 120
GRAY ?= 0
QUICK ?= 0
TEST_INPUT ?=
MODE = $(if $(filter 1,$(GRAY)),compress-gray,compress)
TEST_ARGS = --build-dir "$(BUILD_DIR)" --results-dir "$(RESULTS_DIR)/integration" --smt-state $(SMT_STATE) --timeout $(TIMEOUT) --iterations $(ITERATIONS) --quantum-domains $(QUANTUM) $(if $(filter 1,$(QUICK)),--quick,) $(if $(TEST_INPUT),--input "$(TEST_INPUT)" --range $(RANGE) --stride $(STRIDE),)

.PHONY: all configure seq fine coarse cmp smt help test test_unit test_local test_models verify_all verify_seq verify_fine verify_coarse verify_cmp verify_smt verify_smt_on verify_smt_off run_seq run_sequential run_fine run_coarse run_cmp run_smt run_smt_on run_smt_off topology record_smt_on record_smt_off compare_smt clean
configure:
	cmake -S "$(ROOT)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_APP=ON
all: configure
	cmake --build "$(BUILD_DIR)" --parallel $(JOBS)
seq fine coarse cmp smt: configure
	cmake --build "$(BUILD_DIR)" --target fractal_$@ --parallel $(JOBS)

# test exige todos los modelos; test_local no ejecuta CMP/SMT físicos.
test: all
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure
	$(PYTHON) "$(ROOT)/scripts/test_models.py" $(TEST_ARGS)
test_unit: all
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure -E 'hardware|models_integration'
test_local: all
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure -E hardware
verify_all: all
	$(PYTHON) "$(ROOT)/scripts/test_models.py" $(TEST_ARGS)
test_models: verify_all
verify_seq verify_fine verify_coarse verify_cmp verify_smt: all
	$(PYTHON) "$(ROOT)/scripts/test_models.py" $(TEST_ARGS) --models $(if $(filter verify_seq,$@),sequential,$(if $(filter verify_fine,$@),fine-grained,$(if $(filter verify_coarse,$@),coarse-grained,$(if $(filter verify_cmp,$@),CMP,SMT))))
verify_smt_on:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" verify_smt SMT_STATE=on
verify_smt_off:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" verify_smt SMT_STATE=off

# Compresión, reconstrucción e imagen demostrativa (no benchmark).
define RUN_MODEL
	mkdir -p "$(RESULTS_DIR)/demo/$(1)"
	"$(BUILD_DIR)/fractal_$(1)" $(MODE) "$(INPUT)" "$(RESULTS_DIR)/demo/$(1)/image.frc" --range $(RANGE) --stride $(STRIDE) $(2)
	"$(BUILD_DIR)/fractal_seq" decompress "$(RESULTS_DIR)/demo/$(1)/image.frc" "$(RESULTS_DIR)/demo/$(1)/image.png" --iterations $(ITERATIONS)
endef
run_seq: all
	$(call RUN_MODEL,seq,--teams 1 --threads-per-team 1)
run_sequential: run_seq
run_fine: all
	$(call RUN_MODEL,fine,--teams $(TEAMS) --threads-per-team $(THREADS_PER_TEAM) --quantum-domains $(QUANTUM))
run_coarse: all
	$(call RUN_MODEL,coarse,--teams $(TEAMS) --threads-per-team $(THREADS_PER_TEAM))
run_cmp: all
	$(call RUN_MODEL,cmp,--teams $(TEAMS) --threads-per-team $(THREADS_PER_TEAM) --report-affinity "$(RESULTS_DIR)/demo/cmp/affinity.csv")
run_smt: all
	$(call RUN_MODEL,smt,--teams $(SMT_TEAMS) --threads-per-team $(SMT_THREADS_PER_TEAM) --smt-state $(SMT_STATE) --report-affinity "$(RESULTS_DIR)/demo/smt/affinity.csv")
run_smt_on:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" run_smt SMT_STATE=on
run_smt_off:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" run_smt SMT_STATE=off
topology: all
	"$(BUILD_DIR)/fractal_cmp" --list-cpus
	"$(BUILD_DIR)/fractal_smt" --list-cpus --smt-state $(SMT_STATE)

# Conserva el protocolo ON/OFF ya existente. Cambiar SMT requiere intervención del usuario.
SMT_RECORD_DIR ?= $(RESULTS_DIR)/smt
record_smt_on record_smt_off: all
	$(PYTHON) "$(ROOT)/SMT/verify_smt.py" run --state $(if $(filter record_smt_on,$@),on,off) --input "$(INPUT)" --build-dir "$(BUILD_DIR)" --output-dir "$(SMT_RECORD_DIR)/$(if $(filter record_smt_on,$@),on,off)" --teams $(SMT_TEAMS) --threads-per-team $(SMT_THREADS_PER_TEAM) --range $(RANGE) --stride $(STRIDE) --iterations $(ITERATIONS) --timeout $(TIMEOUT) $(if $(filter 1,$(GRAY)),--gray,)
compare_smt:
	$(PYTHON) "$(ROOT)/SMT/verify_smt.py" compare --on "$(SMT_RECORD_DIR)/on" --off "$(SMT_RECORD_DIR)/off" --output "$(SMT_RECORD_DIR)/comparison.json"
clean:
	cmake --build "$(BUILD_DIR)" --target clean
help:
	@echo 'Build: all seq fine coarse cmp smt | Tests: test test_unit test_local verify_all verify_cmp verify_smt_on verify_smt_off'
	@echo 'Run: run_seq run_fine run_coarse run_cmp run_smt_on run_smt_off | SMT records: record_smt_on record_smt_off compare_smt'
	@echo 'Options: JOBS BUILD_DIR INPUT RANGE STRIDE ITERATIONS TEAMS THREADS_PER_TEAM SMT_TEAMS SMT_THREADS_PER_TEAM QUANTUM GRAY QUICK TIMEOUT RESULTS_DIR TEST_INPUT'
	@echo 'SMT_STATE=on|off verifica el estado; ningun target cambia SMT. Etapas 7/8 no implementadas.'
