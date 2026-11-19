PALM_TOOLCHAIN_ROOT ?= ../palm-toolchain
BEARSSL_SRC ?= $(abspath bearssl-0.6)
INSTALLER ?= local/WiFi_Installer1.2.prc
TOOLCHAIN := $(abspath $(PALM_TOOLCHAIN_ROOT))
PREFIX := $(if $(PALM_TOOLCHAIN_PREFIX),$(PALM_TOOLCHAIN_PREFIX),$(TOOLCHAIN)/.toolchain/prefix)
ARM := $(PREFIX)/bin/arm-none-eabi-gcc
M68K := $(PREFIX)/bin/m68k-none-elf-gcc
SDK := $(PREFIX)/palmdev/sdk/include
SDK_FLAGS := -isystem$(SDK) $(addprefix -isystem,$(addprefix $(SDK)/,Core Core/Hardware Core/System Core/UI Dynamic Libraries))
M68K_ABI := -m68000 -mno-align-int -mpcrel -fpic -fshort-enums -mshort
M68K_FLAGS := $(M68K_ABI) -std=gnu17 -Os -Wall -Wextra -Wno-multichar -Wno-old-style-declaration \
	-ffunction-sections -fdata-sections $(SDK_FLAGS)
M68K_LINK := $(M68K_ABI) -nostartfiles -Wl,--gc-sections,--emit-relocs,-T,app/linker.lkr
ARM_FLAGS := -march=armv5te -mtune=xscale -mthumb -mthumb-interwork -ffixed-r9 -fpie -fvisibility=hidden -Os -Wall -Wextra -ffreestanding \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
BEARSSL_INCLUDES := -I$(BEARSSL_SRC)/inc -I$(BEARSSL_SRC)/src
BEARSSL_MODULES := hash/sha1 codec/dec32be codec/enc32be codec/dec32le codec/enc32le \
	aead/ccm symcipher/aes_ct symcipher/aes_ct_dec symcipher/aes_ct_cbcdec
SHIM_INPUTS := driver/hardware.c driver/link_entry.S wpa2/rsn_supplicant.c wpa2/ccmp_station.c wpa2/arm_memory.c \
	$(addprefix $(BEARSSL_SRC)/src/,$(addsuffix .c,$(BEARSSL_MODULES))) \
	build/aes_ct_enc.o $(BEARSSL_SRC)/src/symcipher/aes_ct_ctrcbc.c

.PHONY: all package installer patch-prc preview test clean
all: build/E2WPA2Test.prc build/hardware.bin build/setup.bin

build:
	mkdir -p $@

build/main.o: app/main.c driver/hardware.h Makefile | build
	$(M68K) $(M68K_FLAGS) -Idriver -c $< -o $@

build/startup.o: app/startup.c Makefile | build
	$(M68K) $(M68K_FLAGS) -c $< -o $@

build/install:
	mkdir -p $@

build/install/main.o: installer/main.c installer/patch.h Makefile | build/install
	$(M68K) $(M68K_FLAGS) -isystem$(SDK)/Extensions/ExpansionMgr -c $< -o $@

build/install/patch.o: installer/patch.c installer/patch.h Makefile | build/install
	$(M68K) $(M68K_FLAGS) -c $< -o $@

build/code0001.elf: build/main.o
build/install/code0001.elf: build/install/main.o build/install/patch.o

build/code0001.elf build/install/code0001.elf: build/startup.o app/linker.lkr
	$(M68K) $(M68K_LINK) -o $@.stage1 $(filter %.o,$^)
	node $(TOOLCHAIN)/tools/generate-m68k-relocs.mjs $@.stage1 $(@D)/relocs.s $(PREFIX)/bin/m68k-none-elf-readelf
	$(M68K) $(M68K_ABI) -c $(@D)/relocs.s -o $(@D)/relocs.o
	$(M68K) $(M68K_LINK) -o $@ $(filter %.o,$^) $(@D)/relocs.o

build/code0001.bin build/install/code0001.bin: %.bin: %.elf
	$(PREFIX)/bin/m68k-none-elf-objcopy -O binary -j .vec -j .text $< $@

build/E2WPA2Test.ro: app/app.rcp build/code0001.bin
	cd build && $(PREFIX)/bin/pilrc -ro -o E2WPA2Test.ro -name 'E2 WPA2 Test' -creator E2WT -type appl ../app/app.rcp

build/E2WPA2Test.prc: build/E2WPA2Test.ro
	$(PREFIX)/bin/build-prc --no-check -o $@ -n 'E2 WPA2 Test' -c E2WT -t appl $<

build/install/base.ro: installer/app.rcp build/install/code0001.bin
	cd build/install && $(PREFIX)/bin/pilrc -ro -o base.ro -name 'E2 WPA2 Installer' -creator E2WI -type appl ../../installer/app.rcp

build/install/base.prc: build/install/base.ro
	$(PREFIX)/bin/build-prc --no-check -o $@ -n 'E2 WPA2 Installer' -c E2WI -t appl $<

build/nonce_seed.h: | build
	python3 tools/patch_installer.py seed $@

# ARM state gives the bitsliced round code more registers.
build/aes_ct_enc.o: $(BEARSSL_SRC)/src/symcipher/aes_ct_enc.c $(BEARSSL_SRC)/src/inner.h $(wildcard $(BEARSSL_SRC)/inc/*.h) wpa2/string.h Makefile | build
	$(ARM) $(ARM_FLAGS) -marm -Iwpa2 $(BEARSSL_INCLUDES) -c $< -o $@

build/hardware.elf: $(SHIM_INPUTS) build/nonce_seed.h driver/hardware.h driver/profile.h panel/credential.h wpa2/rsn_supplicant.h wpa2/ccmp_station.h \
		    driver/stock.ld wpa2/palmone_rsn_wire.h wpa2/palmone_ccmp_wire.h wpa2/probe_result.h wpa2/string.h Makefile | build
	$(ARM) $(ARM_FLAGS) -DPALMONE_E2_ARM_SHA1 -Ibuild -Iwpa2 $(BEARSSL_INCLUDES) \
		-nostdlib -Wl,--gc-sections,--pic-veneer,-T,driver/stock.ld,-e,diagEntry,--defsym,stockTransmit=0xb524 \
		$(SHIM_INPUTS) -lgcc -o $@

build/hardware.bin: build/hardware.elf
	$(PREFIX)/bin/arm-none-eabi-objcopy -O binary -j .text $< $@

build/setup.elf: panel/setup.c driver/hardware.h driver/profile.h panel/credential.h panel/setup_entry.S panel/setup.ld Makefile | build
	$(M68K) $(M68K_FLAGS) -nostdlib -Wl,--gc-sections,-T,panel/setup.ld \
		-include driver/hardware.h panel/setup.c panel/setup_entry.S -o $@

build/setup.bin: build/setup.elf
	$(PREFIX)/bin/m68k-none-elf-objcopy -O binary -j .text $< $@

package: all
	@test -f "$(INSTALLER)" || (echo "Place WiFi_Installer1.2.prc in local/ or set INSTALLER to its path" >&2; exit 1)
	python3 tools/patch_installer.py package "$(INSTALLER)" build/hardware.bin build/setup.bin build

installer: package build/install/base.prc
	python3 tools/build_single_prc.py build/install/base.prc build build/E2WPA2Installer.prc
	node $(TOOLCHAIN)/tools/validate-prc.mjs build/E2WPA2Installer.prc 'E2 WPA2 Installer' appl E2WI

PATCH_PAYLOAD ?= build
patch-prc: all build/install/base.prc
	python3 tools/build_patch_prc.py build/install/base.prc "$(PATCH_PAYLOAD)" build/E2WPA2Patcher.prc
	node $(TOOLCHAIN)/tools/validate-prc.mjs build/E2WPA2Patcher.prc 'E2 WPA2 Installer' appl E2WI

PREVIEW_ZIP ?= build/PalmOne-E2-WPA2-Preview.zip
preview: all build/install/base.prc
	python3 tools/release_bundle.py "$(PREVIEW_ZIP)" "$(BEARSSL_SRC)"

test: all build/install/base.prc
	node $(TOOLCHAIN)/tools/validate-prc.mjs build/E2WPA2Test.prc 'E2 WPA2 Test' appl E2WT
	node $(TOOLCHAIN)/tools/validate-prc.mjs build/install/base.prc 'E2 WPA2 Installer' appl E2WI

clean:
	rm -rf build
