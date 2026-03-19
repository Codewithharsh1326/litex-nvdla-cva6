#!/bin/bash
# upload_firmware.sh — Upload firmware to ZCU104 via LiteX BIOS serial boot
#
# WORKFLOW:
#   1. Build BIOS bitstream once:  python3 soc/zcu104_cva6.py --with-nvdla --build
#   2. Program FPGA once:          scripts/program_fpga.sh
#   3. Every firmware iteration:
#        cd firmware_old && make serial     # rebuild firmware (fast)
#        scripts/upload_firmware.sh         # upload and run instantly
#
# litex_term handles timing automatically — you don't need to be fast!
# It waits for the BIOS boot trigger, then uploads via XMODEM.
# After upload the firmware starts immediately.

set -e

PORT="${1:-/dev/ttyUSB3}"
BAUD="${2:-115200}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FW="$SCRIPT_DIR/../firmware_old/firmware_serial.bin"

# --- Check firmware exists ---
if [ ! -f "$FW" ]; then
    echo "[!] firmware_serial.bin not found — building it now..."
    make -C "$SCRIPT_DIR/../firmware_old" serial
fi

echo "=================================================="
echo " NVDLA Firmware Uploader"
echo "=================================================="
echo " Port   : $PORT"
echo " Baud   : $BAUD"
echo " Binary : $FW  ($(wc -c < "$FW") bytes)"
echo ""
echo " Power-cycle or reset the board now if needed."
echo " litex_term will auto-detect the BIOS boot prompt."
echo " Just wait — no timing needed on your side."
echo "=================================================="

# Activate venv for litex_term
if [ -f "$SCRIPT_DIR/../.venv/bin/activate" ]; then
    source "$SCRIPT_DIR/../.venv/bin/activate"
fi

# litex_term: --kernel auto-uploads when BIOS says "Booting from serial..."
litex_term \
    --kernel "$FW" \
    --kernel-adr 0x40000000 \
    "$PORT" \
    --speed "$BAUD"
