# ============================================================
# Heat2D - Multithreading Experimental Framework
# CE4302 - Arquitectura de Computadores II
#
# Main goals:
#   - Reproducible builds
#   - Automated tests
#   - Automated benchmark campaigns
#   - Reproducible metadata
#   - Separate timing measurements from profiling
# ============================================================

CXX := clang++

HEAT2D := proyecto1/problems/heat2d
INCLUDE := $(HEAT2D)/include
SRC := $(HEAT2D)/src
TESTS := $(HEAT2D)/tests
BENCHMARKS := $(HEAT2D)/benchmarks
SCRIPTS := $(HEAT2D)/scripts

BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin

# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------

COMMON_FLAGS := \
	-std=c++17 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-I$(INCLUDE)

RELEASE_FLAGS := \
	$(COMMON_FLAGS) \
	-O3 \
	-DNDEBUG

TEST_FLAGS := \
	$(COMMON_FLAGS) \
	-O2

PROFILE_FLAGS := \
	$(COMMON_FLAGS) \
	-O3 \
	-g \
	-fno-omit-frame-pointer

# Required by std::thread / mutex / condition_variable.
THREAD_FLAGS := -pthread

# ------------------------------------------------------------
# Python environment
# ------------------------------------------------------------

VENV := .venv
PYTHON := $(VENV)/bin/python
PIP := $(VENV)/bin/pip

# ------------------------------------------------------------
# Common Heat2D sources
# ------------------------------------------------------------

CORE_SRCS := \
	$(SRC)/core/grid.cpp \
	$(SRC)/core/initial_conditions.cpp \
	$(SRC)/core/heat2d.cpp

SEQUENTIAL_SRC := \
	$(SRC)/models/sequential.cpp

FINE_GRAINED_SRCS := \
	$(SRC)/models/fine_grained/logical_thread.cpp \
	$(SRC)/models/fine_grained/round_robin_scheduler.cpp \
	$(SRC)/models/fine_grained/fine_grained.cpp

COARSE_GRAINED_SRCS := \
	$(SRC)/models/coarse_grained/reusable_barrier.cpp \
	$(SRC)/models/coarse_grained/coarse_grained.cpp

# ------------------------------------------------------------
# Executables
# ------------------------------------------------------------

SEQ_BIN := $(BIN_DIR)/heat2d_sequential
FG_BIN := $(BIN_DIR)/heat2d_fine_grained
CG_BIN := $(BIN_DIR)/heat2d_coarse_grained
BENCH_BIN := $(BIN_DIR)/heat2d_benchmark

FG_PROFILE_BIN := $(BIN_DIR)/heat2d_fine_grained_profile
CG_PROFILE_BIN := $(BIN_DIR)/heat2d_coarse_grained_profile

TEST_LOGICAL_BIN := $(BIN_DIR)/test_logical_thread
TEST_SCHEDULER_BIN := $(BIN_DIR)/test_round_robin_scheduler
TEST_FG_BIN := $(BIN_DIR)/test_fine_grained

TEST_BARRIER_BIN := $(BIN_DIR)/test_reusable_barrier
TEST_CG_BIN := $(BIN_DIR)/test_coarse_grained

STRESS_FG_BIN := $(BIN_DIR)/stress_fine_grained
STRESS_CG_BIN := $(BIN_DIR)/stress_coarse_grained

# ------------------------------------------------------------
# Benchmark configuration
# ------------------------------------------------------------

RUNS ?= 200
WARMUPS ?= 5

# Coarse-Grained development/profiling parameters.
#
# IMPORTANT:
# These are NOT yet the formal experimental parameters.
# They are configurable values used for development and perf tests.
CG_THREADS ?= 4
CG_BLOCK_US ?= 100
CG_ROWS_PER_BLOCK ?= 8

# Current formal benchmark:
#   Sequential
#   FG-1
#   FG-2
#   FG-4
#   FG-8
#   FG-16
#   FG-32
#   FG-64
#
# Coarse-Grained has NOT yet been integrated into benchmark_heat2d.cpp.
BENCHMARK_CASES := 15

RESULT_TAG ?= fine_grained_$(shell date +%Y%m%d_%H%M%S)_$(shell uname -m)
RESULT_DIR := $(HEAT2D)/benchmark_results/$(RESULT_TAG)

RAW_RESULTS := $(RESULT_DIR)/raw_results.csv
SUMMARY := $(RESULT_DIR)/summary.csv
PLOTS_DIR := $(RESULT_DIR)/plots
METADATA := $(RESULT_DIR)/metadata.txt
BENCHMARK_LOG := $(RESULT_DIR)/benchmark.log
ANALYSIS_LOG := $(RESULT_DIR)/analysis.log

# ============================================================
# General
# ============================================================

.PHONY: \
	all \
	release \
	tests \
	test \
	test-fg \
	test-cg \
	stress-fg \
	stress-cg \
	python-env \
	python-deps \
	check-clean \
	metadata \
	campaign-dev \
	campaign-fg \
	perf-soft-seq \
	perf-soft-fg \
	perf-soft-cg \
	perf-hw-seq \
	perf-hw-fg \
	perf-hw-cg \
	profile-fg \
	profile-cg \
	help \
	clean

all: release tests

release: \
	$(SEQ_BIN) \
	$(FG_BIN) \
	$(CG_BIN) \
	$(BENCH_BIN)

tests: \
	$(TEST_LOGICAL_BIN) \
	$(TEST_SCHEDULER_BIN) \
	$(TEST_FG_BIN) \
	$(TEST_BARRIER_BIN) \
	$(TEST_CG_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ============================================================
# Sequential
# ============================================================

$(SEQ_BIN): \
	$(SRC)/main.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) \
		$(SRC)/main.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		-o $@

# ============================================================
# Fine-Grained
# ============================================================

$(FG_BIN): \
	$(SRC)/models/fine_grained/main.cpp \
	$(CORE_SRCS) \
	$(FINE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) \
		$(SRC)/models/fine_grained/main.cpp \
		$(CORE_SRCS) \
		$(FINE_GRAINED_SRCS) \
		-o $@

# ============================================================
# Coarse-Grained
# ============================================================

$(CG_BIN): \
	$(SRC)/models/coarse_grained/main.cpp \
	$(CORE_SRCS) \
	$(COARSE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/coarse_grained/main.cpp \
		$(CORE_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		-o $@

# ============================================================
# Benchmark
#
# IMPORTANT:
# This benchmark currently contains only Sequential + Fine-Grained.
# Coarse-Grained will be integrated in a later step.
# ============================================================

$(BENCH_BIN): \
	$(BENCHMARKS)/benchmark_heat2d.cpp \
	$(SRC)/benchmark/benchmark.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(FINE_GRAINED_SRCS) \
	$(COARSE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(BENCHMARKS)/benchmark_heat2d.cpp \
		$(SRC)/benchmark/benchmark.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(FINE_GRAINED_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		-o $@

# ============================================================
# Fine-Grained Unit / Integration Tests
# ============================================================

$(TEST_LOGICAL_BIN): \
	$(TESTS)/fine_grained/test_logical_thread.cpp \
	$(SRC)/models/fine_grained/logical_thread.cpp | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) \
		$(TESTS)/fine_grained/test_logical_thread.cpp \
		$(SRC)/models/fine_grained/logical_thread.cpp \
		-o $@

$(TEST_SCHEDULER_BIN): \
	$(TESTS)/fine_grained/test_round_robin_scheduler.cpp \
	$(SRC)/models/fine_grained/logical_thread.cpp \
	$(SRC)/models/fine_grained/round_robin_scheduler.cpp | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) \
		$(TESTS)/fine_grained/test_round_robin_scheduler.cpp \
		$(SRC)/models/fine_grained/logical_thread.cpp \
		$(SRC)/models/fine_grained/round_robin_scheduler.cpp \
		-o $@

$(TEST_FG_BIN): \
	$(TESTS)/fine_grained/test_fine_grained.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(FINE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) \
		$(TESTS)/fine_grained/test_fine_grained.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(FINE_GRAINED_SRCS) \
		-o $@

# ============================================================
# Coarse-Grained Unit / Integration Tests
# ============================================================

$(TEST_BARRIER_BIN): \
	$(TESTS)/coarse_grained/test_reusable_barrier.cpp \
	$(SRC)/models/coarse_grained/reusable_barrier.cpp | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/coarse_grained/test_reusable_barrier.cpp \
		$(SRC)/models/coarse_grained/reusable_barrier.cpp \
		-o $@

$(TEST_CG_BIN): \
	$(TESTS)/coarse_grained/test_coarse_grained.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(COARSE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/coarse_grained/test_coarse_grained.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(COARSE_GRAINED_SRCS) \
		-o $@

# ============================================================
# Test runners
# ============================================================

test-fg: \
	$(TEST_LOGICAL_BIN) \
	$(TEST_SCHEDULER_BIN) \
	$(TEST_FG_BIN)

	@echo
	@echo "========================================"
	@echo "Running Heat2D Fine-Grained tests"
	@echo "========================================"
	$(TEST_LOGICAL_BIN)
	$(TEST_SCHEDULER_BIN)
	$(TEST_FG_BIN)
	@echo
	@echo "Fine-Grained tests PASSED."

test-cg: \
	$(TEST_BARRIER_BIN) \
	$(TEST_CG_BIN)

	@echo
	@echo "========================================"
	@echo "Running Heat2D Coarse-Grained tests"
	@echo "========================================"
	$(TEST_BARRIER_BIN)
	$(TEST_CG_BIN)
	@echo
	@echo "Coarse-Grained tests PASSED."

test: tests
	@echo
	@echo "========================================"
	@echo "Running all Heat2D tests"
	@echo "========================================"

	@echo
	@echo "--- Fine-Grained ---"
	$(TEST_LOGICAL_BIN)
	$(TEST_SCHEDULER_BIN)
	$(TEST_FG_BIN)

	@echo
	@echo "--- Coarse-Grained ---"
	$(TEST_BARRIER_BIN)
	$(TEST_CG_BIN)

	@echo
	@echo "All Heat2D tests PASSED."

# ============================================================
# Fine-Grained Stress Test
# ============================================================

$(STRESS_FG_BIN): \
	$(TESTS)/fine_grained/stress_fine_grained.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(FINE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) \
		$(TESTS)/fine_grained/stress_fine_grained.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(FINE_GRAINED_SRCS) \
		-o $@

stress-fg: $(STRESS_FG_BIN)
	$(STRESS_FG_BIN) 8 801 1000 3

# ============================================================
# Coarse-Grained Stress Test
# ============================================================

$(STRESS_CG_BIN): \
	$(TESTS)/coarse_grained/stress_coarse_grained.cpp \
	$(CORE_SRCS) \
	$(COARSE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/coarse_grained/stress_coarse_grained.cpp \
		$(CORE_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		-o $@

stress-cg: $(STRESS_CG_BIN)
	$(STRESS_CG_BIN) $(CG_THREADS) 801 1000 3

# ============================================================
# Python
# ============================================================

$(PYTHON):
	python3 -m venv $(VENV)
	$(PYTHON) -m pip install --upgrade pip

python-env: $(PYTHON)

python-deps: python-env
	@$(PYTHON) -c "import scipy, matplotlib" >/dev/null 2>&1 || \
		$(PYTHON) -m pip install scipy matplotlib

# ============================================================
# Repository cleanliness
# ============================================================

check-clean:
	@status="$$(git status --porcelain)"; \
	if [ -n "$$status" ]; then \
		echo "ERROR: Git working tree is not clean."; \
		echo; \
		echo "$$status"; \
		echo; \
		echo "Commit or stash changes before running a formal campaign."; \
		exit 1; \
	fi

# ============================================================
# Experimental metadata
#
# IMPORTANT:
# This metadata still describes the current formal FG campaign.
# ============================================================

metadata: release
	@mkdir -p $(RESULT_DIR)
	@{ \
		echo "=== Heat2D Experimental Metadata ==="; \
		echo; \
		echo "Timestamp:"; \
		date --iso-8601=seconds 2>/dev/null || date; \
		echo; \
		echo "Result tag:"; \
		echo "$(RESULT_TAG)"; \
		echo; \
		echo "Git commit:"; \
		git rev-parse HEAD; \
		echo; \
		echo "Git branch:"; \
		git branch --show-current; \
		echo; \
		echo "Git status:"; \
		git status --porcelain; \
		echo; \
		echo "Compiler:"; \
		$(CXX) --version; \
		echo; \
		echo "Compilation flags:"; \
		echo "$(RELEASE_FLAGS)"; \
		echo; \
		echo "Benchmark binary SHA256:"; \
		sha256sum $(BENCH_BIN) 2>/dev/null || true; \
		echo; \
		echo "Kernel:"; \
		uname -a; \
		echo; \
		echo "Operating system:"; \
		cat /etc/os-release 2>/dev/null || true; \
		echo; \
		echo "Virtualization:"; \
		systemd-detect-virt 2>/dev/null || echo "unknown"; \
		echo; \
		echo "CPU topology:"; \
		lscpu 2>/dev/null || true; \
		echo; \
		echo "Memory:"; \
		free -h 2>/dev/null || true; \
		echo; \
		echo "perf version:"; \
		perf --version 2>/dev/null || echo "perf unavailable"; \
		echo; \
		echo "perf_event_paranoid:"; \
		cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || true; \
		echo; \
		echo "=== Benchmark Configuration ==="; \
		echo "Grid: 801 x 801"; \
		echo "Interior cells: 799 x 799 = 638401"; \
		echo "Steps: 1000"; \
		echo "alpha: 0.05"; \
		echo "dt: 0.000005"; \
		echo "Cell updates per execution: 638401000"; \
		echo "Measured rounds: $(RUNS)"; \
		echo "Warm-up rounds: $(WARMUPS)"; \
		echo "Cases: Sequential + Fine-Grained {1,2,4,8,16,32,64}"; \
		echo "Fine-Grained scheduler: Round-Robin"; \
		echo "Fine-Grained quantum: 1 cell update"; \
	} > $(METADATA)

	@echo "Metadata written to:"
	@echo "  $(METADATA)"

# ============================================================
# Development benchmark
#
# Short campaign used only to validate the complete FG pipeline.
# ============================================================

campaign-dev: RUNS=3
campaign-dev: WARMUPS=1
campaign-dev: campaign-fg

# ============================================================
# Fine-Grained benchmark campaign
#
# Default:
#   200 measured rounds
#   5 warm-up rounds
#
# Produces:
#   metadata.txt
#   raw_results.csv
#   summary.csv
#   benchmark.log
#   analysis.log
#   plots/
# ============================================================

campaign-fg: check-clean release python-deps
	@echo
	@echo "========================================"
	@echo "Heat2D Fine-Grained Campaign"
	@echo "========================================"
	@echo "Result tag : $(RESULT_TAG)"
	@echo "Runs       : $(RUNS)"
	@echo "Warm-ups   : $(WARMUPS)"
	@echo "Output     : $(RESULT_DIR)"
	@echo

	@mkdir -p $(RESULT_DIR)

	@$(MAKE) --no-print-directory \
		metadata \
		RESULT_TAG="$(RESULT_TAG)" \
		RUNS="$(RUNS)" \
		WARMUPS="$(WARMUPS)"

	@echo
	@echo "Running benchmark..."

	$(BENCH_BIN) \
		$(RUNS) \
		$(WARMUPS) \
		$(RAW_RESULTS) | tee $(BENCHMARK_LOG)

	@echo
	@echo "Validating raw sample count..."

	@actual="$$(wc -l < $(RAW_RESULTS))"; \
	expected="$$(( $(RUNS) * $(BENCHMARK_CASES) + 1 ))"; \
	echo "Expected lines: $$expected"; \
	echo "Actual lines:   $$actual"; \
	if [ "$$actual" -ne "$$expected" ]; then \
		echo "ERROR: unexpected number of CSV rows."; \
		exit 1; \
	fi

	@echo
	@echo "Running statistical analysis..."

	$(PYTHON) $(SCRIPTS)/analyze_benchmark.py \
		$(RAW_RESULTS) \
		--output $(SUMMARY) | tee $(ANALYSIS_LOG)

	@echo
	@echo "Generating plots..."

	$(PYTHON) $(SCRIPTS)/plot_benchmark.py \
		$(SUMMARY) \
		--raw $(RAW_RESULTS) \
		--output-dir $(PLOTS_DIR)

	@echo
	@echo "========================================"
	@echo "Campaign completed successfully"
	@echo "========================================"
	@echo "Metadata : $(METADATA)"
	@echo "Raw data : $(RAW_RESULTS)"
	@echo "Summary  : $(SUMMARY)"
	@echo "Plots    : $(PLOTS_DIR)"
	@echo

# ============================================================
# perf - software events
#
# Useful inside UTM and native Linux.
# ============================================================

perf-soft-seq: $(SEQ_BIN)
	sudo perf stat \
		-e task-clock,context-switches,cpu-migrations,page-faults \
		$(SEQ_BIN)

perf-soft-fg: $(FG_BIN)
	sudo perf stat \
		-e task-clock,context-switches,cpu-migrations,page-faults \
		$(FG_BIN) 8

perf-soft-cg: $(CG_BIN)
	sudo perf stat \
		-e task-clock,context-switches,cpu-migrations,page-faults \
		$(CG_BIN) $(CG_THREADS) $(CG_BLOCK_US) $(CG_ROWS_PER_BLOCK)

# ============================================================
# perf - hardware PMU events
#
# Intended for native hardware.
# UTM may report these events as unsupported.
# ============================================================

perf-hw-seq: $(SEQ_BIN)
	sudo perf stat \
		-e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
		$(SEQ_BIN)

perf-hw-fg: $(FG_BIN)
	sudo perf stat \
		-e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
		$(FG_BIN) 8

perf-hw-cg: $(CG_BIN)
	sudo perf stat \
		-e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
		$(CG_BIN) $(CG_THREADS) $(CG_BLOCK_US) $(CG_ROWS_PER_BLOCK)

# ============================================================
# Fine-Grained profiling binary
# ============================================================

$(FG_PROFILE_BIN): \
	$(SRC)/models/fine_grained/main.cpp \
	$(CORE_SRCS) \
	$(FINE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(PROFILE_FLAGS) \
		$(SRC)/models/fine_grained/main.cpp \
		$(CORE_SRCS) \
		$(FINE_GRAINED_SRCS) \
		-o $@

profile-fg: $(FG_PROFILE_BIN)
	sudo perf record \
		-e cpu-clock \
		-g \
		-- $(FG_PROFILE_BIN) 8

	@echo
	@echo "Profiling data written to perf.data"
	@echo "Open it with:"
	@echo "  sudo perf report"

# ============================================================
# Coarse-Grained profiling binary
# ============================================================

$(CG_PROFILE_BIN): \
	$(SRC)/models/coarse_grained/main.cpp \
	$(CORE_SRCS) \
	$(COARSE_GRAINED_SRCS) | $(BIN_DIR)

	$(CXX) $(PROFILE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/coarse_grained/main.cpp \
		$(CORE_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		-o $@

profile-cg: $(CG_PROFILE_BIN)
	sudo perf record \
		-e cpu-clock \
		-g \
		-- $(CG_PROFILE_BIN) $(CG_THREADS) $(CG_BLOCK_US) $(CG_ROWS_PER_BLOCK)

	@echo
	@echo "Profiling data written to perf.data"
	@echo "Open it with:"
	@echo "  sudo perf report"

# ============================================================
# Help
# ============================================================

help:
	@echo
	@echo "Heat2D Makefile"
	@echo
	@echo "Build:"
	@echo "  make release"
	@echo "      Build Sequential, Fine-Grained, Coarse-Grained and benchmark binaries."
	@echo
	@echo "Tests:"
	@echo "  make test"
	@echo "      Compile and run all Fine-Grained and Coarse-Grained tests."
	@echo
	@echo "  make test-fg"
	@echo "      Compile and run only Fine-Grained tests."
	@echo
	@echo "  make test-cg"
	@echo "      Compile and run only Coarse-Grained tests."
	@echo
	@echo "Stress:"
	@echo "  make stress-fg"
	@echo "      Run Fine-Grained stress test."
	@echo
	@echo "  make stress-cg"
	@echo "      Run Coarse-Grained stress test."
	@echo
	@echo "Benchmark:"
	@echo "  make campaign-dev"
	@echo "      3 measured rounds + 1 warm-up for current FG pipeline."
	@echo
	@echo "  make campaign-fg"
	@echo "      200 measured rounds + 5 warm-ups."
	@echo
	@echo "  make campaign-fg RUNS=50 WARMUPS=3"
	@echo "      Custom Fine-Grained campaign."
	@echo
	@echo "  make campaign-fg RESULT_TAG=intel_fg_200"
	@echo "      Custom result directory name."
	@echo
	@echo "Coarse-Grained parameters:"
	@echo "  CG_THREADS=4"
	@echo "      Number of worker threads."
	@echo
	@echo "  CG_BLOCK_US=100"
	@echo "      Synthetic blocking duration in microseconds."
	@echo
	@echo "  CG_ROWS_PER_BLOCK=8"
	@echo "      Interior rows processed between synthetic blocking events."
	@echo
	@echo "Profiling:"
	@echo "  make perf-soft-seq"
	@echo "  make perf-soft-fg"
	@echo "  make perf-soft-cg"
	@echo
	@echo "  make perf-hw-seq"
	@echo "  make perf-hw-fg"
	@echo "  make perf-hw-cg"
	@echo
	@echo "  make profile-fg"
	@echo "  make profile-cg"
	@echo
	@echo "Examples:"
	@echo "  make perf-soft-cg CG_THREADS=4 CG_BLOCK_US=0 CG_ROWS_PER_BLOCK=8"
	@echo "  make perf-soft-cg CG_THREADS=4 CG_BLOCK_US=100 CG_ROWS_PER_BLOCK=8"
	@echo "  make stress-cg CG_THREADS=8"
	@echo
	@echo "Clean:"
	@echo "  make clean"
	@echo "      Deletes build products only."
	@echo "      Benchmark results are preserved."
	@echo

# ============================================================
# Clean
# ============================================================

clean:
	rm -rf $(BUILD_DIR)
	rm -f perf.data