# Makefile for E2 Control — Firmware Reverse-Engineering Pipeline
# Targets are numbered by pipeline stage:
#   1a unpack → 1b rom → 2 ghidra → 3 score → 4 import → 5 firmware (build, test, image)
SERVICE = E2 Control

# Variables
PYTHON = python3 -I
GHIDRA_HOME ?= $(HOME)/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC
export GHIDRA_HOME
VARIANTS := $(patsubst firmware/e2control-%.bin,%,$(wildcard firmware/e2control-*.bin))
variant ?= zoom-phone-tablet

.PHONY: help clean \
        firmware-unpack firmware-rom \
        ghidra-install \
        ghidra-score \
        ghidra-import ghidra-import-all \
        fw-build fw-test fw-image

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — Firmware Reverse-Engineering Pipeline\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'
	@printf "\n\033[33mVariants:\033[0m\n  $(VARIANTS)\n"

clean: ## Remove extracted images and the Ghidra project (keeps firmware/ and the installed module)
	rm -rf extracted ghidra/project
	@echo "Cleanup complete."

# ── Stage 1 · Unpack (jl_isd.bin → app.bin, ROM listing → rom.bin) ───────────

firmware-unpack: $(VARIANTS:%=extracted/%/app.bin) ## [STEP 1a] Decrypt and split every firmware image into extracted/<variant>/

extracted/%/app.bin: firmware/e2control-%.bin tools/jlimg.py
	@echo "Unpacking $*..."
	$(PYTHON) tools/jlimg.py $< extracted/$*

firmware-rom: extracted/rom/rom.bin ## [STEP 1b] Rebuild the BD19 mask ROM + labels from sdk/rom.lst

extracted/rom/rom.bin: sdk/rom.lst tools/romlst.py
	@echo "Rebuilding mask ROM from the vendor listing..."
	$(PYTHON) tools/romlst.py $< extracted/rom

# ── Stage 2 · Ghidra processor module ────────────────────────────────────────

ghidra-install: ## [STEP  2] Compile the JieLi SLEIGH specs and copy the module into GHIDRA_HOME (restart the GUI after)
	@echo "Installing JieLi module into $(GHIDRA_HOME)..."
	ghidra/setup.sh

# ── Stage 3 · Decoder check (q32s vs vendor objdump) ─────────────────────────

ghidra-score: firmware-rom ## [STEP  3] Score the q32s decoder against the ROM listing (usage: make ghidra-score [top=30])
	ghidra/score.sh $(top)

# ── Stage 4 · Analysis (app.bin → ghidra/project/E2 + pseudo-C) ──────────────

ghidra-import: firmware-unpack firmware-rom ghidra-install ## [STEP  4] Analyze one variant into ghidra/project/E2 (usage: make ghidra-import variant=kobo)
	@echo "Analyzing $(variant)..."
	ghidra/import.sh extracted/$(variant)

ghidra-import-all: firmware-unpack firmware-rom ghidra-install ## [STEP  4] Analyze every variant into ghidra/project/E2
	for v in $(VARIANTS); do echo "Analyzing $$v..."; ghidra/import.sh extracted/$$v || exit 1; done

# ── Stage 5 · Kindle firmware ──

fw-build: ## [STEP  5] Build the firmware app.bin in the toolchain container
	fw/build.sh

fw-test: ## [STEP  5] Run the host tests for the Kindle Click logic
	mkdir -p fw/out
	$(CC) -std=c99 -Wall -Wextra -Werror -Ifw/src fw/src/kc_*.c fw/tests/test_kc.c -o fw/out/test_kc
	fw/out/test_kc

# fw/out/app.bin → flashable image built on a stock one

fw-image: ## [STEP  5] Put fw/out/app.bin into a stock image, rest byte-identical (usage: make fw-image base=firmware/e2control-zoom-phone-tablet.bin)
	@test -n "$(base)" || { echo "usage: make fw-image base=firmware/e2control-<variant>.bin"; exit 1; }
	mkdir -p fw/out
	$(PYTHON) tools/jlimg.py pack $(base) fw/out/app.bin fw/out/e2-kindle.bin
