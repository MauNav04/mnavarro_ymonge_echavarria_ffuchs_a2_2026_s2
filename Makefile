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

# Reusable synchronization primitive currently stored inside the
# Coarse-Grained module. CMP temporarily reuses this implementation.
REUSABLE_BARRIER_SRC := \
	$(SRC)/models/coarse_grained/reusable_barrier.cpp

COARSE_GRAINED_SRCS := \
	$(REUSABLE_BARRIER_SRC) \
	$(SRC)/models/coarse_grained/coarse_grained.cpp

CMP_SRCS := \
	$(SRC)/models/cmp/cmp.cpp

# ------------------------------------------------------------
# Executables
# ------------------------------------------------------------

SEQ_BIN := $(BIN_DIR)/heat2d_sequential
FG_BIN := $(BIN_DIR)/heat2d_fine_grained
CG_BIN := $(BIN_DIR)/heat2d_coarse_grained
CMP_BIN := $(BIN_DIR)/heat2d_cmp

BENCH_BIN := $(BIN_DIR)/heat2d_benchmark

FG_PROFILE_BIN := $(BIN_DIR)/heat2d_fine_grained_profile
CG_PROFILE_BIN := $(BIN_DIR)/heat2d_coarse_grained_profile
CMP_PROFILE_BIN := $(BIN_DIR)/heat2d_cmp_profile

TEST_LOGICAL_BIN := $(BIN_DIR)/test_logical_thread
TEST_SCHEDULER_BIN := $(BIN_DIR)/test_round_robin_scheduler
TEST_FG_BIN := $(BIN_DIR)/test_fine_grained

TEST_BARRIER_BIN := $(BIN_DIR)/test_reusable_barrier
TEST_CG_BIN := $(BIN_DIR)/test_coarse_grained

TEST_CMP_BIN := $(BIN_DIR)/test_cmp

STRESS_FG_BIN := $(BIN_DIR)/stress_fine_grained
STRESS_CG_BIN := $(BIN_DIR)/stress_coarse_grained
STRESS_CMP_BIN := $(BIN_DIR)/stress_cmp

# ------------------------------------------------------------
# Benchmark configuration
# ------------------------------------------------------------

RUNS ?= 200
WARMUPS ?= 5

# ------------------------------------------------------------
# Coarse-Grained standalone parameters
# ------------------------------------------------------------

CG_THREADS ?= 4
CG_BLOCK_US ?= 100
CG_ROWS_PER_BLOCK ?= 8

# ------------------------------------------------------------
# CMP standalone parameters
# ------------------------------------------------------------

# Used by:
#   perf-soft-cmp
#   perf-hw-cmp
#   profile-cmp
#
# CMP itself does not inject artificial blocking.
CMP_THREADS ?= 4

# ------------------------------------------------------------
# Current benchmark campaigns
#
# CMP is intentionally NOT integrated into the common benchmark yet.
# First we validate the standalone CMP implementation.
# ------------------------------------------------------------

# Fine-Grained:
#   Sequential
#   FG-1
#   FG-2
#   FG-4
#   FG-8
#   FG-16
#   FG-32
#   FG-64
#
# Coarse-Grained:
#   Sequential
#   CG-1
#   CG-2
#   CG-4
#   CG-8
#   CG-16
#   CG-32
#   CG-64

FG_BENCHMARK_CASES := 8
CG_BENCHMARK_CASES := 8
CMP_BENCHMARK_CASES := 8
ALL_BENCHMARK_CASES := 22

BENCHMARK_CASES ?= $(ALL_BENCHMARK_CASES)
CAMPAIGN_MODEL ?= all

RESULT_TAG ?= heat2d_$(shell date +%Y%m%d_%H%M%S)_$(shell uname -m)
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
	test-cmp \
	stress-fg \
	stress-cg \
	stress-cmp \
	python-env \
	python-deps \
	check-clean \
	metadata \
	campaign-dev-fg \
	campaign-dev-cg \
	campaign-fg \
	campaign-cg \
	campaign-run \
	campaign-dev-cmp \
	campaign-cmp \
	perf-soft-seq \
	perf-soft-fg \
	perf-soft-cg \
	perf-soft-cmp \
	perf-hw-seq \
	perf-hw-fg \
	perf-hw-cg \
	perf-hw-cmp \
	profile-fg \
	profile-cg \
	profile-cmp \
	help \
	clean

all: release tests

release: \
	$(SEQ_BIN) \
	$(FG_BIN) \
	$(CG_BIN) \
	$(CMP_BIN) \
	$(BENCH_BIN)

tests: \
	$(TEST_LOGICAL_BIN) \
	$(TEST_SCHEDULER_BIN) \
	$(TEST_FG_BIN) \
	$(TEST_BARRIER_BIN) \
	$(TEST_CG_BIN) \
	$(TEST_CMP_BIN)

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
# CMP - Chip Multiprocessing
#
# Real operating-system threads execute the numerical kernel
# concurrently. No synthetic blocking is injected.
# ============================================================

$(CMP_BIN): \
	$(SRC)/models/cmp/main.cpp \
	$(CORE_SRCS) \
	$(CMP_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/cmp/main.cpp \
		$(CORE_SRCS) \
		$(CMP_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

# ============================================================
# Common benchmark
#
# Current supported selectors:
#
#   ./heat2d_benchmark RUNS WARMUPS output.csv fg
#   ./heat2d_benchmark RUNS WARMUPS output.csv cg
#   ./heat2d_benchmark RUNS WARMUPS output.csv all
#
# CMP will be integrated after standalone validation.
# ============================================================

$(BENCH_BIN): \
	$(BENCHMARKS)/benchmark_heat2d.cpp \
	$(SRC)/benchmark/benchmark.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(FINE_GRAINED_SRCS) \
	$(COARSE_GRAINED_SRCS) \
	$(CMP_SRCS) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(BENCHMARKS)/benchmark_heat2d.cpp \
		$(SRC)/benchmark/benchmark.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(FINE_GRAINED_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		$(CMP_SRCS) \
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
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/coarse_grained/test_reusable_barrier.cpp \
		$(REUSABLE_BARRIER_SRC) \
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
# CMP Unit / Integration Tests
# ============================================================

$(TEST_CMP_BIN): \
	$(TESTS)/cmp/test_cmp.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(CMP_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/cmp/test_cmp.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(CMP_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
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

test-cmp: $(TEST_CMP_BIN)

	@echo
	@echo "========================================"
	@echo "Running Heat2D CMP tests"
	@echo "========================================"
	$(TEST_CMP_BIN)
	@echo
	@echo "CMP tests PASSED."

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
	@echo "--- CMP ---"
	$(TEST_CMP_BIN)

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
# CMP Stress Test
#
# stress_cmp.cpp currently executes:
#   CMP-1
#   CMP-2
#   CMP-4
#   CMP-8
#
# with 3 repetitions per configuration on the 801x801 workload.
# ============================================================

$(STRESS_CMP_BIN): \
	$(TESTS)/cmp/stress_cmp.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(CMP_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/cmp/stress_cmp.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(CMP_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

stress-cmp: $(STRESS_CMP_BIN)
	$(STRESS_CMP_BIN)

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
# Current statistical campaigns:
#   Fine-Grained
#   Coarse-Grained
#
# CMP metadata will be added when campaign-cmp is integrated.
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
		echo "Campaign model:"; \
		echo "$(CAMPAIGN_MODEL)"; \
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
		echo "$(RELEASE_FLAGS) $(THREAD_FLAGS)"; \
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
		echo "Cases per measured round: $(BENCHMARK_CASES)"; \
		if [ "$(CAMPAIGN_MODEL)" = "fg" ]; then \
			echo "Cases: Sequential + Fine-Grained {1,2,4,8,16,32,64}"; \
			echo "Fine-Grained scheduler: Round-Robin"; \
			echo "Fine-Grained quantum: 1 cell update"; \
			echo "Fine-Grained implementation: simulated logical contexts"; \
		elif [ "$(CAMPAIGN_MODEL)" = "cg" ]; then \
			echo "Cases: Sequential + Coarse-Grained {1,2,4,8,16,32,64}"; \
			echo "Coarse-Grained implementation: std::thread workers"; \
			echo "Coarse-Grained synchronization: reusable timestep barrier"; \
			echo "Coarse-Grained blocking duration: 100 us"; \
			echo "Coarse-Grained rows per blocking event: 8"; \
			echo "Coarse-Grained blocking events per timestep: 100"; \
			echo "Coarse-Grained blocking events per run: 100000"; \
		elif [ "$(CAMPAIGN_MODEL)" = "cmp" ]; then \
			echo "Cases: Sequential + CMP {1,2,4,8,16,32,64}"; \
			echo "CMP implementation: real std::thread workers"; \
			echo "CMP synchronization: reusable timestep barrier"; \
			echo "CMP synthetic blocking: disabled"; \
			echo "CMP barrier phases per timestep: 2"; \
		else \
			echo "Cases: Sequential + Fine-Grained {1,2,4,8,16,32,64} + Coarse-Grained {1,2,4,8,16,32,64} + CMP {1,2,4,8,16,32,64}"; \
		fi; \
	} > $(METADATA)

	@echo "Metadata written to:"
	@echo "  $(METADATA)"

# ============================================================
# Development benchmark campaigns
#
# CMP is not integrated here yet.
# ============================================================

campaign-dev-fg: RUNS=3
campaign-dev-fg: WARMUPS=1
campaign-dev-fg: campaign-fg

campaign-dev-cg: RUNS=3
campaign-dev-cg: WARMUPS=1
campaign-dev-cg: campaign-cg

campaign-dev-cmp: RUNS=3
campaign-dev-cmp: WARMUPS=1
campaign-dev-cmp: campaign-cmp

# ============================================================
# Fine-Grained benchmark campaign
#
# Cases:
#   Sequential
#   FG-1
#   FG-2
#   FG-4
#   FG-8
#   FG-16
#   FG-32
#   FG-64
#
# Default:
#   200 measured rounds
#   5 warm-up rounds
#
# Expected raw CSV lines with RUNS=200:
#   200 * 8 + 1 = 1601
# ============================================================

campaign-fg: CAMPAIGN_MODEL=fg
campaign-fg: BENCHMARK_CASES=$(FG_BENCHMARK_CASES)
campaign-fg: campaign-run

# ============================================================
# Coarse-Grained benchmark campaign
#
# Cases:
#   Sequential
#   CG-1
#   CG-2
#   CG-4
#   CG-8
#   CG-16
#   CG-32
#   CG-64
#
# Default:
#   200 measured rounds
#   5 warm-up rounds
#
# Expected raw CSV lines with RUNS=200:
#   200 * 8 + 1 = 1601
# ============================================================

campaign-cg: CAMPAIGN_MODEL=cg
campaign-cg: BENCHMARK_CASES=$(CG_BENCHMARK_CASES)
campaign-cg: campaign-run

# ============================================================
# CMP benchmark campaign
#
# Cases:
#   Sequential
#   CMP-1
#   CMP-2
#   CMP-4
#   CMP-8
#   CMP-16
#   CMP-32
#   CMP-64
#
# Default:
#   200 measured rounds
#   5 warm-up rounds
#
# Expected raw CSV lines with RUNS=200:
#   200 * 8 + 1 = 1601
# ============================================================

campaign-cmp: CAMPAIGN_MODEL=cmp
campaign-cmp: BENCHMARK_CASES=$(CMP_BENCHMARK_CASES)
campaign-cmp: campaign-run

# ============================================================
# Common campaign implementation
#
# Current valid models:
#
#   fg
#   cg
#
# CMP will be added after benchmark integration.
# ============================================================

campaign-run: check-clean release python-deps
	@if [ "$(CAMPAIGN_MODEL)" != "fg" ] && \
	    [ "$(CAMPAIGN_MODEL)" != "cg" ] && \
	    [ "$(CAMPAIGN_MODEL)" != "cmp" ]; then \
		echo "ERROR: campaign-run requires CAMPAIGN_MODEL=fg, cg or cmp."; \
		exit 1; \
	fi

	@echo
	@echo "========================================"
	@echo "Heat2D Benchmark Campaign"
	@echo "========================================"
	@echo "Model      : $(CAMPAIGN_MODEL)"
	@echo "Cases      : $(BENCHMARK_CASES)"
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
		WARMUPS="$(WARMUPS)" \
		BENCHMARK_CASES="$(BENCHMARK_CASES)" \
		CAMPAIGN_MODEL="$(CAMPAIGN_MODEL)"

	@echo
	@echo "Running benchmark..."

	$(BENCH_BIN) \
		$(RUNS) \
		$(WARMUPS) \
		$(RAW_RESULTS) \
		$(CAMPAIGN_MODEL) | tee $(BENCHMARK_LOG)

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
	@echo "Model    : $(CAMPAIGN_MODEL)"
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

perf-soft-cmp: $(CMP_BIN)
	sudo perf stat \
		-e task-clock,context-switches,cpu-migrations,page-faults \
		$(CMP_BIN) $(CMP_THREADS)

# ============================================================
# perf - hardware PMU events
#
# Intended for native hardware.
#
# UTM may report these events as unsupported because hardware PMU
# counters are generally not exposed by the virtual machine.
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

perf-hw-cmp: $(CMP_BIN)
	sudo perf stat \
		-e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
		$(CMP_BIN) $(CMP_THREADS)

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
# CMP profiling binary
# ============================================================

$(CMP_PROFILE_BIN): \
	$(SRC)/models/cmp/main.cpp \
	$(CORE_SRCS) \
	$(CMP_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(PROFILE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/cmp/main.cpp \
		$(CORE_SRCS) \
		$(CMP_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

profile-cmp: $(CMP_PROFILE_BIN)
	sudo perf record \
		-e cpu-clock \
		-g \
		-- $(CMP_PROFILE_BIN) $(CMP_THREADS)

	@echo
	@echo "CMP profiling data written to perf.data"
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
	@echo "      Build Sequential, Fine-Grained, Coarse-Grained, CMP and benchmark binaries."
	@echo

	@echo "Tests:"
	@echo "  make test"
	@echo "      Compile and run Fine-Grained, Coarse-Grained and CMP tests."
	@echo
	@echo "  make test-fg"
	@echo "      Run only Fine-Grained tests."
	@echo
	@echo "  make test-cg"
	@echo "      Run only Coarse-Grained tests."
	@echo
	@echo "  make test-cmp"
	@echo "      Run only CMP integration tests."
	@echo

	@echo "Stress:"
	@echo "  make stress-fg"
	@echo "      Run Fine-Grained stress test."
	@echo
	@echo "  make stress-cg"
	@echo "      Run Coarse-Grained stress test."
	@echo
	@echo "  make stress-cmp"
	@echo "      Run CMP 801x801 stress test for 1,2,4,8 workers."
	@echo

	@echo "Current benchmark campaigns:"
	@echo "  make campaign-dev-fg RESULT_TAG=utm_fg_smoke"
	@echo "      3 measured rounds + 1 warm-up for Fine-Grained."
	@echo
	@echo "  make campaign-dev-cg RESULT_TAG=utm_cg_smoke"
	@echo "      3 measured rounds + 1 warm-up for Coarse-Grained."
	@echo
	@echo "  make campaign-fg RUNS=200 WARMUPS=5 RESULT_TAG=utm_fg_200"
	@echo "      Sequential + FG {1,2,4,8,16,32,64}."
	@echo
	@echo "  make campaign-cg RUNS=200 WARMUPS=5 RESULT_TAG=utm_cg_200"
	@echo "      Sequential + CG {1,2,4,8,16,32,64}."
	@echo
	@echo "  CMP campaign is not integrated into the common benchmark yet."
	@echo

	@echo "Standalone parameters:"
	@echo "  CG_THREADS=4"
	@echo "      Coarse-Grained worker count."
	@echo
	@echo "  CG_BLOCK_US=100"
	@echo "      Coarse-Grained synthetic blocking duration."
	@echo
	@echo "  CG_ROWS_PER_BLOCK=8"
	@echo "      Coarse-Grained rows per synthetic blocking event."
	@echo
	@echo "  CMP_THREADS=4"
	@echo "      CMP real worker-thread count for perf/profile targets."
	@echo

	@echo "Software perf:"
	@echo "  make perf-soft-seq"
	@echo "  make perf-soft-fg"
	@echo "  make perf-soft-cg"
	@echo "  make perf-soft-cmp CMP_THREADS=4"
	@echo

	@echo "Hardware perf:"
	@echo "  make perf-hw-seq"
	@echo "  make perf-hw-fg"
	@echo "  make perf-hw-cg"
	@echo "  make perf-hw-cmp CMP_THREADS=4"
	@echo

	@echo "Profiling:"
	@echo "  make profile-fg"
	@echo "  make profile-cg"
	@echo "  make profile-cmp CMP_THREADS=4"
	@echo

	@echo "CMP examples:"
	@echo "  build/bin/heat2d_cmp 1"
	@echo "  build/bin/heat2d_cmp 2"
	@echo "  build/bin/heat2d_cmp 4"
	@echo "  build/bin/heat2d_cmp 8"
	@echo "  make test-cmp"
	@echo "  make stress-cmp"
	@echo "  make perf-soft-cmp CMP_THREADS=4"
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