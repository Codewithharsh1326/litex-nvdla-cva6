#!/usr/bin/env python3

# =============================================================================
# LiteX SoC for Xilinx ZCU104 with CVA6 (Ariane) RISC-V 64-bit CPU
# =============================================================================
#
# Based on upstream litex-boards xilinx_zcu104.py target.
# Customized for CVA6 CPU. Supports optional NVDLA_small integration.
#
# Usage:
#   # Generate Verilog only (no Vivado):
#   python3 soc/zcu104_cva6.py --no-compile
#
#   # Full build (Vivado synthesis + implementation):
#   python3 soc/zcu104_cva6.py --build
#
#   # Build and program via JTAG:
#   python3 soc/zcu104_cva6.py --build --load
#

import os
import sys

from migen import *
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.gen import *

from litex_boards.platforms import xilinx_zcu104

from litex.soc.cores.clock import *
from litex.soc.integration.soc_core import *
from litex.soc.integration.soc import SoCRegion
from litex.soc.integration.builder import *
from litex.soc.cores.led import LedChaser
from litex.soc.interconnect import axi
from litex.soc.interconnect import wishbone

# CRG (Clock Reset Generator) -----------------------------------------------------

class _CRG(LiteXModule):
    """Clock Reset Generator for ZCU104.
    
    Uses the 125MHz on-board oscillator and generates:
    - sys     : System clock (default 100MHz, safe for initial bring-up)
    - sys4x   : 4x system clock (for DDR4 PHY, when enabled)
    - pll4x   : PLL 4x output
    - idelay  : 500MHz reference for IDELAY (DDR4 PHY)
    """
    def __init__(self, platform, sys_clk_freq):
        self.rst       = Signal()
        self.cd_sys    = ClockDomain()
        self.cd_sys4x  = ClockDomain()
        self.cd_pll4x  = ClockDomain()
        self.cd_idelay = ClockDomain()

        # # #

        self.pll = pll = USMMCM(speedgrade=-2)
        self.comb += pll.reset.eq(self.rst)
        pll.register_clkin(platform.request("clk125"), 125e6)
        pll.create_clkout(self.cd_pll4x, sys_clk_freq * 4, buf=None, with_reset=False)
        pll.create_clkout(self.cd_idelay, 500e6)
        platform.add_false_path_constraints(self.cd_sys.clk, pll.clkin)

        self.specials += [
            Instance("BUFGCE_DIV",
                p_BUFGCE_DIVIDE = 4,
                i_CE = 1,
                i_I  = self.cd_pll4x.clk,
                o_O  = self.cd_sys.clk),
            Instance("BUFGCE",
                i_CE = 1,
                i_I  = self.cd_pll4x.clk,
                o_O  = self.cd_sys4x.clk),
        ]

        self.idelayctrl = USIDELAYCTRL(cd_ref=self.cd_idelay, cd_sys=self.cd_sys)


# BaseSoC --------------------------------------------------------------------------

class BaseSoC(SoCCore):
    """LiteX SoC on ZCU104 with CVA6 RISC-V CPU.
    
    Default configuration:
    - CVA6 CPU (rv64imac, 64-bit RISC-V)
    - 64KB integrated SRAM (safe for initial bring-up)
    - UART over USB for serial console
    - LED chaser for visual status
    - Optional DDR4 SDRAM (enable with --no-integrated-main-ram)
    - Optional NVDLA_small accelerator (enable with --with-nvdla)
    """
    def __init__(self, sys_clk_freq=50e6, with_led_chaser=True,
                 with_nvdla=False, **kwargs):
        platform = xilinx_zcu104.Platform()

        # CRG --------------------------------------------------------------------------
        self.crg = _CRG(platform, sys_clk_freq)

        # SoCCore ----------------------------------------------------------------------
        # For NVDLA, use 1MB of integrated SRAM to avoid complex DDR4 timing issues on FPGA
        if with_nvdla:
            kwargs["integrated_main_ram_size"] = 0x100000  # 1MB
        elif "integrated_main_ram_size" not in kwargs:
            kwargs["integrated_main_ram_size"] = 0x10000  # 64KB

        SoCCore.__init__(self, platform, sys_clk_freq,
            ident = "LiteX CVA6 + NVDLA SoC on ZCU104" if with_nvdla
                    else "LiteX CVA6 SoC on ZCU104",
            **kwargs
        )

        # DDR4 SDRAM (only when not using integrated RAM) ------------------------------
        if not self.integrated_main_ram_size:
            from litedram.modules import MTA4ATF51264HZ
            from litedram.phy import usddrphy

            self.ddrphy = usddrphy.USPDDRPHY(
                platform.request("ddram"),
                memtype          = "DDR4",
                sys_clk_freq     = sys_clk_freq,
                iodelay_clk_freq = 500e6,
            )
            self.add_sdram("sdram",
                phy           = self.ddrphy,
                module        = MTA4ATF51264HZ(sys_clk_freq, "1:4"),
                size          = 0x40000000,  # 1GB
                l2_cache_size = kwargs.get("l2_size", 8192),
            )

        # NVDLA_small ------------------------------------------------------------------
        if with_nvdla:
            from nvdla_wrapper import NVDLASmall

            script_dir   = os.path.dirname(os.path.abspath(__file__))
            nvdla_rtl    = os.path.join(script_dir, "..", "nvdla", "hw",
                                        "outdir", "nv_small", "vmod")
            nvdla_rtl    = os.path.abspath(nvdla_rtl)

            self.nvdla = NVDLASmall(platform, nvdla_rtl)

            # Connect NVDLA config registers as Wishbone slave at 0x80010000
            # (must be in CVA6 IO region: 0x80000000+)
            self.bus.add_slave("nvdla", self.nvdla.bus,
                region=SoCRegion(origin=0x80010000, size=0x10000,
                                 cached=False))

            # Connect NVDLA DMA AXI master to the main bus
            # NVDLA AXI is 64-bit, LiteX Wishbone is 32-bit → convert first
            nvdla_axi32 = axi.AXIInterface(data_width=32, address_width=32,
                                           id_width=8)
            self.submodules += axi.AXIConverter(self.nvdla.dma, nvdla_axi32)

            nvdla_wb = wishbone.Interface(data_width=32, adr_width=30)
            self.submodules += axi.AXI2Wishbone(nvdla_axi32, nvdla_wb)
            self.bus.add_master(name="nvdla_dma", master=nvdla_wb)

            # Connect interrupt — direct connection to CPU interrupt signal
            # NVDLA IRQ connected to interrupt bit 2 (after uart=0, timer0=1)
            self.comb += self.cpu.interrupt[2].eq(self.nvdla.irq)

        # LEDs -------------------------------------------------------------------------
        if with_led_chaser:
            self.leds = LedChaser(
                pads         = platform.request_all("user_led"),
                sys_clk_freq = sys_clk_freq,
            )


# Build ----------------------------------------------------------------------------

def main():
    from litex.build.parser import LiteXArgumentParser

    parser = LiteXArgumentParser(
        platform    = xilinx_zcu104.Platform,
        description = "LiteX CVA6 RISC-V 64-bit SoC on ZCU104."
    )
    parser.add_target_argument("--sys-clk-freq",
        default = 50e6,
        type    = float,
        help    = "System clock frequency (default: 50MHz)."
    )
    parser.add_target_argument("--firmware-init",
        default = None,
        type    = str,
        help    = "Path to firmware .bin to bake into ROM (replaces BIOS)."
    )
    parser.add_target_argument("--with-nvdla",
        action  = "store_true",
        help    = "Enable NVDLA_small deep learning accelerator."
    )

    # Set CVA6 as default CPU (parser defaults to vexriscv otherwise)
    parser.set_defaults(cpu_type="cva6", cpu_variant="standard")

    args = parser.parse_args()

    soc_kwargs = parser.soc_argdict

    # If custom firmware specified, load it into ROM
    if args.firmware_init is not None:
        soc_kwargs["integrated_rom_init"] = args.firmware_init

    soc = BaseSoC(
        sys_clk_freq = args.sys_clk_freq,
        with_nvdla   = args.with_nvdla,
        **soc_kwargs
    )

    # Set build directory within project
    script_dir     = os.path.dirname(os.path.abspath(__file__))
    build_dir      = os.path.join(script_dir, "build", "zcu104_cva6")
    builder_kwargs = parser.builder_argdict
    if "output_dir" not in builder_kwargs or builder_kwargs["output_dir"] is None:
        builder_kwargs["output_dir"] = build_dir

    # Export CSR map for firmware development
    builder_kwargs["csr_json"] = os.path.join(build_dir, "csr.json")

    builder = Builder(soc, **builder_kwargs)

    if args.build:
        builder.build(**parser.toolchain_argdict)

    if args.load:
        prog = soc.platform.create_programmer()
        prog.load_bitstream(builder.get_bitstream_filename(mode="sram"))


if __name__ == "__main__":
    main()
