/* nvdla_drv.h — Bare-metal NVDLA nv_small driver API */
#ifndef NVDLA_DRV_H
#define NVDLA_DRV_H

#include <stdint.h>

/* Return codes */
#define NVDLA_OK        0
#define NVDLA_ERR_TIMEOUT   -1
#define NVDLA_ERR_PARAM     -2

/* nv_small HW constants */
#define NVDLA_ATOMIC_C  8   /* MAC atomic-C size (input channels per group) */
#define NVDLA_ATOMIC_K  8   /* MAC atomic-K size (output channels per group) */

/* BRAM memory map for inference buffers (firmware owns the upper 768KB of 1MB BRAM) */
#define NVDLA_WEIGHT_BASE   0x40040000UL   /* 256KB for weights  */
#define NVDLA_BUF0_BASE     0x40080000UL   /* 256KB activation buffer 0 */
#define NVDLA_BUF1_BASE     0x400C0000UL   /* 256KB activation buffer 1 */

/* Interrupt poll timeout (cycles, approx 10s at 100MHz) */
#define NVDLA_POLL_TIMEOUT  1000000000U

/* ---- Initialisation ---- */
void nvdla_init(void);

/* ---- Convolution layer ---- */
typedef struct {
    /* Input feature map */
    uint32_t src_addr;      /* physical BRAM address */
    uint16_t in_width;
    uint16_t in_height;
    uint16_t in_channels;   /* must be multiple of NVDLA_ATOMIC_C */
    /* Weights */
    uint32_t wt_addr;
    uint8_t  kernel_w;
    uint8_t  kernel_h;
    uint16_t out_channels;  /* must be multiple of NVDLA_ATOMIC_K */
    uint8_t  pad;
    uint8_t  stride;
    /* Output accumulator -> SDP bypass output */
    uint32_t dst_addr;
    uint16_t out_width;
    uint16_t out_height;
    /* Scale factors for INT8 (bias add via SDP) */
    uint32_t bias_addr;     /* 0 = no bias */
    uint8_t  input_scale;   /* INT8 scale factor (multiplied by 1/127) */
} nvdla_conv_cfg_t;

int nvdla_run_conv(const nvdla_conv_cfg_t *cfg);

/* ---- Pooling layer ---- */
typedef struct {
    uint32_t src_addr;
    uint16_t in_width;
    uint16_t in_height;
    uint16_t channels;
    uint32_t dst_addr;
    uint8_t  pool_w;
    uint8_t  pool_h;
    uint8_t  stride_w;
    uint8_t  stride_h;
    uint8_t  pad;
} nvdla_pool_cfg_t;

int nvdla_run_pool(const nvdla_pool_cfg_t *cfg);

/* ---- Wait for done interrupt (poll) ---- */
int nvdla_wait_done(uint32_t intr_mask);

/* ---- Utility ---- */
void nvdla_dump_status(void);

#endif /* NVDLA_DRV_H */
