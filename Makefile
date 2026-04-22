# SDL3D — Top-level Makefile wrapping CMake
#
# Usage:
#   make              Build library + tests
#   make test         Run the test suite
#   make demos        Build library + tests + demos
#   make clean        Remove build artifacts
#   make install      Install to system (Release build)
#   make release      Build optimized Release configuration
#   make debug        Build Debug configuration (default)
#   make sanitize     Build with AddressSanitizer + UBSan
#   make format       Check clang-format compliance
#   make format-fix   Auto-fix formatting
#   make help         Show this help

BUILD_DIR    ?= build
CMAKE_FLAGS  ?=

.PHONY: all debug release sanitize test demos clean install format format-fix help

all: debug

debug:
	@cmake -B $(BUILD_DIR)/debug \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSDL3D_BUILD_TESTS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/debug

release:
	@cmake -B $(BUILD_DIR)/release \
		-DCMAKE_BUILD_TYPE=Release \
		-DSDL3D_BUILD_TESTS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/release

sanitize:
	@CC=clang cmake -B $(BUILD_DIR)/sanitize \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSDL3D_BUILD_TESTS=ON \
		-DSDL3D_ENABLE_SANITIZERS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/sanitize

test: debug
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

test-release: release
	@cd $(BUILD_DIR)/release && ctest --output-on-failure

demos:
	@cmake -B $(BUILD_DIR)/debug \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSDL3D_BUILD_TESTS=ON \
		-DSDL3D_BUILD_DEMOS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/debug

install: release
	@cmake --install $(BUILD_DIR)/release

clean:
	@rm -rf $(BUILD_DIR)

format:
	@./scripts/check_clang_format.sh

format-fix:
	@find src include demos tests -name '*.c' -o -name '*.h' -o -name '*.cpp' \
		| grep -v vendor | xargs clang-format -i
	@echo "Formatting applied."

help:
	@head -14 Makefile | tail -13
