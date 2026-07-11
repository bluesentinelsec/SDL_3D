# Slayer 3D — Top-level Makefile wrapping CMake
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
#   make web          Build + serve the browser editor via Docker
#   make web-stop     Stop the browser editor container
#   make format       Check clang-format compliance
#   make format-fix   Auto-fix formatting
#   make help         Show this help

BUILD_DIR    ?= build
CMAKE_FLAGS  ?=
BUILD_FLAGS  ?= --parallel

# Browser editor container settings (override on the command line as needed).
WEB_IMAGE     ?= slayer3d-editor-web
WEB_CONTAINER ?= slayer3d-editor-web
WEB_PORT      ?= 8080

.PHONY: all debug release sanitize test demos clean install web web-stop format format-fix help

all: debug

debug:
	@cmake -B $(BUILD_DIR)/debug \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSLAYER3D_BUILD_TESTS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/debug $(BUILD_FLAGS)

release:
	@cmake -B $(BUILD_DIR)/release \
		-DCMAKE_BUILD_TYPE=Release \
		-DSLAYER3D_BUILD_TESTS=ON \
		-DSLAYER3D_BUILD_DEMOS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/release $(BUILD_FLAGS)

sanitize:
	@CC=clang cmake -B $(BUILD_DIR)/sanitize \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSLAYER3D_BUILD_TESTS=ON \
		-DSLAYER3D_ENABLE_SANITIZERS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/sanitize $(BUILD_FLAGS)

test: debug
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

test-release: release
	@cd $(BUILD_DIR)/release && ctest --output-on-failure

demos:
	@cmake -B $(BUILD_DIR)/debug \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSLAYER3D_BUILD_TESTS=ON \
		-DSLAYER3D_BUILD_DEMOS=ON \
		$(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR)/debug $(BUILD_FLAGS)

install: release
	@cmake --install $(BUILD_DIR)/release

clean:
	@rm -rf $(BUILD_DIR)

web:
	@docker build -f docker/emscripten-editor/Dockerfile -t $(WEB_IMAGE) .
	@docker rm -f $(WEB_CONTAINER) >/dev/null 2>&1 || true
	@docker run --rm -d -p $(WEB_PORT):8080 --name $(WEB_CONTAINER) $(WEB_IMAGE) >/dev/null
	@echo "Slayer3D web editor running: http://localhost:$(WEB_PORT)/slayer3d_editor_web.html"
	@echo "Stop it with: make web-stop"

web-stop:
	@docker rm -f $(WEB_CONTAINER) >/dev/null 2>&1 || true
	@echo "Web editor container stopped."

format:
	@./scripts/check_clang_format.sh

format-fix:
	@find src include demos tests -name '*.c' -o -name '*.h' -o -name '*.cpp' \
		| grep -v vendor | xargs clang-format -i
	@echo "Formatting applied."

help:
	@head -16 Makefile | tail -15
