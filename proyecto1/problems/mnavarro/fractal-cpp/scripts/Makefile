# Automatización Fractal — CE4302. CMake conserva el núcleo único compartido.
SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_DIR ?= $(ROOT)/build
RESULTS_DIR ?= $(ROOT)/results
PYTHON ?= $(if $(wildcard $(ROOT)/.venv/bin/python),$(ROOT)/.venv/bin/python,python3)
JOBS ?= 2
BUILD_TYPE ?= Release
INPUT ?= $(ROOT)/assets/color128.png
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
	cmake -S "$(ROOT)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_APP=ON -DPython3_EXECUTABLE="$(shell command -v "$(PYTHON)")"
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
	@echo 'Uso habitual desde fractal-cpp/:'
	@echo '  make setup   Preparar .venv y dependencias Python (una vez).'
	@echo '  make quick   Diagnostico completo: modelos, escalabilidad, Perf, graficas y ZIP.'
	@echo '  make run     SMT ON y OFF automaticos; benchmark/escalabilidad: 200; Perf: 20. Graficas y ZIP.'
	@echo '  make report  Detectar CSV existentes y generar todas las graficas y el ZIP.'
	@echo 'make run solicita sudo para cambiar SMT y restaurarlo a ON; quick conserva el estado actual.'
	@echo 'Build: build/. Entrada: assets/color128.png. Referencia del ultimo informe: metrics/latest.json.'
	@echo 'Los comandos anteriores siguen disponibles: make help_advanced.'
help_advanced:
	@echo 'Build: all seq fine coarse cmp smt | Tests: test test_unit test_local verify_all verify_cmp verify_smt_on verify_smt_off'
	@echo 'Run: run_seq run_fine run_coarse run_cmp run_smt_on run_smt_off | SMT records: record_smt_on record_smt_off compare_smt'
	@echo 'Options: JOBS BUILD_DIR INPUT RANGE STRIDE ITERATIONS TEAMS THREADS_PER_TEAM SMT_TEAMS SMT_THREADS_PER_TEAM QUANTUM GRAY QUICK TIMEOUT RESULTS_DIR TEST_INPUT'
	@echo 'SMT_STATE=on|off verifica el estado en objetivos individuales; make run automatiza ON/OFF/ON con sudo. Perf: setup_perf profile_fast profile_perf profile_plot test_profiles. Benchmark y graficas: benchmark_full benchmark_fast scalability plot.'
	@echo 'Exportar todos los resultados: archive_metrics (ZIP nuevo en results/exports/metrics.zip; incluye CSV). Perf guarda salida cruda en JSONL; PERF_RAW_STORAGE=files conserva .stat individuales.'
	@echo 'Graficas completas: plot_all (CSV_FILES y PERF_CSV_FILES; directorios de salida distintos). Capturas antiguas: compact_perf PERF_DIR=... REMOVE_STAT_FILES=1 verifica y consolida .stat sin repetir mediciones.'

# Etapa 7: mediciones -> CSV individuales -> tablas/figuras desde CSV.
RUNS ?= 200
FAST_RUNS ?= 5
RUNS_SCALING ?= 200
THREADS_SCALING ?= 1,2,4,8,16
COOLDOWN ?= 0.15
WARMUPS ?= 1
BENCHMARK_MODELS ?= all
BENCH_DIR ?= $(ROOT)/metrics/benchmark-$(SMT_STATE)
FAST_DIR ?= $(ROOT)/metrics/diagnostic-$(SMT_STATE)
SCALING_DIR ?= $(ROOT)/metrics/scaling-$(SMT_STATE)
CSV_FILES ?= "$(BENCH_DIR)/raw.csv"
PLOT_DIR ?= $(BENCH_DIR)/plots
STATS_DIR ?= $(BENCH_DIR)/statistics
BENCH_ARGS = --build-dir "$(BUILD_DIR)" --input "$(INPUT)" --models "$(BENCHMARK_MODELS)" --smt-state $(SMT_STATE) --teams $(TEAMS) --members $(THREADS_PER_TEAM) --smt-teams $(SMT_TEAMS) --smt-members $(SMT_THREADS_PER_TEAM) --range $(RANGE) --stride $(STRIDE) --quantum $(QUANTUM) --cooldown $(COOLDOWN) --warmups $(WARMUPS) --timeout $(TIMEOUT) $(if $(filter 1,$(GRAY)),--gray,)
.PHONY: benchmark benchmark_full benchmark_fast benchmark_on benchmark_off benchmark_scaling benchmark_scaling_fast scalability escalabilidad metrics plot_metrics plot graficas
benchmark: all
	$(PYTHON) "$(ROOT)/scripts/benchmark.py" $(BENCH_ARGS) --runs $(RUNS) --output-dir "$(BENCH_DIR)"
	$(PYTHON) "$(ROOT)/scripts/metrics.py" --csv "$(BENCH_DIR)/raw.csv" --output-dir "$(BENCH_DIR)/statistics" --no-plots
benchmark_full: benchmark
benchmark_fast: all
	$(PYTHON) "$(ROOT)/scripts/benchmark.py" $(BENCH_ARGS) --runs $(FAST_RUNS) --diagnostic --output-dir "$(FAST_DIR)"
	$(PYTHON) "$(ROOT)/scripts/metrics.py" --csv "$(FAST_DIR)/raw.csv" --output-dir "$(FAST_DIR)/statistics" --no-plots
benchmark_on:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" benchmark SMT_STATE=on
benchmark_off:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" benchmark SMT_STATE=off
scalability: all
	$(PYTHON) "$(ROOT)/scripts/scalability.py" $(BENCH_ARGS) --runs $(RUNS_SCALING) --threads $(THREADS_SCALING) --output-dir "$(SCALING_DIR)"
	$(PYTHON) "$(ROOT)/scripts/metrics.py" --csv "$(SCALING_DIR)/raw.csv" --output-dir "$(SCALING_DIR)/statistics" --no-plots
benchmark_scaling: scalability
escalabilidad: scalability
benchmark_scaling_fast: all
	$(PYTHON) "$(ROOT)/scripts/scalability.py" $(BENCH_ARGS) --runs $(FAST_RUNS) --threads $(THREADS_SCALING) --diagnostic --output-dir "$(SCALING_DIR)"
	$(PYTHON) "$(ROOT)/scripts/metrics.py" --csv "$(SCALING_DIR)/raw.csv" --output-dir "$(SCALING_DIR)/statistics" --no-plots
metrics:
	$(PYTHON) "$(ROOT)/scripts/metrics.py" --csv $(CSV_FILES) --output-dir "$(STATS_DIR)" --no-plots
plot_metrics:
	$(PYTHON) "$(ROOT)/scripts/plot_metrics.py" --csv $(CSV_FILES) --output-dir "$(PLOT_DIR)"
plot: plot_metrics
graficas: plot_metrics

# Generar ambos conjuntos desde CSV; no ejecuta compresiones ni mezcla sus estadísticas.
.PHONY: plot_all
plot_all: plot_metrics profile_plot

.PHONY: test_benchmarks
test_benchmarks: all
	$(PYTHON) -c 'import scipy, matplotlib'
	$(PYTHON) "$(ROOT)/tests/test_metrics.py"
	ctest --test-dir "$(BUILD_DIR)" -R 'benchmark_report|benchmark_pipeline' --output-on-failure

# Etapa 8: Perf por proceso, pares de eventos y estado SMT verificado.
PERF ?= perf
PERF_RUNS ?= 20
PERF_FAST_RUNS ?= 1
PERF_MODELS ?= all
PERF_MIN_RUNNING ?= 90
PERF_TIMEOUT ?= 600
PERF_RAW_STORAGE ?= jsonl
PERF_DIR ?= $(ROOT)/metrics/perf-$(SMT_STATE)
PERF_FAST_DIR ?= $(ROOT)/metrics/perf-diagnostic-$(SMT_STATE)
PERF_CHECK_DIR ?= $(ROOT)/metrics/perf-check-$(SMT_STATE)
PERF_CSV_FILES ?= "$(PERF_DIR)/events.csv"
PERF_PLOT_DIR ?= $(ROOT)/metrics/perf-plots
PERF_ARGS = --build-dir "$(BUILD_DIR)" --input "$(INPUT)" --perf "$(PERF)" --raw-storage $(PERF_RAW_STORAGE) --models "$(PERF_MODELS)" --smt-state $(SMT_STATE) --teams $(TEAMS) --members $(THREADS_PER_TEAM) --smt-teams $(SMT_TEAMS) --smt-members $(SMT_THREADS_PER_TEAM) --range $(RANGE) --stride $(STRIDE) --quantum $(QUANTUM) --cooldown $(COOLDOWN) --warmups $(WARMUPS) --timeout $(PERF_TIMEOUT) --min-running $(PERF_MIN_RUNNING) $(if $(filter 1,$(GRAY)),--gray,)
.PHONY: setup_perf profile profile_perf perfilado profile_fast profile_on profile_off profile_plot test_profiles
setup_perf: all
	$(PYTHON) "$(ROOT)/scripts/profile.py" $(PERF_ARGS) --check --output-dir "$(PERF_CHECK_DIR)"
profile_perf: all
	$(PYTHON) "$(ROOT)/scripts/profile.py" $(PERF_ARGS) --runs $(PERF_RUNS) --output-dir "$(PERF_DIR)"
	$(PYTHON) "$(ROOT)/scripts/profile_metrics.py" --csv "$(PERF_DIR)/events.csv" --output-dir "$(PERF_DIR)/statistics" --no-plots
profile: profile_perf
perfilado: profile_perf
profile_fast: all
	$(PYTHON) "$(ROOT)/scripts/profile.py" $(PERF_ARGS) --diagnostic --runs $(PERF_FAST_RUNS) --output-dir "$(PERF_FAST_DIR)"
	$(PYTHON) "$(ROOT)/scripts/profile_metrics.py" --csv "$(PERF_FAST_DIR)/events.csv" --output-dir "$(PERF_FAST_DIR)/statistics" --no-plots
profile_on:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" profile_perf SMT_STATE=on
profile_off:
	$(MAKE) -f "$(ROOT)/scripts/Makefile" profile_perf SMT_STATE=off
profile_plot:
	$(PYTHON) "$(ROOT)/scripts/profile_metrics.py" --csv $(PERF_CSV_FILES) --output-dir "$(PERF_PLOT_DIR)"
test_profiles: all
	$(PYTHON) -c 'import scipy, matplotlib'
	ctest --test-dir "$(BUILD_DIR)" -R '^perf_' --output-on-failure

# Exportar los resultados completos, incluso CSV ignorados por Git. No borra fuentes.
METRICS_DIR ?= $(ROOT)/metrics
METRICS_ARCHIVE ?= $(ROOT)/results/exports/metrics.zip
.PHONY: archive_metrics
archive_metrics:
	$(PYTHON) "$(ROOT)/scripts/archive_metrics.py" --input-dir "$(METRICS_DIR)" --output "$(METRICS_ARCHIVE)"

# Capturas antiguas: copiar primero; retirar .stat sólo por petición explícita.
REMOVE_STAT_FILES ?= 0
.PHONY: compact_perf
compact_perf:
	$(PYTHON) "$(ROOT)/scripts/compact_perf.py" --input-dir "$(PERF_DIR)" $(if $(filter 1,$(REMOVE_STAT_FILES)),--remove-originals,)

# Entrada sencilla: el orquestador reutiliza los runners, validaciones y analizadores.
.PHONY: setup quick run report help_advanced
setup:
	python3 "$(ROOT)/scripts/workflow.py" setup
quick run: all
	$(PYTHON) "$(ROOT)/scripts/workflow.py" $@ --build-dir "$(BUILD_DIR)" --input "$(INPUT)" --metrics-dir "$(METRICS_DIR)" --results-dir "$(RESULTS_DIR)"
report:
	$(PYTHON) "$(ROOT)/scripts/workflow.py" report --metrics-dir "$(METRICS_DIR)" --results-dir "$(RESULTS_DIR)"
