# NVDLA + CVA6 RISC-V SoC on ZCU104

> A fully custom **CVA6 (Ariane) RISC-V 64-bit SoC** integrated with **NVIDIA's NVDLA (nv_small)** deep learning accelerator, synthesized on a Xilinx ZCU104 FPGA using the [LiteX](https://github.com/enjoy-digital/litex) framework. Runs an MNIST inference pipeline — conv1/pool1 in SW on the CPU, conv2/pool2 on NVDLA hardware, FC in SW.

## 📋 Project Status

| Phase | Status | Description |
|-------|--------|-------------|
| **Phase 1** | ✅ Complete | CVA6 64-bit RISC-V SoC booting on ZCU104 |
| **Phase 2** | 🔧 In Progress | NVDLA_small integration + MNIST inference |

> **Call for contributors!** The NVDLA bare-metal firmware (CSR driver + DMA) is hand-written from RTL. There may be bugs in the CDMA/CACC/SDP/PDP sequencing. If you have NVDLA experience, PRs and issues are very welcome.

---

## 🏗️ Architecture

```
  ZCU104 FPGA (PL)  @  50 MHz
  ┌──────────────────────────────────────────────────────┐
  │  CVA6 RV64IMAC                                       │
  │  │  Wishbone 32-bit bus                              │
  │  ├── ROM  0x10000000  128KB  (LiteX BIOS)            │
  │  ├── SRAM 0x20000000    8KB                           │
  │  ├── BRAM 0x40000000    1MB  (firmware + DMA bufs)   │
  │  ├── CSR  0x80000000   64KB  (UART/Timer/LEDs)       │
  │  └── NVDLA 0x80010000  64KB  (register space)        │
  │       │  Wishbone → APB → NV_NVDLA_apb2csb → CSB    │
  │       └── DMA: NVDLA AXI master → Wishbone → BRAM    │
  └──────────────────────────────────────────────────────┘
```

### NVDLA Inference Buffer Layout (1MB BRAM @ 0x40000000)
| Region | Address | Size | Contents |
|--------|---------|------|----------|
| Firmware stack/BSS | `0x40000000` | 256 KB | CVA6 stack + BSS arrays |
| Weights | `0x40040000` | 256 KB | conv2 + FC weights |
| BUF0 | `0x40080000` | 256 KB | SW pool1 out / NVDLA pool2 out |
| BUF1 | `0x400C0000` | 256 KB | NVDLA conv2 out |

---

## 📁 Directory Structure

```
NVDLA_RISC/
│
├── soc/
│   ├── zcu104_cva6.py       ← Main SoC definition (CVA6 + NVDLA + BRAM, 50 MHz)
│   ├── nvdla_wrapper.py     ← LiteX module: Wishbone→APB→CSB bridge + DMA
│   └── build/zcu104_cva6/
│       ├── gateware/
│       │   └── xilinx_zcu104.bit  ← Bitstream (program this onto the FPGA)
│       ├── csr.csv               ← Peripheral base addresses (UART, Timer…)
│       └── csr.json
│
├── firmware_old/
│   ├── main.c               ← MNIST inference top-level (edit this to change logic)
│   ├── weights.c            ← INT8 quantized LeNet-5 weights (auto-generated)
│   ├── test_image.h         ← 28×28 MNIST test digit
│   ├── nvdla_driver/
│   │   ├── nvdla_drv.h      ← Driver API (nvdla_run_conv, nvdla_run_pool…)
│   │   ├── nvdla_drv.c      ← Register-level NVDLA driver (CDMA/CSC/CMAC/CACC/SDP/PDP)
│   │   └── nvdla_regs.h     ← All NVDLA CSR offsets (base: 0x80010000)
│   ├── bsp/
│   │   ├── uart.h           ← UART base address + API (update if csr.csv changes)
│   │   ├── uart.c           ← Polled UART TX/RX
│   │   ├── crt0.S           ← Startup: set sp, copy .data, zero .bss, call main
│   │   └── memutils.c       ← memcpy / memset (no libc)
│   ├── link.ld              ← Linker: ROM @0x10000000 (for --firmware-init builds)
│   ├── link_ram.ld          ← Linker: BRAM @0x40000000 (for litex_term serial upload)
│   └── Makefile
│       make                 → firmware.bin       (ROM bake-in)
│       make serial          → firmware_serial.bin (serial upload)
│
├── nvdla/hw/outdir/nv_small/vmod/   ← 528 generated NVDLA Verilog files
│
├── scripts/
│   ├── program_fpga.sh      ← Programs xilinx_zcu104.bit via JTAG
│   ├── upload_firmware.sh   ← Uploads firmware_serial.bin via litex_term
│   └── setup_env.sh         ← Activates .venv + sets PATH
│
└── deps/                    ← LiteX ecosystem (litex, migen, litex-boards, pythondata-cpu-cva6…)
```

---

## 🚀 Quickstart

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Python | 3.10 | in `.venv/` |
| RISC-V GCC 64-bit | 8.3.0 | at `/home/harsh/riscv/bin/` |
| Vivado | 2022.2 | for bitstream builds only |
| Minicom / litex_term | any | for serial console |

---

### A. First-time FPGA setup (one Vivado run ever)

```bash
# 1. Activate the environment
source scripts/setup_env.sh

# 2. Build SoC + bitstream  (~90 min, only needed once or when SoC changes)
python3 soc/zcu104_cva6.py --with-nvdla --build

# 3. Program FPGA
scripts/program_fpga.sh
```

---

### B. Day-to-day firmware iteration (no Vivado needed)

```bash
# Rebuild firmware after editing firmware_old/main.c
make -C firmware_old serial

# Upload + run  (litex_term handles timing automatically — no racing!)
scripts/upload_firmware.sh /dev/ttyUSB3
```

`litex_term` acts as both the uploader and serial console. When the BIOS prints "Booting from serial…" it auto-responds with XMODEM, loads the binary to `0x40000000`, and jumps to it. You'll see firmware output directly in the terminal.

> ⚠️ **Known Issue: Serial firmware upload is currently not working.**
> The `litex_term` / XMODEM serial boot flow has not been successfully verified yet — the BIOS boot window and serial handshake are not reliably completing.
>
> **Current workaround:** Every firmware change requires a full Vivado rebuild with the new binary baked into ROM via `--firmware-init`:
> ```bash
> make -C firmware_old          # builds firmware.bin (ROM-linked)
> python3 soc/zcu104_cva6.py --with-nvdla --firmware-init firmware_old/firmware.bin --build
> scripts/program_fpga.sh
> ```
> This takes ~90 minutes per iteration. Fixing the serial upload path is a priority contribution opportunity — see the contributor section above.


---

### C. Changing the SoC (requires Vivado re-run)

Edit `soc/zcu104_cva6.py` or `soc/nvdla_wrapper.py`, then:
```bash
python3 soc/zcu104_cva6.py --with-nvdla --build
scripts/program_fpga.sh
```

> ⚠️ After any SoC rebuild, check `soc/build/zcu104_cva6/csr.csv` and update `firmware_old/bsp/uart.h` (`UART_BASE`) if the UART address changed.

---

## 🔧 How to Make Common Changes

### Change the test image or expected label
Edit `firmware_old/test_image.h` — replace the `mnist_test_img[]` array and `mnist_test_label`.

### Change NVDLA network layers
Edit `firmware_old/main.c`. The conv2 and pool2 ops call `nvdla_run_conv()` and `nvdla_run_pool()` with a config struct — adjust dimensions, addresses and strides there.

### Change NVDLA clock speed
In `soc/zcu104_cva6.py`, change the default `sys_clk_freq=50e6`. **Note:** increasing the clock may cause timing violations. Vivado rebuild required.

### Update weights
Replace `firmware_old/weights.c` with a new auto-generated file containing `conv1_weights`, `conv2_weights`, `fc_weights` and their biases as `const int8_t` / `const int32_t` arrays.

### Add a new peripheral
Add it to `soc/zcu104_cva6.py`, rebuild with Vivado, then check `csr.csv` for its new base address and add it to your firmware.

---

## 🐛 Debugging Tips

**Nothing appears on serial console**
Check `soc/build/zcu104_cva6/csr.csv` for the current `uart` base address and make sure `firmware_old/bsp/uart.h` matches:
```bash
grep "^csr_base,uart" soc/build/zcu104_cva6/csr.csv
```

**NVDLA read returns 0x00**
The NVDLA CSR bus uses 32-bit big-endian so a single-byte read of `0x00` at `0x80010000` is correct (MSB of the 32-bit HW_VERSION). Read 4 bytes:
```
litex> mem_read 0x80011000 4
```
Should return `00 30 30 31` (version 0.0.1).

**Firmware hangs at NVDLA layer**
`nvdla_wait_done()` polls with a 20-second timeout. If it times out, check:
- Weights are loaded to a valid BRAM address before starting the layer
- `in_channels` and `out_channels` are multiples of `ATOMIC_C=8`
- DMA addresses are 32-byte aligned

---

## 📖 References

- [LiteX](https://github.com/enjoy-digital/litex) — SoC builder
- [CVA6 / Ariane](https://github.com/openhwgroup/cva6) — 64-bit RISC-V CPU
- [NVDLA Open Source](https://nvdla.org) / [nvdla/hw](https://github.com/nvdla/hw) — DLA RTL
- [LiteX-Boards ZCU104](https://github.com/litex-hub/litex-boards) — Board support
