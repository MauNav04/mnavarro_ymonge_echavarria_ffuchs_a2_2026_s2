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

# Detect available C++ compiler (prefer clang++ if installed, otherwise fallback to g++)
ifeq ($(shell which clang++ 2>/dev/null),)
CXX ?= g++
else
CXX ?= clang++
endif

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
# Coarse-Grained module. CMP and SMT reuse this implementation.
REUSABLE_BARRIER_SRC := \
	$(SRC)/models/coarse_grained/reusable_barrier.cpp

COARSE_GRAINED_SRCS := \
	$(REUSABLE_BARRIER_SRC) \
	$(SRC)/models/coarse_grained/coarse_grained.cpp

CMP_SRCS := \
	$(SRC)/models/cmp/cmp.cpp

SMT_SRCS := \
	$(SRC)/models/smt/cpu_topology.cpp \
	$(SRC)/models/smt/thread_affinity.cpp \
	$(SRC)/models/smt/smt.cpp

# ------------------------------------------------------------
# Executables
# ------------------------------------------------------------

SEQ_BIN := $(BIN_DIR)/heat2d_sequential
FG_BIN := $(BIN_DIR)/heat2d_fine_grained
CG_BIN := $(BIN_DIR)/heat2d_coarse_grained
CMP_BIN := $(BIN_DIR)/heat2d_cmp
SMT_BIN := $(BIN_DIR)/heat2d_smt

BENCH_BIN := $(BIN_DIR)/heat2d_benchmark

FG_PROFILE_BIN := $(BIN_DIR)/heat2d_fine_grained_profile
CG_PROFILE_BIN := $(BIN_DIR)/heat2d_coarse_grained_profile
CMP_PROFILE_BIN := $(BIN_DIR)/heat2d_cmp_profile
SMT_PROFILE_BIN := $(BIN_DIR)/heat2d_smt_profile

TEST_LOGICAL_BIN := $(BIN_DIR)/test_logical_thread
TEST_SCHEDULER_BIN := $(BIN_DIR)/test_round_robin_scheduler
TEST_FG_BIN := $(BIN_DIR)/test_fine_grained

TEST_BARRIER_BIN := $(BIN_DIR)/test_reusable_barrier
TEST_CG_BIN := $(BIN_DIR)/test_coarse_grained

TEST_CMP_BIN := $(BIN_DIR)/test_cmp

TEST_SMT_TOPOLOGY_BIN := $(BIN_DIR)/test_cpu_topology
TEST_SMT_AFFINITY_BIN := $(BIN_DIR)/test_thread_affinity
TEST_SMT_BIN := $(BIN_DIR)/test_smt

STRESS_FG_BIN := $(BIN_DIR)/stress_fine_grained
STRESS_CG_BIN := $(BIN_DIR)/stress_coarse_grained
STRESS_CMP_BIN := $(BIN_DIR)/stress_cmp
STRESS_SMT_BIN := $(BIN_DIR)/stress_smt

# ------------------------------------------------------------
# Benchmark configuration
# ------------------------------------------------------------

RUNS ?= 200
WARMUPS ?= 5

# Formal campaigns require a clean Git working tree.
#
# Development campaigns may explicitly disable this check.
REQUIRE_CLEAN ?= 1

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
# SMT standalone / profiling parameters
# ------------------------------------------------------------

# By default use every logical CPU available to the process.
# Override explicitly when comparing SMT-8 vs SMT-16 on native hardware.
SMT_THREADS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN)

# ------------------------------------------------------------
# Current benchmark campaigns
#
# Fine-Grained, Coarse-Grained and CMP use the fixed execution-context
# set {1,2,4,8,16,32,64}. SMT intentionally disables oversubscription
# and only includes {1,2,4,8,16} values that fit inside the logical CPUs
# currently available to the process.
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
#
# CMP:
#   Sequential
#   CMP-1
#   CMP-2
#   CMP-4
#   CMP-8
#   CMP-16
#   CMP-32
#   CMP-64
#
# SMT:
#   Sequential + supported values from SMT-{1,2,4,8,16}

FG_BENCHMARK_CASES := 8
CG_BENCHMARK_CASES := 8
CMP_BENCHMARK_CASES := 8

# SMT cases are topology-dependent:
#   Sequential + SMT-{1,2,4,8,16}, capped by available logical CPUs.
SMT_THREAD_LIST := $(strip $(shell \
	n=$$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN); \
	for t in 1 2 4 8 16; do \
		if [ $$t -le $$n ]; then printf "%s " $$t; fi; \
	done))

SMT_MODEL_CASES := $(words $(SMT_THREAD_LIST))
SMT_BENCHMARK_CASES := $(shell echo $$((1 + $(SMT_MODEL_CASES))))

# Previous combined benchmark:
#   Sequential + 7 FG + 7 CG + 7 CMP = 22 cases.
# SMT contributes only its model cases because Sequential is shared.
ALL_BENCHMARK_CASES := $(shell echo $$((22 + $(SMT_MODEL_CASES))))

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
	test-smt \
	stress-fg \
	stress-cg \
	stress-cmp \
	stress-smt \
	python-env \
	python-deps \
	check-clean \
	metadata \
	campaign-dev-fg \
	campaign-dev-cg \
	campaign-dev-cmp \
	campaign-dev-smt \
	campaign-fg \
	campaign-cg \
	campaign-cmp \
	campaign-smt \
	campaign-run \
	perf-soft-seq \
	perf-soft-fg \
	perf-soft-cg \
	perf-soft-cmp \
	perf-soft-smt \
	perf-hw-seq \
	perf-hw-fg \
	perf-hw-cg \
	perf-hw-cmp \
	perf-hw-smt \
	profile-fg \
	profile-cg \
	profile-cmp \
	profile-smt \
	help \
	clean

all: release tests

release: \
	$(SEQ_BIN) \
	$(FG_BIN) \
	$(CG_BIN) \
	$(CMP_BIN) \
	$(SMT_BIN) \
	$(BENCH_BIN)

tests: \
	$(TEST_LOGICAL_BIN) \
	$(TEST_SCHEDULER_BIN) \
	$(TEST_FG_BIN) \
	$(TEST_BARRIER_BIN) \
	$(TEST_CG_BIN) \
	$(TEST_CMP_BIN) \
	$(TEST_SMT_TOPOLOGY_BIN) \
	$(TEST_SMT_AFFINITY_BIN) \
	$(TEST_SMT_BIN)

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
# SMT - Simultaneous Multithreading
# ============================================================

$(SMT_BIN): \
	$(SRC)/models/smt/main.cpp \
	$(CORE_SRCS) \
	$(SMT_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/smt/main.cpp \
		$(CORE_SRCS) \
		$(SMT_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

# ============================================================
# Common benchmark
# ============================================================

$(BENCH_BIN): \
	$(BENCHMARKS)/benchmark_heat2d.cpp \
	$(SRC)/benchmark/benchmark.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(FINE_GRAINED_SRCS) \
	$(COARSE_GRAINED_SRCS) \
	$(CMP_SRCS) \
	$(SMT_SRCS) | $(BIN_DIR)

	$(CXX) $(RELEASE_FLAGS) $(THREAD_FLAGS) \
		$(BENCHMARKS)/benchmark_heat2d.cpp \
		$(SRC)/benchmark/benchmark.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(FINE_GRAINED_SRCS) \
		$(COARSE_GRAINED_SRCS) \
		$(CMP_SRCS) \
		$(SMT_SRCS) \
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
# SMT Unit / Integration Tests
# ============================================================

$(TEST_SMT_TOPOLOGY_BIN): \
	$(TESTS)/smt/test_cpu_topology.cpp \
	$(SRC)/models/smt/cpu_topology.cpp | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/smt/test_cpu_topology.cpp \
		$(SRC)/models/smt/cpu_topology.cpp \
		-o $@

$(TEST_SMT_AFFINITY_BIN): \
	$(TESTS)/smt/test_thread_affinity.cpp \
	$(SRC)/models/smt/cpu_topology.cpp \
	$(SRC)/models/smt/thread_affinity.cpp | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/smt/test_thread_affinity.cpp \
		$(SRC)/models/smt/cpu_topology.cpp \
		$(SRC)/models/smt/thread_affinity.cpp \
		-o $@

$(TEST_SMT_BIN): \
	$(TESTS)/smt/test_smt.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(SMT_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/smt/test_smt.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(SMT_SRCS) \
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

test-smt: \
	$(TEST_SMT_TOPOLOGY_BIN) \
	$(TEST_SMT_AFFINITY_BIN) \
	$(TEST_SMT_BIN)

	@echo
	@echo "========================================"
	@echo "Running Heat2D SMT tests"
	@echo "========================================"
	$(TEST_SMT_TOPOLOGY_BIN)
	$(TEST_SMT_AFFINITY_BIN)
	$(TEST_SMT_BIN)
	@echo
	@echo "SMT tests PASSED."

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
	@echo "--- SMT ---"
	$(TEST_SMT_TOPOLOGY_BIN)
	$(TEST_SMT_AFFINITY_BIN)
	$(TEST_SMT_BIN)

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
# SMT Stress Test
# ============================================================

$(STRESS_SMT_BIN): \
	$(TESTS)/smt/stress_smt.cpp \
	$(CORE_SRCS) \
	$(SEQUENTIAL_SRC) \
	$(SMT_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(TEST_FLAGS) $(THREAD_FLAGS) \
		$(TESTS)/smt/stress_smt.cpp \
		$(CORE_SRCS) \
		$(SEQUENTIAL_SRC) \
		$(SMT_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

stress-smt: $(STRESS_SMT_BIN)
	$(STRESS_SMT_BIN)

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
		echo "CPU/core mapping:"; \
		lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE 2>/dev/null || true; \
		echo; \
		echo "Linux SMT control:"; \
		cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo "unavailable"; \
		echo; \
		echo "Process-visible logical CPUs (nproc):"; \
		nproc 2>/dev/null || true; \
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
		elif [ "$(CAMPAIGN_MODEL)" = "smt" ]; then \
			echo "Cases: Sequential + SMT {$(SMT_THREAD_LIST)}"; \
			echo "SMT implementation: persistent std::thread workers"; \
			echo "SMT topology discovery: sched_getaffinity + Linux sysfs"; \
			echo "SMT affinity: pthread_setaffinity_np"; \
			echo "SMT placement: one logical CPU per physical core before sibling threads"; \
			echo "SMT oversubscription: disabled"; \
			echo "SMT synchronization: reusable timestep barrier"; \
			echo "SMT barrier phases per timestep: 2"; \
		else \
			echo "Cases: Sequential + Fine-Grained {1,2,4,8,16,32,64} + Coarse-Grained {1,2,4,8,16,32,64} + CMP {1,2,4,8,16,32,64} + SMT {$(SMT_THREAD_LIST)}"; \
		fi; \
	} > $(METADATA)

	@echo "Metadata written to:"
	@echo "  $(METADATA)"

# ============================================================
# Development benchmark campaigns
#
# FG / CG / CMP preserve their existing behavior.
#
# SMT development additionally:
#   1. runs the complete SMT test suite,
#   2. executes a short benchmark,
#   3. generates statistics,
#   4. generates plots,
#   5. does not require a clean Git working tree.
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

campaign-dev-smt:
	@echo
	@echo "========================================"
	@echo "Heat2D SMT Development Campaign"
	@echo "========================================"
	@echo
	@echo "Step 1/2 - Running SMT tests..."
	@echo

	@$(MAKE) --no-print-directory test-smt

	@echo
	@echo "Step 2/2 - Running SMT benchmark pipeline..."
	@echo

	@$(MAKE) --no-print-directory campaign-smt \
		RUNS=3 \
		WARMUPS=1 \
		REQUIRE_CLEAN=0 \
		RESULT_TAG="$(RESULT_TAG)"

# ============================================================
# Fine-Grained benchmark campaign
# ============================================================

campaign-fg: CAMPAIGN_MODEL=fg
campaign-fg: BENCHMARK_CASES=$(FG_BENCHMARK_CASES)
campaign-fg: campaign-run

# ============================================================
# Coarse-Grained benchmark campaign
# ============================================================

campaign-cg: CAMPAIGN_MODEL=cg
campaign-cg: BENCHMARK_CASES=$(CG_BENCHMARK_CASES)
campaign-cg: campaign-run

# ============================================================
# CMP benchmark campaign
# ============================================================

campaign-cmp: CAMPAIGN_MODEL=cmp
campaign-cmp: BENCHMARK_CASES=$(CMP_BENCHMARK_CASES)
campaign-cmp: campaign-run

# ============================================================
# SMT benchmark campaign
#
# On target Ryzen 8C / 16T:
#
#   Sequential
#   SMT-1
#   SMT-2
#   SMT-4
#   SMT-8
#   SMT-16
#
# RUNS=200:
#
#   200 * 6 + 1 header = 1201 CSV lines
# ============================================================

campaign-smt: CAMPAIGN_MODEL=smt
campaign-smt: BENCHMARK_CASES=$(SMT_BENCHMARK_CASES)
campaign-smt: campaign-run

# ============================================================
# Common campaign implementation
#
# REQUIRE_CLEAN=1:
#   intended for formal campaigns.
#
# REQUIRE_CLEAN=0:
#   intended only for development/smoke campaigns.
# ============================================================

campaign-run: release python-deps
	@if [ "$(REQUIRE_CLEAN)" = "1" ]; then \
		$(MAKE) --no-print-directory check-clean; \
	fi

	@if [ "$(CAMPAIGN_MODEL)" != "fg" ] && \
	    [ "$(CAMPAIGN_MODEL)" != "cg" ] && \
	    [ "$(CAMPAIGN_MODEL)" != "cmp" ] && \
	    [ "$(CAMPAIGN_MODEL)" != "smt" ]; then \
		echo "ERROR: campaign-run requires CAMPAIGN_MODEL=fg, cg, cmp or smt."; \
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
	@echo "Git clean  : $(REQUIRE_CLEAN)"
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
# Heat2D Standardized Canonical Benchmarking Targets
# ============================================================

# Mini-prueba de desarrollo rápido (3 corridas por modelo, ~10 segundos)
smoke-heat2d: release
	python3 $(SCRIPTS)/benchmark_canonical.py --runs 3

# Campaña experimental formal completa (200 corridas por modelo, ~3 minutos)
campaign-200-heat2d: release
	python3 $(SCRIPTS)/benchmark_canonical.py --runs 200

# Perfilado microarquitectónico con Linux perf (3 corridas por modelo)
profile-perf-heat2d: release
	python3 $(SCRIPTS)/profile_perf.py --runs 3

# Regenerar todas las gráficas estandarizadas a partir de los CSV en metricas/
plot-heat2d:
	python3 $(SCRIPTS)/plot_benchmark.py \
		$(HEAT2D)/metricas/resumen_estadistico.csv \
		--raw $(HEAT2D)/metricas/heat2d_benchmark.csv \
		--output-dir $(HEAT2D)/metricas/graficas
	python3 $(SCRIPTS)/profile_perf.py --plot-only

plot-heat2d-200: plot-heat2d

profile-plot-heat2d:
	python3 $(SCRIPTS)/profile_perf.py --plot-only

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

perf-soft-smt: $(SMT_BIN)
	sudo perf stat \
		-e task-clock,context-switches,cpu-migrations,page-faults \
		$(SMT_BIN) $(SMT_THREADS)

# ============================================================
# perf - hardware PMU events
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

perf-hw-smt: $(SMT_BIN)
	sudo perf stat \
		-e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
		$(SMT_BIN) $(SMT_THREADS)

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
# SMT profiling binary
# ============================================================

$(SMT_PROFILE_BIN): \
	$(SRC)/models/smt/main.cpp \
	$(CORE_SRCS) \
	$(SMT_SRCS) \
	$(REUSABLE_BARRIER_SRC) | $(BIN_DIR)

	$(CXX) $(PROFILE_FLAGS) $(THREAD_FLAGS) \
		$(SRC)/models/smt/main.cpp \
		$(CORE_SRCS) \
		$(SMT_SRCS) \
		$(REUSABLE_BARRIER_SRC) \
		-o $@

profile-smt: $(SMT_PROFILE_BIN)
	sudo perf record \
		-e cpu-clock \
		-g \
		-- $(SMT_PROFILE_BIN) $(SMT_THREADS)

	@echo
	@echo "SMT profiling data written to perf.data"
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
	@echo "      Build Sequential, Fine-Grained, Coarse-Grained, CMP, SMT and benchmark binaries."
	@echo

	@echo "Tests:"
	@echo "  make test"
	@echo "      Compile and run all Heat2D tests."
	@echo
	@echo "  make test-smt"
	@echo "      Run SMT topology, affinity and integration tests."
	@echo

	@echo "Stress:"
	@echo "  make stress-smt"
	@echo "      Run SMT 801x801 stress test."
	@echo

	@echo "SMT development campaign:"
	@echo "  make campaign-dev-smt RESULT_TAG=utm_smt_smoke"
	@echo "      Automatically runs SMT tests, benchmark, statistical analysis and plots."
	@echo "      Uses 3 measured rounds + 1 warm-up."
	@echo "      Does not require a clean Git working tree."
	@echo

	@echo "SMT formal campaign:"
	@echo "  make campaign-smt RUNS=200 WARMUPS=5 RESULT_TAG=ryzen_smt_200"
	@echo "      Requires a clean Git working tree."
	@echo "      On 8C/16T target: Sequential + SMT {1,2,4,8,16}."
	@echo

	@echo "Software perf:"
	@echo "  make perf-soft-smt SMT_THREADS=16"
	@echo

	@echo "Hardware perf:"
	@echo "  make perf-hw-smt SMT_THREADS=16"
	@echo

	@echo "Profiling:"
	@echo "  make profile-smt SMT_THREADS=16"
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