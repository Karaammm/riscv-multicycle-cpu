# RISC-V Multicycle CPU with Peripherals and Interrupts

This repository contains a multicycle RV32I processor with memory-mapped peripherals
and machine-mode interrupt support.

## Features

- A multicycle RV32I CPU and controller.
- CSR support for machine-mode interrupts (`mstatus`, `mie`, `mtvec`, `mepc`, `mcause`, and `mip`).
- A button peripheral capable of generating interrupts.
- LED and seven-segment display peripherals.
- Memory-mapped memory and peripheral access.
- A RISC-V assembly program that configures interrupts, handles button input, updates game state, and displays the result.
- Verilator/DAP integration in `src/` for simulation and debugging.

Generated simulator output, waveform files, and logs are intentionally excluded from the
repository and can be regenerated locally.

## Requirements

- GNU Make
- Verilator 5.x or later
- A RISC-V RV32 toolchain installed at `/opt/riscv/bin`, including:
  `riscv64-unknown-linux-gnu-gcc` and `riscv64-unknown-linux-gnu-objcopy`
- C++20 compiler and the libraries required by the Verilator debug adapter

The Makefile currently assumes the toolchain path above and uses the `riscv64-unknown-linux-gnu-` prefix.

### Docker option

Docker avoids installing these Linux-specific dependencies directly. Start Docker
Desktop, then build the image from this repository root:

```sh
docker build -t riscv-multicycle-cpu .
```

Run the complete build with the repository mounted so generated files remain available on
the host:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace \
  riscv-multicycle-cpu build_gecko
```

Run the resulting simulator:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace \
  --entrypoint ./Vtb \
  riscv-multicycle-cpu
```

This command starts the Verilator Debug Adapter Protocol (DAP) server. It is not a
standalone command-line test, so it normally prints nothing and waits for a debugger
client on standard input. Use it from the VS Code debugger with the project's debug
configuration, or connect another DAP client. The simulator exposes CPU registers,
memory, program counter, LEDs, seven-segment displays, and button inputs through that
protocol.

The assembly program initializes the game state to "lose", waits for a button interrupt,
and changes the state to "win" when the interrupt handler runs. The result is represented
by the LED peripheral: the program writes the red or green LED pattern through memory-mapped
I/O. It is not printed to the terminal.

The container uses Ubuntu 24.04 and installs the Debian RISC-V Linux toolchain under the
`/opt/riscv/bin` names expected by the existing Makefile.

## Build and run

From the repository root:

```sh
make build_gecko
./Vtb
```

`build_gecko` first assembles `program.s` into `out/gecko.bin`, then builds the Verilator testbench. The top-level HDL module is named `tb` in `gecko.v`, despite the filename.

For a deterministic terminal demonstration, use the smoke test:

```sh
make test
```

It drives the real simulated CPU and reports reset, initial game state, button interrupt,
CSR state, interrupt-handler result, and LED output. A successful run ends with:

```text
Simulation result: PASS
```

The Docker equivalent is:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace \
  riscv-multicycle-cpu test
```

To rebuild only the HDL simulator after an assembly image already exists:

```sh
make verilog_gecko
./Vtb
```

To remove generated output:

```sh
make clean
```

## Validation on macOS

The source was checked in this environment with:

```sh
verilator --lint-only --top-module tb --timing -Wno-fatal -Wno-DECLFILENAME *.v *.sv
```

Verilator elaborated all 20 HDL modules successfully. It reported pre-existing width, case-overlap, and multiple-driver warnings. The normal Makefile build could not complete on the host because:

1. `/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc` is not installed.
2. The Makefile's Verilator flags promote the existing `MULTIDRIVEN` warning to an error.

The Docker build was not executed in this environment because Docker Desktop's daemon was
not running. After starting Docker Desktop, use the commands above. The container may still
stop on the existing `MULTIDRIVEN` warning because the Makefile promotes it to an error;
for a temporary validation run, pass `VERILATOR_EXTRA_FLAGS=-Wno-MULTIDRIVEN` to the
build command:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e VERILATOR_EXTRA_FLAGS=-Wno-MULTIDRIVEN \
  riscv-multicycle-cpu build_gecko
```

The warning should ultimately be fixed rather than permanently hidden.

The generated `Vtb` process is a debug-capable simulator and may remain running while it
waits for a debugger connection. Stop it with `Ctrl-C` when finished.
