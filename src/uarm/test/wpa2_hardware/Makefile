PALM_TOOLCHAIN_ROOT ?= ../../../../../palm-toolchain
TOOLCHAIN := $(abspath $(PALM_TOOLCHAIN_ROOT))
PREFIX := $(if $(PALM_TOOLCHAIN_PREFIX),$(PALM_TOOLCHAIN_PREFIX),$(TOOLCHAIN)/.toolchain/prefix)
ARM := $(PREFIX)/bin/arm-none-eabi-gcc
M68K := $(PREFIX)/bin/m68k-none-elf-gcc
SDK := $(PREFIX)/palmdev/sdk/include
SDK_FLAGS := -isystem$(SDK) $(addprefix -isystem,$(addprefix $(SDK)/,Core Core/Hardware Core/System Core/UI Dynamic Libraries))
M68K_ABI := -m68000 -mno-align-int -mpcrel -fpic -fshort-enums -mshort
M68K_FLAGS := $(M68K_ABI) -std=gnu17 -Os -Wall -Wextra -Wno-multichar -Wno-old-style-declaration \
	-ffunction-sections -fdata-sections $(SDK_FLAGS)
PROBE := ../wpa2_probe
M68K_LINK := $(M68K_ABI) -nostartfiles -Wl,--gc-sections,--emit-relocs,-T,$(PROBE)/linker.lkr
ARM_FLAGS := -march=armv4t -mthumb -mthumb-interwork -ffixed-r9 -fpie -fvisibility=hidden -Os -Wall -Wextra -ffreestanding \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
BEARSSL_MODULES := hash/sha1 codec/dec32be codec/enc32be codec/dec32le codec/enc32le \
	aead/ccm symcipher/aes_ct symcipher/aes_ct_dec symcipher/aes_ct_cbcdec \
	symcipher/aes_ct_enc symcipher/aes_ct_ctrcbc
SHIM_SOURCES := hardware.c link_entry.S $(PROBE)/rsn_supplicant.c $(PROBE)/ccmp_station.c $(PROBE)/arm_memory.c \
	$(addprefix $(BEARSSL_SRC)/src/,$(addsuffix .c,$(BEARSSL_MODULES)))

.PHONY: all package preview test clean
all: build/E2WPA2Test.prc build/hardware.bin build/setup.bin

build:
	mkdir -p $@

build/main.o: main.c hardware.h Makefile | build
	$(M68K) $(M68K_FLAGS) -c $< -o $@

build/startup.o: $(PROBE)/startup.c Makefile | build
	$(M68K) $(M68K_FLAGS) -c $< -o $@

build/code0001.elf: build/main.o build/startup.o $(PROBE)/linker.lkr
	$(M68K) $(M68K_LINK) -o $@.stage1 $(filter %.o,$^)
	node $(TOOLCHAIN)/tools/generate-m68k-relocs.mjs $@.stage1 build/relocs.s $(PREFIX)/bin/m68k-none-elf-readelf
	$(M68K) $(M68K_ABI) -c build/relocs.s -o build/relocs.o
	$(M68K) $(M68K_LINK) -o $@ $(filter %.o,$^) build/relocs.o

build/code0001.bin: build/code0001.elf
	$(PREFIX)/bin/m68k-none-elf-objcopy -O binary -j .vec -j .text $< $@

build/E2WPA2Test.ro: app.rcp build/code0001.bin
	cd build && $(PREFIX)/bin/pilrc -ro -o E2WPA2Test.ro -name 'E2 WPA2 Test' -creator E2WT -type appl ../app.rcp

build/E2WPA2Test.prc: build/E2WPA2Test.ro
	$(PREFIX)/bin/build-prc --no-check -o $@ -n 'E2 WPA2 Test' -c E2WT -t appl $<

build/nonce_seed.h: package.py hardware.h | build
	python3 package.py seed $@

build/hardware.elf: $(SHIM_SOURCES) build/nonce_seed.h hardware.h profile.h ../wpa2_stock/credential.h $(PROBE)/rsn_supplicant.h $(PROBE)/ccmp_station.h \
		    ../wpa2_stock/stock.ld ../../uarm/palmone_rsn_wire.h ../../uarm/palmone_ccmp_wire.h Makefile | build
	$(ARM) $(ARM_FLAGS) -DWPA2_CLEAR_WEP_PROFILE -Ibuild -I$(PROBE) -I../../uarm -I$(BEARSSL_SRC)/inc -I$(BEARSSL_SRC)/src \
		-nostdlib -Wl,--gc-sections,--pic-veneer,-T,../wpa2_stock/stock.ld,-e,diagEntry,--defsym,stockTransmit=0xb524 \
		$(SHIM_SOURCES) -lgcc -o $@

build/hardware.bin: build/hardware.elf
	$(PREFIX)/bin/arm-none-eabi-objcopy -O binary -j .text $< $@

build/setup.elf: ../wpa2_stock/setup.c hardware.h profile.h ../wpa2_stock/credential.h ../wpa2_stock/setup_entry.S ../wpa2_stock/setup.ld Makefile | build
	$(M68K) $(M68K_FLAGS) -nostdlib -Wl,--gc-sections,-T,../wpa2_stock/setup.ld \
		-include hardware.h -DWPA2_CLEAR_WEP_PROFILE ../wpa2_stock/setup.c ../wpa2_stock/setup_entry.S -o $@

build/setup.bin: build/setup.elf
	$(PREFIX)/bin/m68k-none-elf-objcopy -O binary -j .text $< $@

package: all
	@test -n "$(INSTALLER)" || (echo "Set INSTALLER to the path of WiFi_Installer1.2.prc" >&2; exit 1)
	python3 package.py package "$(INSTALLER)" build/hardware.bin build/setup.bin build

PREVIEW_ZIP ?= build/PalmOne-E2-WPA2-Preview.zip
preview: all
	python3 release_bundle.py "$(PREVIEW_ZIP)" "$(BEARSSL_SRC)"

test: all
	node $(TOOLCHAIN)/tools/validate-prc.mjs build/E2WPA2Test.prc 'E2 WPA2 Test' appl E2WT

clean:
	rm -rf build
