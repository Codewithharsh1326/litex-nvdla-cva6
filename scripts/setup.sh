#!/bin/bash
# setup.sh — Bootstrap script for litex-nvdla-cva6
#
# Run this once after cloning to restore all large dependencies:
#   bash scripts/setup.sh
#
# What it does:
#   1. Creates a Python 3.10 virtual environment (.venv)
#   2. Installs the LiteX ecosystem into deps/ (litex, migen, litex-boards, etc.)
#   3. Clones the NVDLA nv_small RTL into nvdla/hw/ and builds it

set -e
cd "$(dirname "$0")/.."   # run from project root regardless of where called from

PYTHON=${PYTHON:-python3.10}
RISCV_GCC=${RISCV_GCC:-/home/harsh/riscv/bin/riscv64-unknown-elf-gcc}

echo "========================================================"
echo " litex-nvdla-cva6 setup"
echo "========================================================"

# ── Step 1: Python venv ──────────────────────────────────────
echo ""
echo "[1/3] Creating Python virtual environment (.venv)..."
if [ ! -d ".venv" ]; then
    $PYTHON -m venv .venv
fi
source .venv/bin/activate

pip install --upgrade pip meson ninja 2>&1 | tail -3

# ── Step 2: LiteX ecosystem ───────────────────────────────────
echo ""
echo "[2/3] Installing LiteX ecosystem into deps/ ..."
mkdir -p deps
cd deps

if [ ! -f "litex_setup.py" ]; then
    curl -o litex_setup.py \
        https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
fi

$PYTHON litex_setup.py \
    --init --install \
    --cores migen litex litex-boards \
            pythondata-cpu-cva6 \
            pythondata-software-picolibc \
            pythondata-software-compiler_rt \
            litedram liteeth litescope

cd ..

# ── Step 3: NVDLA nv_small RTL ───────────────────────────────
echo ""
echo "[3/3] Cloning NVDLA hw repo and building nv_small RTL..."
mkdir -p nvdla
cd nvdla

if [ ! -d "hw" ]; then
    git clone https://github.com/nvdla/hw.git hw
fi

cd hw

echo ""
echo "  Building NVDLA nv_small RTL (requires Python 2, Perl, Java in PATH)..."
echo "  If tmake build fails, pre-built RTL may already exist in nvdla/hw/outdir/."

if command -v tmake &>/dev/null || [ -f tools/bin/tmake ]; then
    make 2>&1 | tail -5 || true
    ./tools/bin/tmake -build vmod 2>&1 | tail -10 || \
        echo "  [WARN] tmake failed — check Python2/Perl/Java. Pre-built RTL in outdir/ if present."
else
    echo "  [WARN] tmake not found. Skipping RTL build."
    echo "  If outdir/nv_small/vmod/ is missing, install Java + run:"
    echo "    cd nvdla/hw && make && ./tools/bin/tmake -build vmod"
fi

cd ../..

# ── Done ──────────────────────────────────────────────────────
echo ""
echo "========================================================"
echo " Setup complete!"
echo ""
echo " Next steps:"
echo "   source .venv/bin/activate"
echo "   python3 soc/zcu104_cva6.py --with-nvdla --build"
echo "   scripts/program_fpga.sh"
echo ""
echo " RISC-V toolchain expected at: $RISCV_GCC"
echo " Override with: RISCV_GCC=/path/to/gcc bash scripts/setup.sh"
echo "========================================================"
