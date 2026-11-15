# E2 Control: reverse engineering (steps 1 to 3) and Kindle firmware (steps 4 and 5).
# Only fw-write touches the device.

PYTHON = python3 -I
GHIDRA_HOME ?= $(HOME)/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC
export GHIDRA_HOME
VARIANTS := $(patsubst firmware/e2control-%.bin,%,$(wildcard firmware/e2control-*.bin))
variant ?= zoom-phone-tablet
ROM = extracted/rom/rom.bin

.PHONY: help clean test firmware-unpack ghidra-score ghidra-import fw-build fw-write

help: ## Print this help message
	@printf '\033[01;32mE2 Control — reverse engineering and Kindle firmware\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'
	@printf "\n\033[33mVariants:\033[0m (default: $(variant))\n  $(VARIANTS)\n"

clean: ## Remove generated files: extracted/, ghidra/project/, fw/out/
	rm -rf extracted ghidra/project fw/out

test: ## Run all host tests (no device, no Docker; needs the images in firmware/)
	mkdir -p fw/out
	$(CC) -std=c99 -Wall -Wextra -Werror -Ifw/src fw/src/kc_*.c fw/tests/test_kc.c -o fw/out/test_kc
	fw/out/test_kc
	$(PYTHON) tools/test_jlimg.py
	$(PYTHON) tools/test_e2boot.py

# ── Reverse engineering ──────────────────────────────────────────────────────

firmware-unpack: $(VARIANTS:%=extracted/%/app.bin) ## [STEP 1] Decrypt and split every image in firmware/ into extracted/<variant>/

extracted/%/app.bin: firmware/e2control-%.bin tools/jlimg.py
	$(PYTHON) tools/jlimg.py $< extracted/$*

$(ROM): sdk/rom.lst tools/romlst.py   # the mask ROM and its labels, from the vendor listing
	$(PYTHON) tools/romlst.py $< extracted/rom

ghidra-score: $(ROM) ## [STEP 2] Install the q32s Ghidra module and check its decoder against the vendor ROM listing
	ghidra/score.sh $(top)

ghidra-import: extracted/$(variant)/app.bin $(ROM) ## [STEP 3] Analyze one variant into ghidra/project/E2 (variant=kobo)
	ghidra/setup.sh
	ghidra/import.sh extracted/$(variant)

# ── Kindle firmware ──────────────────────────────────────────────────────────

fw-build: ## [STEP 4] Build the app (Docker) and pack it into fw/out/e2-kindle.bin (variant=<your unit's>)
	@test -f firmware/e2control-$(variant).bin || { echo "missing firmware/e2control-$(variant).bin: the image of your unit's variant (README.md, Vendor firmware images)"; exit 1; }
	fw/build.sh
	$(PYTHON) tools/jlimg.py pack firmware/e2control-$(variant).bin fw/out/app.bin fw/out/e2-kindle.bin

fw-write: ## [STEP 5] WRITES TO THE DEVICE: wait for the E2 on USB, flash fw/out/e2-kindle.bin, reboot (needs reboot=1)
	@test "$(reboot)" = 1 || { echo "fw-write writes to the device as soon as it appears, then reboots it. usage: make fw-write reboot=1"; exit 1; }
	$(PYTHON) tools/e2boot.py rescue fw/out/e2-kindle.bin --yes
