# Contributing to litex-nvdla-cva6

First off, thank you for your interest in contributing! Integrating an NVIDIA Deep Learning Accelerator (NVDLA) with a 64-bit RISC-V CVA6 (Ariane) core via LiteX is a complex hardware-software co-design challenge, and community help is highly appreciated.

## 🎯 Current High-Priority Bounties

While all bug fixes and optimizations are welcome, we are actively looking for help with the following two critical areas:

1. **Fixing the Serial Firmware Upload (`litex_term` / XMODEM):** Currently, the XMODEM serial boot flow is failing, requiring a 90-minute full Vivado rebuild (`--firmware-init`) for every firmware change. If you have experience with LiteX BIOS, serial handshakes, or XMODEM debugging, getting `scripts/upload_firmware.sh` to work reliably is our #1 priority.
2. **NVDLA Bare-Metal Driver Verification:** The NVDLA firmware (`firmware_old/nvdla_driver/`) is hand-written from the RTL specification. There may be sequencing bugs in the CDMA, CACC, SDP, or PDP blocks. Pull requests fixing DMA alignment, register sequencing, or timing issues in the NVDLA inference pipeline are incredibly valuable.

## 🛠 Development Environment Setup

To contribute to both the gateware and the firmware, ensure your environment matches the project's requirements:

* **Python:** 3.10 (Use the provided `.venv` via `scripts/setup_env.sh`)
* **RISC-V Toolchain:** RISC-V GCC 64-bit (`riscv64-unknown-elf-gcc`, version 8.3.0 or compatible)
* **FPGA Toolchain:** Xilinx Vivado 2022.2 (Required for bitstream generation for the ZCU104 board)
* **Serial Terminal:** `minicom` or `litex_term`

## 🔄 Contribution Workflow

### 1. Modifying the Hardware (LiteX SoC / NVDLA Wrapper)
If you are changing the Wishbone/APB bridging, adding peripherals, or modifying the clock domain in `soc/zcu104_cva6.py` or `soc/nvdla_wrapper.py`:
1. Rebuild the SoC: `python3 soc/zcu104_cva6.py --with-nvdla --build`
2. Verify that `soc/build/zcu104_cva6/csr.csv` reflects your address map changes correctly.
3. Update `firmware_old/bsp/uart.h` or relevant headers if peripheral base addresses have shifted.

### 2. Modifying the Firmware (C / Drivers)
If you are tweaking the NVDLA drivers or the MNIST inference logic in `firmware_old/`:
1. Edit the relevant `.c` or `.h` files.
2. Run `make -C firmware_old serial` to verify it compiles.
3. *Note: Until the serial boot bug is fixed, you must bake the firmware into the ROM and rebuild the SoC via Vivado to test your changes on hardware.*

### 3. Submitting Your Changes
1. Fork the repository and create your branch from `main`.
2. Name your branch descriptively (e.g., `fix/xmodem-serial-boot` or `feat/nvdla-dma-alignment`).
3. Ensure your C code follows standard formatting and includes comments explaining any hardware-specific register manipulations.
4. Open a Pull Request! In your PR description, please state whether you have verified your changes in simulation or directly on a ZCU104 FPGA.

Thank you for helping us build an open-source RISC-V + NVDLA ecosystem!
