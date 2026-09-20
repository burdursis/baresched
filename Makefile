.DEFAULT_GOAL := build

CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build/debug
COVERAGE_DIR ?= build/coverage
BUILD_TYPE ?= Debug
JOBS ?= 2
CMAKE_ARGS ?=

.PHONY: help configure build test example coverage clean

help:
	@echo "make build     Configure and build host tests"
	@echo "make test      Build and run all tests"
	@echo "make example   Build and run the deterministic host example"
	@echo "make coverage  Build with GCC, run tests, generate HTML/XML coverage"
	@echo "make clean     Clean compiled targets in debug and coverage directories"
	@echo "Overrides: BUILD_DIR, COVERAGE_DIR, BUILD_TYPE, JOBS, CMAKE_ARGS"

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DBUILD_TESTING=ON -DSCHEDULER_ENABLE_COVERAGE=OFF -DSCHEDULER_BUILD_EXAMPLES=ON

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

example: build
	"$(BUILD_DIR)/examples/basic/scheduler_example"

coverage:
	$(CMAKE) -S . -B "$(COVERAGE_DIR)" $(CMAKE_ARGS) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSCHEDULER_ENABLE_COVERAGE=ON -DSCHEDULER_BUILD_EXAMPLES=OFF
	$(CMAKE) --build "$(COVERAGE_DIR)" --target coverage --parallel "$(JOBS)"

clean:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then $(CMAKE) --build "$(BUILD_DIR)" --target clean; fi
	@if [ -f "$(COVERAGE_DIR)/CMakeCache.txt" ]; then $(CMAKE) --build "$(COVERAGE_DIR)" --target clean; fi
