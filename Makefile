# SDL3D — Top-level Makefile wrapping CMake
#
# Usage:
#   make              Build everything (library + demos + tests)
#   make test         Run the test suite
#   make clean        Remove build artifacts
#   make install      Install to system (Release build)
#   make release      Build optimized Release configuration
#   make debug        Build Debug configuration (default)
#   make sanitize     Build with AddressSanitizer + UBSan
#   make format       Check clang-format compliance
#   make format-fix   Auto-fix formatting
#   make demos        Build only the demos
#   make showcase     Run the 1080p showcase demo
#   make doom         Run the Doom tribute demo
#   make help         Show this help

BUILD_DIR    ?= build
BUILD_TYPE   ?= Debug
CMAKE_FLAGS  ?=
PRESET       ?=

.PHONY: all debug release sanitize test clean install format format-fix \
        demos showcase doom help

all: debug

debug:
	@cmake -B $(BUILD_DIR)/debug \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSDL3D_BUILD_TESTS=ON \
		-DSDL3D_BUILD_DEMOS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/debug

release:
	@cmake -B $(BUILD_DIR)/release \
		-DCMAKE_BUILD_TYPE=Release \
		-DSDL3D_BUILD_TESTS=ON \
		-DSDL3D_BUILD_DEMOS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/release

sanitize:
	@CC=clang cmake -B $(BUILD_DIR)/sanitize \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSDL3D_BUILD_TESTS=ON \
		-DSDL3D_BUILD_DEMOS=ON \
		-DSDL3D_ENABLE_SANITIZERS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/sanitize

test: debug
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

test-release: release
	@cd $(BUILD_DIR)/release && ctest --output-on-failure

demos: debug
	@cmake --build $(BUILD_DIR)/debug --target sdl3d_showcase_1080p sdl3d_doom_tribute

showcase: demos
	@./$(BUILD_DIR)/debug/demos/sdl3d_showcase_1080p

doom: demos
	@./$(BUILD_DIR)/debug/demos/sdl3d_doom_tribute

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
	@head -17 Makefile | tail -15
