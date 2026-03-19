#!/bin/bash
# Script to program ZCU104 FPGA with the generated bitstream
# Make sure Vivado is in the PATH

source /tools/Xilinx/Vivado/2022.2/settings64.sh

BITSTREAM="/home/harsh/Documents/NVDLA_RISC/soc/build/zcu104_cva6/gateware/xilinx_zcu104.bit"

if [ ! -f "$BITSTREAM" ]; then
    echo "Error: Bitstream not found at $BITSTREAM"
    exit 1
fi

echo "Programming FPGA..."
cat << 'TCL_EOF' > program.tcl
open_hw_manager
connect_hw_server
open_hw_target
current_hw_device [get_hw_devices xczu7_0]
refresh_hw_device -update_hw_probes false [lindex [get_hw_devices xczu7_0] 0]
set_property PROBES.FILE {} [get_hw_devices xczu7_0]
set_property FULL_PROBES.FILE {} [get_hw_devices xczu7_0]
set_property PROGRAM.FILE {/home/harsh/Documents/NVDLA_RISC/soc/build/zcu104_cva6/gateware/xilinx_zcu104.bit} [get_hw_devices xczu7_0]
program_hw_devices [get_hw_devices xczu7_0]
refresh_hw_device [lindex [get_hw_devices xczu7_0] 0]
exit
TCL_EOF

vivado -mode batch -source program.tcl
rm program.tcl
echo "Done."
