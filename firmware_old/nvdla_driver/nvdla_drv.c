/* nvdla_drv.c — Bare-metal NVDLA nv_small register-level driver
 *
 * Drives conv + pooling layers directly via MMIO register writes.
 * INT8 precision, NCHW feature data format, direct (non-batch) mode.
 */

#include "nvdla_drv.h"
#include "nvdla_regs.h"
#include "../bsp/uart.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static inline void nvdla_wr(uint32_t off, uint32_t val) {
    NVDLA_WRITE(off, val);
}
static inline uint32_t nvdla_rd(uint32_t off) {
    return NVDLA_READ(off);
}

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */

void nvdla_init(void) {
    /* Clear all pending interrupts */
    nvdla_wr(NVDLA_GLB_S_INTR_SET_0, 0xFFFFFFFF);
    /* Mask all interrupts (we poll) */
    nvdla_wr(NVDLA_GLB_S_INTR_MASK_0, 0xFFFFFFFF);

    uart_puts("[nvdla] init done, HW version=");
    uart_put_hex32(nvdla_rd(NVDLA_GLB_S_NVDLA_HW_VERSION_0));
    uart_putc('\n');
}

/* ------------------------------------------------------------------ */
/*  Wait for completion (polling GLB interrupt status)                  */
/* ------------------------------------------------------------------ */

int nvdla_wait_done(uint32_t intr_mask) {
    uint32_t timeout = NVDLA_POLL_TIMEOUT;
    while (timeout--) {
        uint32_t status = nvdla_rd(NVDLA_GLB_S_INTR_STATUS_0);
        if (status & intr_mask) {
            /* Acknowledge */
            nvdla_wr(NVDLA_GLB_S_INTR_SET_0, status & intr_mask);
            return NVDLA_OK;
        }
    }
    uart_puts("[nvdla] TIMEOUT waiting for interrupt mask=");
    uart_put_hex32(intr_mask);
    uart_putc('\n');
    return NVDLA_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/*  Convolution                                                         */
/* ------------------------------------------------------------------ */

int nvdla_run_conv(const nvdla_conv_cfg_t *cfg) {
    int err;

    /* --- CDMA: load input data --- */
    /* Select group 0 (single buffering) */
    nvdla_wr(NVDLA_CDMA_S_POINTER_0, 0);

    /* MISC_CFG: datain_format=feature (0), conv_mode=direct (0), proc_precision=INT8 (1),
     *           in_precision=INT8 (1), batch_number=1 */
    nvdla_wr(NVDLA_CDMA_D_MISC_CFG_0,
        (0 << 0)  |   /* conv_mode: DIRECT */
        (1 << 12) |   /* in_precision: INT8 */
        (1 << 16) |   /* proc_precision: INT8 */
        (0 << 20));   /* datain_format: FEATURE */

    nvdla_wr(NVDLA_CDMA_D_DATAIN_FORMAT_0, 0); /* channel_extension=1 */

    /* Input size: width-1, height-1, channels-1 (hardware uses 0-based) */
    nvdla_wr(NVDLA_CDMA_D_DATAIN_SIZE_0_0,
        ((cfg->in_width  - 1) << 0) |
        ((cfg->in_height - 1) << 16));
    nvdla_wr(NVDLA_CDMA_D_DATAIN_SIZE_1_0, cfg->in_channels - 1);

    /* Input memory: SRAM (0=CV-SRAM, 1=MC), addr */
    nvdla_wr(NVDLA_CDMA_D_DAIN_RAM_TYPE_0, 1); /* MC (DRAM/BRAM) */
    nvdla_wr(NVDLA_CDMA_D_DAIN_ADDR_HIGH_0_0, 0);
    nvdla_wr(NVDLA_CDMA_D_DAIN_ADDR_LOW_0_0, cfg->src_addr);

    /* Strides: line stride = channels * width (INT8 = 1 byte/sample) */
    uint32_t line_stride = (uint32_t)cfg->in_channels * cfg->in_width;
    uint32_t surf_stride = line_stride * cfg->in_height;
    nvdla_wr(NVDLA_CDMA_D_LINE_STRIDE_0, line_stride);
    nvdla_wr(NVDLA_CDMA_D_SURF_STRIDE_0, surf_stride);

    /* Kernel config */
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_FORMAT_0, 0); /* uncompressed */
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_SIZE_0_0,
        ((cfg->kernel_w - 1) << 0) |
        ((cfg->kernel_h - 1) << 8) |
        ((cfg->in_channels - 1) << 16));
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_SIZE_1_0, cfg->out_channels - 1);
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_RAM_TYPE_0, 1);
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_ADDR_HIGH_0, 0);
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_ADDR_LOW_0, cfg->wt_addr);

    /* Weight bytes: Oc * Ic * kH * kW * 1 byte (INT8) */
    uint32_t wt_bytes = (uint32_t)cfg->out_channels * cfg->in_channels *
                        cfg->kernel_h * cfg->kernel_w;
    nvdla_wr(NVDLA_CDMA_D_WEIGHT_BYTES_0, wt_bytes);

    nvdla_wr(NVDLA_CDMA_D_ZERO_PADDING_0,
        (cfg->pad << 0) | (cfg->pad << 8) | (cfg->pad << 16) | (cfg->pad << 24));
    nvdla_wr(NVDLA_CDMA_D_ZERO_PADDING_VALUE_0, 0);

    /* No mean subtract */
    nvdla_wr(NVDLA_CDMA_D_MEAN_FORMAT_0, 0);

    /* No CVT (pass-through) */
    nvdla_wr(NVDLA_CDMA_D_CVT_CFG_0, 0);
    nvdla_wr(NVDLA_CDMA_D_CVT_OFFSET_0, 0);
    nvdla_wr(NVDLA_CDMA_D_CVT_SCALE_0, 0x4000);  /* 1.0 in 2.14 fixed point */

    /* Stride */
    nvdla_wr(NVDLA_CDMA_D_CONV_STRIDE_EXT_0, ((cfg->stride - 1) << 0) | ((cfg->stride - 1) << 16));
    nvdla_wr(NVDLA_CDMA_D_DILATION_EXT_0, 0); /* dilation=1 */

    /* CBUF bank: weight bank=0..15, data bank=16..31 */
    nvdla_wr(NVDLA_CDMA_D_BANK_0, (0 << 0) | (8 << 8));

    /* Enable CDMA */
    nvdla_wr(NVDLA_CDMA_D_OP_ENABLE_0, 1);

    /* --- CSC --- */
    nvdla_wr(NVDLA_CSC_S_POINTER_0, 0);
    nvdla_wr(NVDLA_CSC_D_MISC_CFG_0,
        (0 << 0)  |   /* conv_mode: DIRECT */
        (1 << 12) |   /* in_precision: INT8 */
        (1 << 16));   /* proc_precision: INT8 */
    nvdla_wr(NVDLA_CSC_D_DATAIN_FORMAT_0, 0); /* FEATURE */
    nvdla_wr(NVDLA_CSC_D_DATAIN_SIZE_EXT_0_0,
        ((cfg->in_width  - 1) << 0) |
        ((cfg->in_height - 1) << 16));
    nvdla_wr(NVDLA_CSC_D_DATAIN_SIZE_EXT_1_0, cfg->in_channels - 1);
    nvdla_wr(NVDLA_CSC_D_DATAIN_SIZE_EXT_2_0, 0); /* batch=1 */
    nvdla_wr(NVDLA_CSC_D_WEIGHT_FORMAT_0, 0);
    nvdla_wr(NVDLA_CSC_D_WEIGHT_SIZE_EXT_0_0,
        ((cfg->kernel_w - 1) << 0) |
        ((cfg->kernel_h - 1) << 8) |
        ((cfg->in_channels - 1) << 16));
    nvdla_wr(NVDLA_CSC_D_WEIGHT_SIZE_EXT_1_0, cfg->out_channels - 1);
    nvdla_wr(NVDLA_CSC_D_DATAOUT_SIZE_0_0,
        ((cfg->out_width  - 1) << 0) |
        ((cfg->out_height - 1) << 16));
    nvdla_wr(NVDLA_CSC_D_DATAOUT_SIZE_1_0, cfg->out_channels - 1);
    nvdla_wr(NVDLA_CSC_D_CONV_STRIDE_EXT_0, ((cfg->stride - 1) << 0) | ((cfg->stride - 1) << 16));
    nvdla_wr(NVDLA_CSC_D_DILATION_EXT_0, 0);
    nvdla_wr(NVDLA_CSC_D_ZERO_PADDING_0,
        (cfg->pad << 0) | (cfg->pad << 8) | (cfg->pad << 16) | (cfg->pad << 24));
    nvdla_wr(NVDLA_CSC_D_BANK_0, (0 << 0) | (8 << 8));
    /* Atomics = ceiling(Ic/AtomicC) * ceiling(Oc/AtomicK) * kH * kW * out_w * out_h */
    uint32_t atomics = ((cfg->in_channels  + NVDLA_ATOMIC_C - 1) / NVDLA_ATOMIC_C) *
                       ((cfg->out_channels + NVDLA_ATOMIC_K - 1) / NVDLA_ATOMIC_K) *
                       cfg->kernel_h * cfg->kernel_w *
                       cfg->out_width * cfg->out_height;
    nvdla_wr(NVDLA_CSC_D_ATOMICS_0, atomics - 1);
    nvdla_wr(NVDLA_CSC_D_OP_ENABLE_0, 1);

    /* --- CMAC_A + CMAC_B: same config --- */
    nvdla_wr(NVDLA_CMAC_A_S_POINTER_0, 0);
    nvdla_wr(NVDLA_CMAC_A_D_MISC_CFG_0, (1 << 0)); /* proc_precision=INT8 */
    nvdla_wr(NVDLA_CMAC_A_D_OP_ENABLE_0, 1);

    nvdla_wr(NVDLA_CMAC_B_S_POINTER_0, 0);
    nvdla_wr(NVDLA_CMAC_B_D_MISC_CFG_0, (1 << 0));
    nvdla_wr(NVDLA_CMAC_B_D_OP_ENABLE_0, 1);

    /* --- CACC: accumulate results, write to dst --- */
    nvdla_wr(NVDLA_CACC_S_POINTER_0, 0);
    nvdla_wr(NVDLA_CACC_D_MISC_CFG_0, (1 << 0)); /* proc_precision=INT8 */
    nvdla_wr(NVDLA_CACC_D_DATAOUT_SIZE_0_0,
        ((cfg->out_width  - 1) << 0) |
        ((cfg->out_height - 1) << 16));
    nvdla_wr(NVDLA_CACC_D_DATAOUT_SIZE_1_0, cfg->out_channels - 1);
    nvdla_wr(NVDLA_CACC_D_DATAOUT_ADDR_0,  cfg->dst_addr >> 5); /* 32-byte aligned */
    nvdla_wr(NVDLA_CACC_D_LINE_STRIDE_0,
        (uint32_t)cfg->out_width * cfg->out_channels * 4); /* INT32 accum */
    nvdla_wr(NVDLA_CACC_D_SURF_STRIDE_0,
        (uint32_t)cfg->out_width * cfg->out_height * cfg->out_channels * 4);
    nvdla_wr(NVDLA_CACC_D_DATAOUT_MAP_0, 0); /* packed */
    nvdla_wr(NVDLA_CACC_D_CLIP_CFG_0, 0);    /* no clip */
    nvdla_wr(NVDLA_CACC_D_OP_ENABLE_0, 1);

    /* --- SDP: bias add + relu (bypass EW, bypass LUT) --- */
    nvdla_wr(NVDLA_SDP_S_POINTER_0, 0);
    nvdla_wr(NVDLA_SDP_D_DATA_CUBE_WIDTH_0,   cfg->out_width  - 1);
    nvdla_wr(NVDLA_SDP_D_DATA_CUBE_HEIGHT_0,  cfg->out_height - 1);
    nvdla_wr(NVDLA_SDP_D_DATA_CUBE_CHANNEL_0, cfg->out_channels - 1);
    nvdla_wr(NVDLA_SDP_D_DST_BASE_ADDR_LOW_0,  cfg->dst_addr);
    nvdla_wr(NVDLA_SDP_D_DST_BASE_ADDR_HIGH_0, 0);

    uint32_t dst_line_stride = (uint32_t)cfg->out_width * cfg->out_channels;
    nvdla_wr(NVDLA_SDP_D_DST_LINE_STRIDE_0,    dst_line_stride);
    nvdla_wr(NVDLA_SDP_D_DST_SURFACE_STRIDE_0, dst_line_stride * cfg->out_height);

    if (cfg->bias_addr) {
        /* BS sub-engine: ALU add from DRAM bias, bypass MUL */
        nvdla_wr(NVDLA_SDP_D_DP_BS_CFG_0,
            (1 << 0) |   /* bs_bypass=0: enable */
            (0 << 1) |   /* alu_bypass=0: enable ALU add */
            (1 << 2));   /* mul_bypass=1: bypass MUL */
        nvdla_wr(NVDLA_SDP_D_DP_BS_ALU_CFG_0, (0 << 0) | (1 << 8)); /* src=DRAM, op=ADD */
        nvdla_wr(NVDLA_SDP_D_DP_BS_MUL_CFG_0, (1 << 0)); /* mul_bypass */
    } else {
        nvdla_wr(NVDLA_SDP_D_DP_BS_CFG_0, (1 << 0)); /* bs_bypass=1 */
    }
    /* Bypass BN and EW sub-engines */
    nvdla_wr(NVDLA_SDP_D_DP_BN_CFG_0, (1 << 0)); /* bn_bypass */
    nvdla_wr(NVDLA_SDP_D_DP_EW_CFG_0, (1 << 0)); /* ew_bypass */

    /* Feature mode: output INT8, activation=ReLU */
    nvdla_wr(NVDLA_SDP_D_FEATURE_MODE_CFG_0,
        (1 << 0) |   /* output_dst=DRAM */
        (1 << 4));   /* activation=ReLU */

    nvdla_wr(NVDLA_SDP_D_DATA_FORMAT_0, 0); /* FEATURE */
    nvdla_wr(NVDLA_SDP_D_CVT_OFFSET_0, 0);
    nvdla_wr(NVDLA_SDP_D_CVT_SCALE_0,  cfg->input_scale);
    nvdla_wr(NVDLA_SDP_D_CVT_SHIFT_0,  7); /* >>7 to normalise INT8 */

    nvdla_wr(NVDLA_SDP_D_DST_DMA_CFG_0, 1); /* dst=DRAM */

    /* Enable SDP */
    nvdla_wr(NVDLA_SDP_D_OP_ENABLE_0, 1);

    /* --- Wait for CACC + SDP done --- */
    err = nvdla_wait_done(NVDLA_GLB_INTR_CACC_DONE | NVDLA_GLB_INTR_SDP_DONE);

    uart_puts("[nvdla] conv done, err=");
    uart_put_dec(err);
    uart_putc('\n');

    return err;
}

/* ------------------------------------------------------------------ */
/*  Max Pooling                                                         */
/* ------------------------------------------------------------------ */

int nvdla_run_pool(const nvdla_pool_cfg_t *cfg) {
    int err;

    nvdla_wr(NVDLA_PDP_S_POINTER_0, 0);

    /* Input cube */
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_IN_WIDTH_0,   cfg->in_width  - 1);
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_IN_HEIGHT_0,  cfg->in_height - 1);
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_IN_CHANNEL_0, cfg->channels  - 1);

    /* Output cube */
    uint16_t out_w = (cfg->in_width  + 2*cfg->pad - cfg->pool_w) / cfg->stride_w + 1;
    uint16_t out_h = (cfg->in_height + 2*cfg->pad - cfg->pool_h) / cfg->stride_h + 1;
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_OUT_WIDTH_0,   out_w - 1);
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_OUT_HEIGHT_0,  out_h - 1);
    nvdla_wr(NVDLA_PDP_D_DATA_CUBE_OUT_CHANNEL_0, cfg->channels - 1);

    /* Operation: MAX pooling, INT8, read from DRAM */
    nvdla_wr(NVDLA_PDP_D_OPERATION_MODE_CFG_0,
        (0 << 0) |   /* pooling_method: MAX */
        (0 << 4) |   /* flying_mode: read from DRAM (SDP writes output first) */
        (1 << 8));   /* split_num: 1 */

    /* Kernel */
    nvdla_wr(NVDLA_PDP_D_POOLING_KERNEL_CFG_0,
        ((cfg->pool_w - 1) << 0) | ((cfg->pool_h - 1) << 4) |
        ((cfg->stride_w - 1) << 8) | ((cfg->stride_h - 1) << 12));

    nvdla_wr(NVDLA_PDP_D_POOLING_PADDING_CFG_0,
        (cfg->pad << 0) | (cfg->pad << 4) | (cfg->pad << 8) | (cfg->pad << 12));

    /* Source: on-the-fly from SDP (no DRAM read) */
    nvdla_wr(NVDLA_PDP_D_SRC_BASE_ADDR_LOW_0,  cfg->src_addr);
    nvdla_wr(NVDLA_PDP_D_SRC_BASE_ADDR_HIGH_0, 0);
    nvdla_wr(NVDLA_PDP_D_SRC_LINE_STRIDE_0,
        (uint32_t)cfg->in_width * cfg->channels);
    nvdla_wr(NVDLA_PDP_D_SRC_SURFACE_STRIDE_0,
        (uint32_t)cfg->in_width * cfg->in_height * cfg->channels);

    /* Destination */
    nvdla_wr(NVDLA_PDP_D_DST_BASE_ADDR_LOW_0,  cfg->dst_addr);
    nvdla_wr(NVDLA_PDP_D_DST_BASE_ADDR_HIGH_0, 0);
    nvdla_wr(NVDLA_PDP_D_DST_LINE_STRIDE_0,
        (uint32_t)out_w * cfg->channels);
    nvdla_wr(NVDLA_PDP_D_DST_SURFACE_STRIDE_0,
        (uint32_t)out_w * out_h * cfg->channels);
    nvdla_wr(NVDLA_PDP_D_DST_RAM_CFG_0, 1); /* DRAM */

    nvdla_wr(NVDLA_PDP_D_DATA_FORMAT_0, 0); /* INT8 */

    nvdla_wr(NVDLA_PDP_D_OP_ENABLE_0, 1);

    err = nvdla_wait_done(NVDLA_GLB_INTR_PDP_DONE);

    uart_puts("[nvdla] pool done, err=");
    uart_put_dec(err);
    uart_putc('\n');

    return err;
}

/* ------------------------------------------------------------------ */
/*  Debug                                                               */
/* ------------------------------------------------------------------ */

void nvdla_dump_status(void) {
    uart_puts("[nvdla] GLB_INTR_STATUS=");
    uart_put_hex32(nvdla_rd(NVDLA_GLB_S_INTR_STATUS_0));
    uart_puts(" CDMA_STATUS=");
    uart_put_hex32(nvdla_rd(NVDLA_CDMA_S_STATUS_0));
    uart_puts(" SDP_STATUS=");
    uart_put_hex32(nvdla_rd(NVDLA_SDP_S_STATUS_0));
    uart_putc('\n');
}
