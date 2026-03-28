---
name: Hardware / Firmware Bug Report
about: Create a report to help us fix SoC or bare-metal C issues
title: '[BUG] '
labels: bug, hardware
assignees: ''
---

**Describe the bug**
A clear and concise description of what the bug is (e.g., NVDLA DMA hangs, UART prints garbage, timing violation in Vivado).

**To Reproduce**
Steps to reproduce the behavior:
1. Did you run `python3 soc/zcu104_cva6.py --build`?
2. Did you compile with `make -C firmware_old`?
3. See error...

**Expected behavior**
A clear and concise description of what you expected to happen.

**Hardware & Toolchain Environment (Mandatory):**
 - Target Board: [e.g., Xilinx ZCU104]
 - Vivado Version: [e.g., 2022.2]
 - RISC-V GCC Version: [e.g., 8.3.0]
 - LiteX Version/Commit: [e.g., latest master]
 - Host OS: [e.g., Ubuntu 22.04]

**Additional context**
Add any other context about the problem here, such as serial console logs or Vivado critical warnings.
