#!/usr/bin/env python3
"""
LiteX wrapper for NVDLA nv_small.

Integrates the NVIDIA Deep Learning Accelerator into a LiteX SoC:
- Wishbone slave → APB → CSB bridge for register configuration
- NVDLA AXI4 DMA master → LiteX bus for memory access
- Interrupt line routed to CPU
"""

import os
import glob

from migen import *
from litex.gen import *
from litex.soc.interconnect import axi
from litex.soc.interconnect import wishbone


class NVDLASmall(LiteXModule):
    """NVDLA nv_small LiteX integration wrapper.

    Parameters
    ----------
    platform : LiteXPlatform
        FPGA platform (used to add Verilog sources).
    nvdla_rtl_dir : str
        Path to the nv_small RTL output directory
        (e.g., nvdla/hw/outdir/nv_small/vmod).
    """

    def __init__(self, platform, nvdla_rtl_dir):
        # =====================================================================
        # Interfaces exposed to LiteX SoC
        # =====================================================================

        # Wishbone slave for CSR access (CPU → NVDLA config registers)
        # NVDLA CSB has 16-bit word address × 4 = 64KB register space
        self.bus = wishbone.Interface(data_width=32, adr_width=30)

        # AXI master for DMA (NVDLA → memory)
        # NVDLA nv_small: 32-bit address, 64-bit data, 8-bit ID, 4-bit len
        self.dma = axi.AXIInterface(data_width=64, address_width=32, id_width=8)

        # Interrupt output
        self.irq = Signal()

        # =====================================================================
        # Internal APB signals (Wishbone → APB → CSB)
        # =====================================================================
        apb_psel    = Signal()
        apb_penable = Signal()
        apb_pwrite  = Signal()
        apb_paddr   = Signal(32)
        apb_pwdata  = Signal(32)
        apb_prdata  = Signal(32)
        apb_pready  = Signal()

        # =====================================================================
        # Wishbone → APB bridge
        # =====================================================================
        # APB state machine (simple 2-phase protocol)
        wb = self.bus
        apb_phase = Signal()  # 0=setup, 1=access

        self.sync += [
            If(wb.cyc & wb.stb & ~wb.ack,
                If(~apb_phase,
                    # Setup phase
                    apb_psel.eq(1),
                    apb_penable.eq(0),
                    apb_pwrite.eq(wb.we),
                    apb_paddr.eq(Cat(Signal(2, reset=0), wb.adr[:16])),  # Word → byte addr
                    apb_pwdata.eq(wb.dat_w),
                    apb_phase.eq(1),
                ).Else(
                    # Access phase
                    apb_penable.eq(1),
                    If(apb_pready,
                        wb.ack.eq(1),
                        wb.dat_r.eq(apb_prdata),
                        apb_psel.eq(0),
                        apb_penable.eq(0),
                        apb_phase.eq(0),
                    )
                )
            ).Else(
                wb.ack.eq(0),
            )
        ]

        # =====================================================================
        # CSB signals (output of APB2CSB bridge)
        # =====================================================================
        csb2nvdla_valid      = Signal()
        csb2nvdla_ready      = Signal()
        csb2nvdla_addr       = Signal(16)
        csb2nvdla_wdat       = Signal(32)
        csb2nvdla_write      = Signal()
        csb2nvdla_nposted    = Signal()
        nvdla2csb_valid      = Signal()
        nvdla2csb_data       = Signal(32)
        nvdla2csb_wr_complete = Signal()

        # =====================================================================
        # APB2CSB bridge instance (provided by NVDLA RTL)
        # =====================================================================
        self.specials += Instance("NV_NVDLA_apb2csb",
            # Clock / Reset
            i_pclk   = ClockSignal("sys"),
            i_prstn  = ~ResetSignal("sys"),
            # APB side
            i_psel    = apb_psel,
            i_penable = apb_penable,
            i_pwrite  = apb_pwrite,
            i_paddr   = apb_paddr,
            i_pwdata  = apb_pwdata,
            o_prdata  = apb_prdata,
            o_pready  = apb_pready,
            # CSB side
            o_csb2nvdla_valid   = csb2nvdla_valid,
            i_csb2nvdla_ready   = csb2nvdla_ready,
            o_csb2nvdla_addr    = csb2nvdla_addr,
            o_csb2nvdla_wdat    = csb2nvdla_wdat,
            o_csb2nvdla_write   = csb2nvdla_write,
            o_csb2nvdla_nposted = csb2nvdla_nposted,
            i_nvdla2csb_valid   = nvdla2csb_valid,
            i_nvdla2csb_data    = nvdla2csb_data,
        )

        # =====================================================================
        # NVDLA core instance
        # =====================================================================
        dma = self.dma

        self.specials += Instance("NV_nvdla",
            # Clock / Reset
            i_dla_core_clk                  = ClockSignal("sys"),
            i_dla_csb_clk                   = ClockSignal("sys"),
            i_global_clk_ovr_on             = 0,
            i_tmc2slcg_disable_clock_gating = 0,
            i_dla_reset_rstn                = ~ResetSignal("sys"),
            i_direct_reset_                 = ~ResetSignal("sys"),
            i_test_mode                     = 0,

            # CSB interface (from APB2CSB bridge)
            i_csb2nvdla_valid        = csb2nvdla_valid,
            o_csb2nvdla_ready        = csb2nvdla_ready,
            i_csb2nvdla_addr         = csb2nvdla_addr,
            i_csb2nvdla_wdat         = csb2nvdla_wdat,
            i_csb2nvdla_write        = csb2nvdla_write,
            i_csb2nvdla_nposted      = csb2nvdla_nposted,
            o_nvdla2csb_valid        = nvdla2csb_valid,
            o_nvdla2csb_data         = nvdla2csb_data,
            o_nvdla2csb_wr_complete  = nvdla2csb_wr_complete,

            # DBBIF AXI4 Master — Write Address
            o_nvdla_core2dbb_aw_awvalid = dma.aw.valid,
            i_nvdla_core2dbb_aw_awready = dma.aw.ready,
            o_nvdla_core2dbb_aw_awid    = dma.aw.id,
            o_nvdla_core2dbb_aw_awlen   = dma.aw.len,
            o_nvdla_core2dbb_aw_awaddr  = dma.aw.addr,

            # DBBIF AXI4 Master — Write Data
            o_nvdla_core2dbb_w_wvalid = dma.w.valid,
            i_nvdla_core2dbb_w_wready = dma.w.ready,
            o_nvdla_core2dbb_w_wdata  = dma.w.data,
            o_nvdla_core2dbb_w_wstrb  = dma.w.strb,
            o_nvdla_core2dbb_w_wlast  = dma.w.last,

            # DBBIF AXI4 Master — Write Response
            i_nvdla_core2dbb_b_bvalid = dma.b.valid,
            o_nvdla_core2dbb_b_bready = dma.b.ready,
            i_nvdla_core2dbb_b_bid    = dma.b.id,

            # DBBIF AXI4 Master — Read Address
            o_nvdla_core2dbb_ar_arvalid = dma.ar.valid,
            i_nvdla_core2dbb_ar_arready = dma.ar.ready,
            o_nvdla_core2dbb_ar_arid    = dma.ar.id,
            o_nvdla_core2dbb_ar_arlen   = dma.ar.len,
            o_nvdla_core2dbb_ar_araddr  = dma.ar.addr,

            # DBBIF AXI4 Master — Read Data
            i_nvdla_core2dbb_r_rvalid = dma.r.valid,
            o_nvdla_core2dbb_r_rready = dma.r.ready,
            i_nvdla_core2dbb_r_rid    = dma.r.id,
            i_nvdla_core2dbb_r_rlast  = dma.r.last,
            i_nvdla_core2dbb_r_rdata  = dma.r.data,

            # Interrupt
            o_dla_intr = self.irq,

            # Power control (tie to 0 — no power gating)
            i_nvdla_pwrbus_ram_c_pd  = 0,
            i_nvdla_pwrbus_ram_ma_pd = 0,
            i_nvdla_pwrbus_ram_mb_pd = 0,
            i_nvdla_pwrbus_ram_p_pd  = 0,
            i_nvdla_pwrbus_ram_o_pd  = 0,
            i_nvdla_pwrbus_ram_a_pd  = 0,
        )

        # =====================================================================
        # Add NVDLA Verilog sources to FPGA platform
        # =====================================================================
        self._add_rtl_sources(platform, nvdla_rtl_dir)

    @staticmethod
    def _add_rtl_sources(platform, rtl_dir):
        """Add all NVDLA Verilog sources and include paths."""
        # Include directories
        include_dir = os.path.join(rtl_dir, "include")
        if os.path.isdir(include_dir):
            platform.add_verilog_include_path(include_dir)

        # Add all .v files recursively
        for subdir in ["vlibs", "rams", "nvdla"]:
            src_dir = os.path.join(rtl_dir, subdir)
            if os.path.isdir(src_dir):
                for vfile in sorted(glob.glob(os.path.join(src_dir, "**", "*.v"), recursive=True)):
                    # Skip pre-processed files
                    if vfile.endswith(".vcp"):
                        continue
                    # For RAMs: use FPGA versions only, skip ASIC synth models
                    if "/rams/synth/" in vfile:
                        continue
                    platform.add_source(vfile)
