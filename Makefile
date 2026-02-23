# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------

.DEFAULT_GOAL := all

PYTHON := python3
VENV_DIR := build/.bip-buffer-venv
VENV_ACTIVATE := . $(VENV_DIR)/bin/activate

SRCS := src/bip_buffer.c
HDRS := include/bip_buffer.h
EXAMPLES := $(wildcard examples/*.c)
BUILD_DIR := build

IMAGE_VERSION ?= 1.0.0
BIP_BUFFER_IMAGE_BUILD := bip-buffer/build:$(IMAGE_VERSION)

ifdef SKIP_CONTAINER
  CONTAINER_RUNNER :=
  VENV_DEPS :=
else
  CONTAINER_RUNNER := @podman run --rm --userns=keep-id \
    -v $(PWD):$(PWD) -w $(PWD) $(BIP_BUFFER_IMAGE_BUILD)
  VENV_DEPS := _image
endif

# Blue bold header: $(call header,TITLE)
define header
	@printf '\033[1;34m%-40s\033[0m\n' '$(1)'
endef

# ---------------------------------------------------------------------------
# Private targets
# ---------------------------------------------------------------------------

.PHONY: _image
_image: install-deps
	@podman image inspect $(BIP_BUFFER_IMAGE_BUILD) >/dev/null 2>&1 \
		|| $(MAKE) image-build

# Incremental venv setup:
#   1. Copy python3 into build/.bip-buffer-venv/.python/ so the venv is fully
#      self-contained and symlinks resolve inside the container
#   2. Create the venv only if it doesn't exist yet
#   3. Install tools only if they're missing
VENV_PYTHON := $(VENV_DIR)/.python

$(VENV_DIR): $(VENV_DEPS)
	$(call header,Setting up venv)
	@mkdir -p build
	$(CONTAINER_RUNNER) sh -c " \
	  if [ ! -x $(VENV_PYTHON)/bin/$(PYTHON) ]; then \
	    PYPREFIX=\$$(dirname \$$(dirname \$$(readlink -f \$$(command -v $(PYTHON))))) \
	    && mkdir -p $(VENV_PYTHON) \
	    && cp -a \"\$$PYPREFIX\"/* $(VENV_PYTHON)/; \
	  fi \
	  && { [ -d $(VENV_DIR)/bin ] || $(VENV_PYTHON)/bin/$(PYTHON) -m venv $(VENV_DIR); } \
	  && ln -sfn ../.python/bin/$(PYTHON) $(VENV_DIR)/bin/$(PYTHON) \
	  && $(VENV_ACTIVATE) \
	  && { [ -f $(VENV_DIR)/.initialized ] \
	       || { pip install --upgrade pip \
	            && pip install clang-format clang-tidy cppcheck gcovr lizard \
	            && touch $(VENV_DIR)/.initialized; }; } \
	"

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

.PHONY: install-deps
install-deps:
	@missing=""; \
	command -v make >/dev/null 2>&1 || missing="$$missing make"; \
	command -v podman >/dev/null 2>&1 || missing="$$missing podman"; \
	if [ -n "$$missing" ]; then \
		echo "Installing:$$missing"; \
		sudo apt-get update && sudo apt-get install -y $$missing; \
	else \
		echo "All dependencies already installed."; \
	fi

.PHONY: venv
venv: $(VENV_DIR)

# ---------------------------------------------------------------------------
# Development
# ---------------------------------------------------------------------------

.PHONY: format
format: $(VENV_DIR)
	$(call header,Formatting [clang-format])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && clang-format -i $(SRCS) $(HDRS) $(EXAMPLES)"

.PHONY: clang-tidy
clang-tidy: release
	$(call header,Linting [clang-tidy])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) \
		&& clang-tidy -p $(BUILD_DIR)/artifacts $(SRCS) \
		&& clang-tidy $(EXAMPLES) -- -std=c11 -I include"

.PHONY: cppcheck
cppcheck: release
	$(call header,Linting [cppcheck])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) \
		&& cppcheck \
			--project=$(BUILD_DIR)/artifacts/compile_commands.json \
			--file-filter='src/*' \
			--enable=warning,style,performance,portability \
			--check-level=exhaustive \
			--error-exitcode=1 \
		&& cppcheck \
			--std=c11 -I include \
			--enable=warning,style,performance,portability \
			--check-level=exhaustive \
			--error-exitcode=1 \
			$(EXAMPLES)"

.PHONY: lizard
lizard: $(VENV_DIR)
	$(call header,Complexity [lizard])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && lizard -C 10 -L 60 -w $(SRCS) $(EXAMPLES)"

.PHONY: lint
lint: clang-tidy cppcheck lizard

# ---------------------------------------------------------------------------
# Test & Coverage
# ---------------------------------------------------------------------------

.PHONY: test
test: $(VENV_DIR)
	$(call header,Testing [ceedling])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && ceedling test:all"

.PHONY: gcov
gcov: $(VENV_DIR)
	$(call header,Coverage [ceedling gcov])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && ceedling gcov:all"

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

.PHONY: examples
examples: $(VENV_DIR)
	$(call header,Examples)
	@mkdir -p $(BUILD_DIR)/examples
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) \
		&& for f in examples/*.c; do \
			name=\$$(basename \"\$$f\" .c) \
			&& gcc -std=c11 -Wall -Wextra -Werror -pedantic --coverage \
				-I include src/bip_buffer.c \"\$$f\" \
				-o build/examples/\"\$$name\" \
			&& build/examples/\"\$$name\"; \
		done \
		&& gcovr --force-color --filter src/ --print-summary \
		&& gcovr --force-color --filter examples/ \
			--fail-under-line 100 --print-summary"

# ---------------------------------------------------------------------------
# Release builds
# ---------------------------------------------------------------------------

.PHONY: release
release: $(VENV_DIR)
	$(call header,Release [native])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && ceedling release"

CROSS_TARGETS := release-arm32 release-arm64 release-x86_64 release-mips release-mips64 \
                 release-riscv64 release-powerpc release-freebsd-x86_64 release-freebsd-arm64

.PHONY: $(CROSS_TARGETS)
$(CROSS_TARGETS): release-%: $(VENV_DIR)
	$(call header,Release [$*])
	$(CONTAINER_RUNNER) sh -c "$(VENV_ACTIVATE) && ceedling --mixin=mixins/$*.yml release"
	@mkdir -p dist/$*
	@cp build/$*/release/libbip_buffer.a dist/$*/libbip_buffer.a

.PHONY: release-all
release-all: $(CROSS_TARGETS)

# ---------------------------------------------------------------------------
# Build image
# ---------------------------------------------------------------------------

.PHONY: image-build
image-build: install-deps
	$(call header,Building bip-buffer dev image [podman])
	@podman build -f containers/build.Containerfile \
		-t $(BIP_BUFFER_IMAGE_BUILD) . \
		|| podman image inspect $(BIP_BUFFER_IMAGE_BUILD) >/dev/null 2>&1

# ---------------------------------------------------------------------------
# Build & Clean
# ---------------------------------------------------------------------------

.PHONY: all
all: format release lint test gcov examples

.PHONY: clean
clean:
	$(call header,Cleaning)
	@rm -rf $(BUILD_DIR) dist

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------

.PHONY: help
help:
	@echo "Available commands:"
	@echo ""
	@echo "  Setup:"
	@echo "    install-deps             - Install podman"
	@echo "    venv                     - Create build/.bip-buffer-venv with Python tools"
	@echo ""
	@echo "  Development:"
	@echo "    format                   - Format code with clang-format"
	@echo "    clang-tidy               - Run clang-tidy static analysis"
	@echo "    cppcheck                 - Run cppcheck static analysis"
	@echo "    lizard                   - Check cyclomatic complexity (CCN=10, length=60)"
	@echo "    lint                     - Run clang-tidy, cppcheck, and lizard"
	@echo ""
	@echo "  Test & Coverage:"
	@echo "    test                     - Run all Unity tests via Ceedling"
	@echo "    gcov                     - Run tests with gcov coverage report"
	@echo "    examples                 - Build and run examples/"
	@echo ""
	@echo "  Release builds:"
	@echo "    release                  - Build native static library (.a)"
	@echo "    release-{arch}           - Cross-compile (arm32 arm64 x86_64 mips mips64 riscv64 powerpc"
	@echo "                               freebsd-x86_64 freebsd-arm64)"
	@echo "    release-all              - Cross-compile for all architectures"
	@echo ""
	@echo "  Build image:"
	@echo "    image-build              - Build the dev container image"
	@echo ""
	@echo "  Build & Clean:"
	@echo "    all                      - Run format, release, lint, test, gcov, and examples"
	@echo "    clean                    - Remove build artifacts"
	@echo ""
	@echo "  Options:"
	@echo "    SKIP_CONTAINER=1         - Run tools on host instead of in container"
