#!/bin/bash
# ============================================================================
# Environment Setup Script for LiteX CVA6 RISC-V Project
# Source this file:  source scripts/setup_env.sh
# ============================================================================

PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Activate Python virtual environment
if [ -f "$PROJ_ROOT/.venv/bin/activate" ]; then
    source "$PROJ_ROOT/.venv/bin/activate"
    echo "[OK] Python venv activated: $(python3 --version)"
else
    echo "[ERROR] Virtual environment not found at $PROJ_ROOT/.venv"
    return 1
fi

# RISC-V Toolchain
RISCV_PATH="/home/harsh/riscv"
if [ -d "$RISCV_PATH/bin" ]; then
    export PATH="$RISCV_PATH/bin:$PATH"
    echo "[OK] RISC-V toolchain: $(riscv64-unknown-elf-gcc --version | head -1)"
else
    echo "[WARN] RISC-V toolchain not found at $RISCV_PATH"
fi

# Xilinx Vivado
VIVADO_PATH="/tools/Xilinx/Vivado/2022.2"
if [ -d "$VIVADO_PATH" ]; then
    source "$VIVADO_PATH/settings64.sh"
    echo "[OK] Vivado: $(vivado -version 2>/dev/null | head -1)"
else
    echo "[WARN] Vivado not found at $VIVADO_PATH"
fi

# Project root
export NVDLA_RISC_ROOT="$PROJ_ROOT"
echo "[OK] Project root: $PROJ_ROOT"
echo ""
echo "Environment ready. Run your LiteX commands now."
