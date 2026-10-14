# Helper functions for space handling
s? = $(subst $(empty) ,?,$1)
?s = $(subst ?, ,$1)
dirx = $(call ?s,$(dir $(call s?,$1)))

# Base directory
export HERE = $(patsubst %/,%,$(call dirx,$(abspath $(lastword $(MAKEFILE_LIST)))))

# RISC-V toolchain configuration
RVTOOLPREFIX=/opt/riscv/bin/riscv64-unknown-linux-gnu-
RVGCC=$(RVTOOLPREFIX)gcc 
RVOBJCOPY=$(RVTOOLPREFIX)objcopy 

# Build configuration
TSCALE  := 1ns/1ns
OUT_DIR := out
OUT_MT  ?= 12
SIM_MT  ?= 4
VERILATOR_EXTRA_FLAGS ?=

# Compiler flags
GCC_FLAGS=-march=rv32ia_zicsr -mabi=ilp32 -static -nostdlib -nostartfiles -mcmodel=medany -g

# Detect OS
UNAME_S := $(shell uname -s)

# OS-specific configurations
ifeq ($(UNAME_S),Linux)
    # Linux-specific flags
    VERILATOR_FLAGS=-j $(OUT_MT) \
        --threads $(SIM_MT) \
        --timescale $(TSCALE) \
        --top-module tb \
        --cc --exe --build \
        --assert \
        --x-initial-edge \
        --timing \
        --trace \
        --trace-underscore \
        -Wno-WIDTH \
        -Wno-UNSIGNED \
        -Wno-UNOPTTHREADS \
        -Wno-CASEOVERLAP \
        -Wno-UNOPTFLAT \
        -O2 \
        -LDFLAGS -lcppdap \
        -LDFLAGS -ldw \
        -LDFLAGS -lelf \
        -LDFLAGS -ljsoncpp \
        -CFLAGS -std=c++20
else ifeq ($(UNAME_S),Darwin)
    # macOS-specific flags
    VERILATOR_FLAGS=-j $(OUT_MT) \
        --threads $(SIM_MT) \
        --timescale $(TSCALE) \
        --top-module tb \
        --cc --exe --build \
        --assert \
        --x-initial-edge \
        --timing \
        --trace \
        --trace-underscore \
        -Wno-WIDTH \
        -Wno-UNSIGNED \
        -Wno-UNOPTTHREADS \
        -Wno-CASEOVERLAP \
        -Wno-UNOPTFLAT \
        -O2 \
        -LDFLAGS -lcppdap \
        -LDFLAGS -ljsoncpp \
        -CFLAGS "-std=c++20 -I/usr/local/include" \
        -LDFLAGS "-L/usr/local/lib" \
        -DCMAKE_MACOSX_RPATH=1
endif

VERILATOR_FLAGS += $(VERILATOR_EXTRA_FLAGS)

# Source files
ASSEMBLY_SOURCES=$(notdir $(wildcard *.s))
ASSEMBLY_SOURCES+=$(addprefix "$(HERE)/src/, $(addsuffix ",$(notdir $(wildcard src/*.s))))
VERILOG_SOURCES:=$(addprefix "$(HERE)/, $(addsuffix ",$(notdir $(wildcard *.v *.sv *.c *.cpp))))
VERILOG_SOURCES+=$(addprefix "$(HERE)/src/, $(addsuffix ",$(notdir $(wildcard src/*.v src/*.sv src/*.c src/*.cpp))))
SMOKE_SOURCES="$(HERE)/"*.v "$(HERE)/"*.sv \
    "$(HERE)/src/base64.cpp" "$(HERE)/src/dapDefines.cpp" \
    "$(HERE)/src/pcToLine.cpp" "$(HERE)/src/smoke.cpp"

.PHONY: clean build_% test smoke_%

clean:
	rm -rf $(OUT_DIR)

$(OUT_DIR):
	mkdir -p $@

assembly_%: $(OUT_DIR)
ifeq ($(strip $(ASSEMBLY_SOURCES)),)
	@echo "No assembly sources found"
else
	$(RVGCC) $(GCC_FLAGS) -o $(OUT_DIR)/$(basename $*) -T mmio.ld $(ASSEMBLY_SOURCES)
	$(RVOBJCOPY) -O binary  $(OUT_DIR)/$*  $(OUT_DIR)/$*.bin
endif

verilog_%: $(OUT_DIR)
ifeq ($(strip $(VERILOG_SOURCES)),)
	@echo "No verilog sources found"
else
	cd $(OUT_DIR) && verilator $(VERILATOR_FLAGS) $(VERILOG_SOURCES)
	rm -f Vtb
	ln $(OUT_DIR)/obj_dir/Vtb Vtb
endif

build_%: $(OUT_DIR) assembly_% verilog_%
	@echo "Done"

smoke_%: $(OUT_DIR) assembly_%
	cd $(OUT_DIR) && verilator $(VERILATOR_FLAGS) $(SMOKE_SOURCES)
	rm -f Vtb
	ln $(OUT_DIR)/obj_dir/Vtb Vtb

test: smoke_gecko
	./Vtb