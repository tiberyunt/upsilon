.DELETE_ON_ERROR:
CC ?= cc
PYTHON ?= python3
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wno-parentheses -frounding-math -ffp-contract=off
LDLIBS += -lm
BUILD ?= build
TOOL = $(BUILD)/upsilon
AS = $(BUILD)/upsilon-as
LD = $(BUILD)/upsilon-ld
ASM_CORE = assembler/assembler.c assembler/source.c common/isa.c
ASM_SOURCES = assembler/main.c $(ASM_CORE)
SOURCES = $(wildcard simulator/*.c) $(ASM_CORE)
CORE = $(filter-out simulator/main.c,$(SOURCES))
HEADERS = $(wildcard assembler/*.h common/*.h simulator/*.h)
CPPFLAGS += -Iassembler -Icommon -Isimulator
export PYTHONPATH := $(CURDIR)/tests/reference:$(PYTHONPATH)
GNU_DIR ?= ../upsilon-gnu
NETBSD_DIR ?= ../upsilon-netbsd

.PHONY: all test test-c examples rom hello basic-rom basic basic-demo firmware-rom firmware firmware-demo clean
all: assembler linker simulator

.PHONY: assembler linker simulator
assembler: $(AS)
linker: $(LD)
simulator: $(TOOL)

$(BUILD):
	mkdir -p $@

$(AS): $(ASM_SOURCES) $(wildcard assembler/*.h common/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ASM_SOURCES) $(LDLIBS) -o $@

$(LD): $(AS)
	cp $< $@

$(BUILD)/firmware_image.h: $(BUILD)/firmware_rom.bin $(AS)
	$(AS) embed $< -o $@

$(TOOL): $(SOURCES) $(HEADERS) $(BUILD)/firmware_image.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(BUILD) $(SOURCES) $(LDLIBS) -o $@

test: firmware-rom test-c dos-image
	$(PYTHON) -m unittest discover -s tests -v

test-c: $(BUILD)/test_core $(BUILD)/test_boot
	$(BUILD)/test_core
	$(BUILD)/test_boot

$(BUILD)/test_core: tests/test_core.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isimulator tests/test_core.c $(CORE) $(LDLIBS) -o $@

examples: $(TOOL)
	$(TOOL) run examples/fibonacci.s
	$(TOOL) run examples/factorial.s
	$(TOOL) run examples/memory.s

rom: $(AS)
	$(AS) asm examples/hello_rom.s -o $(BUILD)/hello_rom.bin --map $(BUILD)/hello_rom.map.json
hello: rom $(TOOL)
	$(TOOL) run $(BUILD)/hello_rom.bin --binary --boot --serial

basic-rom: $(AS)
	$(AS) asm examples/basic_rom.s -o $(BUILD)/basic_rom.bin --map $(BUILD)/basic_rom.map.json
basic: basic-rom $(TOOL)
	$(TOOL) run $(BUILD)/basic_rom.bin --binary --boot --keyboard
basic-demo: basic-rom $(TOOL)
	$(TOOL) run $(BUILD)/basic_rom.bin --binary --boot --keyboard < examples/basic_demo.bas

$(BUILD)/firmware_rom.s: $(AS) examples/monitor.s examples/tape_boot.inc examples/disk_boot.inc examples/basic_rom.s examples/native_assembler.s examples/native_float.inc examples/native_tables.inc
	$(AS) rom-link examples/monitor.s examples/basic_rom.s examples/native_assembler.s -o $@
$(BUILD)/firmware_rom.bin: $(BUILD)/firmware_rom.s $(AS)
	$(AS) asm $< -o $@ --map $(BUILD)/firmware_rom.map.json
firmware-rom: $(BUILD)/firmware_rom.bin $(TOOL)
firmware: firmware-rom
	$(TOOL) run $(BUILD)/firmware_rom.bin --binary --boot --keyboard --max-steps 100000000
firmware-demo: firmware-rom
	$(TOOL) run $(BUILD)/firmware_rom.bin --binary --boot --keyboard --max-steps 100000000 < examples/firmware_demo.txt

clean:
	rm -rf $(BUILD)

SANFLAGS = -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-parentheses -frounding-math -ffp-contract=off -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: sanitize
$(BUILD)/upsilon-sanitize: $(SOURCES) $(HEADERS) $(BUILD)/firmware_image.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(SANFLAGS) -I$(BUILD) $(SOURCES) $(LDLIBS) -o $@
$(BUILD)/test_core-sanitize: tests/test_core.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(SANFLAGS) -Isimulator tests/test_core.c $(CORE) $(LDLIBS) -o $@
sanitize: firmware-rom dos-image $(BUILD)/upsilon-sanitize $(BUILD)/test_core-sanitize $(BUILD)/test_boot-sanitize
	$(BUILD)/test_core-sanitize
	$(BUILD)/test_boot-sanitize
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_c_toolchain.py -v
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_tape.py -v
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_v04_c.py -v
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_dos.py -v
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_disk.py -v
	UPSILON_C=$(CURDIR)/$(BUILD)/upsilon-sanitize $(PYTHON) -m unittest discover -s tests -p test_platform.py -v


$(BUILD)/tape_hello.bin: examples/tape_hello.s $(AS)
	$(AS) asm $< --origin 0x100000 -o $@

exampletape: $(BUILD)/tape_hello.bin $(TOOL)
	$(TOOL) mktape $< -o $@ --load 0x100000 --record-size 17

.PHONY: tape-demo install
tape-demo: exampletape
	$(TOOL) -m 256 -t exampletape

PREFIX ?= /usr/local
install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TOOL) $(AS) $(LD) $(DESTDIR)$(PREFIX)/bin/

# A new install image is reproducible; an existing user reel is never replaced.
DOS_SOURCES = $(wildcard dos/*.s dos/*.inc dos/*.ASM dos/*.BAT)
.PHONY: dos dos-tape dos-image
$(BUILD)/dos_boot.bin: dos/boot.s dos/layout.inc dos/console.inc dos/filesystem.inc dos/harddisk.inc $(AS)
	$(AS) asm $< --origin 0x80000 -o $@
$(BUILD)/IO.SYS: $(DOS_SOURCES) examples/native_assembler.s examples/native_float.inc examples/native_tables.inc $(AS)
	$(AS) asm dos/io.s --origin 0x100000 -o $@ --map $(BUILD)/dos_io.map.json
$(BUILD)/COMMAND.EXE: dos/command.s $(AS)
	$(AS) asm $< --origin 0x180000 -o $@
$(BUILD)/VI.EXE: dos/vi.s $(AS)
	$(AS) asm $< --origin 0x200000 -o $@
$(BUILD)/ASM.EXE: dos/asm.s $(AS)
	$(AS) asm $< --origin 0x200000 -o $@
$(BUILD)/LINK.EXE: dos/link.s $(AS)
	$(AS) asm $< --origin 0x200000 -o $@
$(BUILD)/SYSCOPY.EXE: dos/syscopy.s $(AS)
	$(AS) asm $< --origin 0x200000 -o $@
$(BUILD)/dos-install.tap: $(BUILD)/SYSCOPY.EXE $(BUILD)/dos_boot.bin $(BUILD)/IO.SYS $(BUILD)/COMMAND.EXE $(BUILD)/VI.EXE $(BUILD)/ASM.EXE $(BUILD)/LINK.EXE dos/AUTOEXEC.BAT dos/HELLO.ASM dos/MAIN.ASM dos/GREET.ASM dos/README.TXT $(TOOL)
	$(TOOL) mktape $(BUILD)/dos_boot.bin -o $@ --load 0x80000 \
	  --add IO.SYS=$(BUILD)/IO.SYS --add COMMAND.EXE=$(BUILD)/COMMAND.EXE \
	  --add AUTOEXEC.BAT=dos/AUTOEXEC.BAT --add VI.EXE=$(BUILD)/VI.EXE \
	  --add ASM.EXE=$(BUILD)/ASM.EXE --add LINK.EXE=$(BUILD)/LINK.EXE \
	  --add HELLO.ASM=dos/HELLO.ASM --add MAIN.ASM=dos/MAIN.ASM \
	  --add GREET.ASM=dos/GREET.ASM --add README.TXT=dos/README.TXT \
	  --add SYSCOPY.EXE=$(BUILD)/SYSCOPY.EXE --add BOOT.SYS=$(BUILD)/dos_boot.bin
dos-image: $(BUILD)/dos-install.tap
dos-tape: dos-image
	@if test -e dostape; then echo "Keeping existing dostape (saved files preserved)."; else cp $(BUILD)/dos-install.tap dostape; echo "Created dostape."; fi
dos: kotltos

# KotlTOS media are separate from legacy dostape; existing user images survive.
.PHONY: kotltos kotltos-image kotltos-tape kotltos-disk
$(BUILD)/kotltos-install.tap: $(BUILD)/dos-install.tap
	cp $< $@
kotltos-image: $(BUILD)/kotltos-install.tap
kotltos-tape: kotltos-image
	@if test -e kotltos.tap; then echo "Keeping existing kotltos.tap."; else cp $(BUILD)/kotltos-install.tap kotltos.tap; echo "Created kotltos.tap."; fi
kotltos-disk: $(TOOL)
	@if test -e harddisk.qcow2; then echo "Keeping existing harddisk.qcow2."; else $(TOOL) mkdisk harddisk.qcow2 --size 128; fi
kotltos: kotltos-tape kotltos-disk
	$(TOOL) -m 256 -t kotltos.tap -d harddisk.qcow2 --max-steps 1000000000

# GNU cross tools are optional and built independently of the native tools.
.PHONY: gnu-toolchain gnu-test gnu-demo
gnu-toolchain: $(TOOL)
	$(PYTHON) $(GNU_DIR)/scripts/build.py
gnu-test: $(TOOL) dos-image
	@test -x $(GNU_DIR)/install/bin/upsilon-unknown-elf-gcc || { echo "Run make gnu-toolchain first."; exit 1; }
	$(PYTHON) $(GNU_DIR)/tests/test_toolchain.py
gnu-demo: $(TOOL)
	$(PYTHON) $(GNU_DIR)/scripts/build.py runtime
	$(TOOL) run $(GNU_DIR)/out/hello.bin --binary --origin 0x200000 --memory 16777216 --serial

.PHONY: gnu-kotltos-demo
gnu-kotltos-demo: $(TOOL) dos-image
	$(PYTHON) $(GNU_DIR)/scripts/build.py runtime
	$(PYTHON) $(GNU_DIR)/scripts/package_demo.py
	$(TOOL) -m 16 -t $(GNU_DIR)/out/gcc-demo.tap --max-steps 100000000

# Host platform checks do not require downloading NetBSD sources.
$(BUILD)/test_boot: tests/test_boot.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isimulator tests/test_boot.c $(CORE) $(LDLIBS) -o $@
$(BUILD)/test_boot-sanitize: tests/test_boot.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(SANFLAGS) -Isimulator tests/test_boot.c $(CORE) $(LDLIBS) -o $@
.PHONY: netbsd-bootstrap netbsd-demo netbsd-test
netbsd-bootstrap: $(TOOL)
	$(PYTHON) $(NETBSD_DIR)/scripts/build.py
netbsd-demo: netbsd-bootstrap
	$(TOOL) -m 32 --kernel $(NETBSD_DIR)/build/bootstrap.elf --bootargs '-s'
netbsd-test: netbsd-bootstrap test-c
	$(PYTHON) $(NETBSD_DIR)/tests/test_bootstrap.py
