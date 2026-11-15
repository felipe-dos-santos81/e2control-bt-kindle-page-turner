# E2 Control: reverse engineering (steps 1 to 4) and Kindle firmware (step 5).
#   1a unpack → 1b rom → 2 ghidra module → 3 score → 4 import
#   5a test → 5b build → 5c image → 5d flash
# Only fw-rescue touches the device.

PYTHON = python3 -I
GHIDRA_HOME ?= $(HOME)/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC
export GHIDRA_HOME
VARIANTS := $(patsubst firmware/e2control-%.bin,%,$(wildcard firmware/e2control-*.bin))
variant ?= zoom-phone-tablet

.PHONY: help clean test \
        firmware-unpack firmware-rom \
        ghidra-install \
        ghidra-score \
        ghidra-import ghidra-import-all \
        fw-test tools-test fw-build fw-image fw-rescue

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32mE2 Control — reverse engineering and Kindle firmware\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'
	@printf "\n\033[33mVariants:\033[0m\n  $(VARIANTS)\n"

clean: ## Remove generated files: extracted/, ghidra/project/, fw/out/
	rm -rf extracted ghidra/project fw/out
	@echo "Cleanup complete."

test: fw-test tools-test ## Run all host tests (no device, no Docker)

# ── Stage 1 · Unpack (jl_isd.bin → app.bin, ROM listing → rom.bin) ───────────

firmware-unpack: $(VARIANTS:%=extracted/%/app.bin) ## [STEP 1a] Decrypt and split every image in firmware/ into extracted/<variant>/

extracted/%/app.bin: firmware/e2control-%.bin tools/jlimg.py
	@echo "Unpacking $*..."
	$(PYTHON) tools/jlimg.py $< extracted/$*

firmware-rom: extracted/rom/rom.bin ## [STEP 1b] Rebuild the mask ROM and its labels from sdk/rom.lst

extracted/rom/rom.bin: sdk/rom.lst tools/romlst.py
	@echo "Rebuilding mask ROM from the vendor listing..."
	$(PYTHON) tools/romlst.py $< extracted/rom

# ── Stage 2 · Ghidra processor module ────────────────────────────────────────

ghidra-install: ## [STEP  2] Compile the q32s module and copy it into GHIDRA_HOME (restart the Ghidra GUI after)
	@echo "Installing JieLi module into $(GHIDRA_HOME)..."
	ghidra/setup.sh

# ── Stage 3 · Decoder check (q32s vs vendor objdump) ─────────────────────────

ghidra-score: firmware-rom ## [STEP  3] Check the q32s decoder against the ROM listing (usage: make ghidra-score [top=30])
	ghidra/score.sh $(top)

# ── Stage 4 · Analysis (app.bin → ghidra/project/E2 + pseudo-C) ──────────────

ghidra-import: firmware-unpack firmware-rom ghidra-install ## [STEP  4] Analyze one variant into ghidra/project/E2 (usage: make ghidra-import [variant=kobo])
	@echo "Analyzing $(variant)..."
	ghidra/import.sh extracted/$(variant)

ghidra-import-all: firmware-unpack firmware-rom ghidra-install ## [STEP  4] Analyze every variant into ghidra/project/E2
	for v in $(VARIANTS); do echo "Analyzing $$v..."; ghidra/import.sh extracted/$$v || exit 1; done

# ── Stage 5 · Kindle firmware (fw/src + SDK → app.bin → flash image → device) ─

fw-test: ## [STEP 5a] Run the host tests of the firmware logic (fw/src/kc_*.c)
	mkdir -p fw/out
	$(CC) -std=c99 -Wall -Wextra -Werror -Ifw/src fw/src/kc_*.c fw/tests/test_kc.c -o fw/out/test_kc
	fw/out/test_kc

tools-test: ## [STEP 5a] Run the image-packer tests (need firmware/*.bin) and the updater-client tests
	$(PYTHON) tools/test_jlimg.py
	$(PYTHON) tools/test_e2boot.py

fw-build: ## [STEP 5b] Build fw/out/app.bin in the Jieli toolchain container (needs Docker)
	fw/build.sh

fw-image: ## [STEP 5c] Put fw/out/app.bin into your unit's image → fw/out/e2-kindle.bin (usage: make fw-image base=<image>)
	@test -n "$(base)" || { echo "usage: make fw-image base=<your unit's image, e.g. firmware/e2control-zoom-phone-tablet.bin>"; exit 1; }
	mkdir -p fw/out
	$(PYTHON) tools/jlimg.py pack $(base) fw/out/app.bin fw/out/e2-kindle.bin

fw-rescue: ## [STEP 5d] WRITES TO THE DEVICE: wait for the E2 on USB, flash fw/out/e2-kindle.bin, reboot (usage: make fw-rescue yes=1)
	@test "$(yes)" = 1 || { echo "fw-rescue writes to the device as soon as it appears. usage: make fw-rescue yes=1"; exit 1; }
	$(PYTHON) tools/e2boot.py rescue fw/out/e2-kindle.bin --yes
