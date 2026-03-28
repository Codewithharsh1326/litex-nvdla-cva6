## Description
Briefly describe the changes and the motivation. If this fixes an open issue, please link it here.

## Domain of Change
- [ ] LiteX SoC / Python Gateware (`soc/`)
- [ ] Bare-metal Firmware / C Drivers (`firmware_old/`)
- [ ] Verilog RTL (NVDLA / Ariane integration)
- [ ] Build Scripts / Automation (`scripts/`)

## Hardware & Simulation Verification Checklist
Since this project integrates an NVDLA accelerator with a RISC-V processor, testing on actual silicon or verified simulation is crucial. Please check all that apply:

- [ ] I have successfully synthesized the bitstream for the Xilinx ZCU104 board.
- [ ] I have run the firmware on the ZCU104 and verified the expected inference output.
- [ ] I have verified that the Ariane (CVA6) core boots successfully with these changes.
- [ ] My C code changes compile without errors (`make -C firmware_old serial`).
- [ ] If changing the SoC, I verified the new peripheral addresses in `csr.csv`.
