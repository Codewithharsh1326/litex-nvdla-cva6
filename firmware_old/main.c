/* main.c — NVDLA MNIST inference, bare-metal CVA6 RV64IMAC on ZCU104
 *
 * Architecture:  LeNet-5 variant
 *   conv1 (1x28x28 → 8x24x24, 5x5) + relu + pool1 (→ 8x12x12): SOFTWARE
 *   conv2 (8x12x12 → 16x8x8,  5x5) + relu + pool2 (→ 16x4x4)  : NVDLA HW
 *   fc    (256 → 10)                                             : SOFTWARE
 *
 * conv1 is done in SW because NVDLA nv_small requires in_channels to be a
 * multiple of ATOMIC_C=8.  A 1-channel input cannot be handled directly.
 *
 * Memory layout (1MB main_ram @ 0x40000000):
 *   0x40000000 - 0x4003FFFF  : firmware stack                      (256KB)
 *   0x40040000 - 0x4007FFFF  : weights                             (256KB)
 *   0x40080000 - 0x400BFFFF  : activation buffer 0 (sw conv1 out) (256KB)
 *   0x400C0000 - 0x400FFFFF  : activation buffer 1                 (256KB)
 */

#include <stdint.h>
#include <string.h>
#include "bsp/uart.h"
#include "nvdla_driver/nvdla_drv.h"
#include "nvdla_driver/nvdla_regs.h"
#include "test_image.h"

/* ---- Embedded weights (INT8 quantized) ---- */
extern const int8_t  conv1_weights[8 * 1 * 5 * 5];
extern const int32_t conv1_bias[8];
extern const int8_t  conv2_weights[16 * 8 * 5 * 5];
extern const int32_t conv2_bias[16];
extern const int8_t  fc_weights[10 * 256];
extern const int32_t fc_bias[10];

/* ---- Buffer addresses ---- */
#define WEIGHT_CONV2_ADDR   (NVDLA_WEIGHT_BASE + 0x0000)   /* 3200B */
#define WEIGHT_CONV2B_ADDR  (NVDLA_WEIGHT_BASE + 0x0D00)   /* 64B  */
#define WEIGHT_FC_ADDR      (NVDLA_WEIGHT_BASE + 0x0E00)   /* 2560B */
#define WEIGHT_FCB_ADDR     (NVDLA_WEIGHT_BASE + 0x1800)   /* 40B  */

#define BUF0                NVDLA_BUF0_BASE   /* SW conv1 output (8x12x12) */
#define BUF1                NVDLA_BUF1_BASE   /* NVDLA conv2 output */

/* ---- Software conv1: 1x28x28 → 8x24x24, 5x5, no padding, stride=1 ---- */
/* Output is INT8 after ReLU (bias add + clamp) */
static int8_t sw_conv1_out[8 * 24 * 24];

static void sw_conv1(void) {
    for (int k = 0; k < 8; k++) {
        for (int r = 0; r < 24; r++) {
            for (int c = 0; c < 24; c++) {
                int32_t acc = conv1_bias[k];
                for (int ir = 0; ir < 5; ir++)
                    for (int ic = 0; ic < 5; ic++) {
                        int8_t w = conv1_weights[k * 25 + ir * 5 + ic];
                        int8_t x = mnist_test_img[(r + ir) * 28 + (c + ic)];
                        acc += (int32_t)w * (int32_t)x;
                    }
                /* Scale to INT8 range (input_scale=127 → >>7) */
                acc = acc >> 7;
                /* ReLU */
                if (acc < 0)   acc = 0;
                if (acc > 127) acc = 127;
                sw_conv1_out[k * 24 * 24 + r * 24 + c] = (int8_t)acc;
            }
        }
    }
    uart_puts("[fw] sw conv1 done\n");
}

/* ---- Software max-pool2: 8x24x24 → 8x12x12, 2x2, stride=2 ---- */
static int8_t sw_pool1_out[8 * 12 * 12];

static void sw_pool1(void) {
    for (int k = 0; k < 8; k++)
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 12; c++) {
                int8_t mx = -128;
                for (int pr = 0; pr < 2; pr++)
                    for (int pc = 0; pc < 2; pc++) {
                        int8_t v = sw_conv1_out[k * 576 + (r*2+pr) * 24 + (c*2+pc)];
                        if (v > mx) mx = v;
                    }
                sw_pool1_out[k * 144 + r * 12 + c] = mx;
            }
    uart_puts("[fw] sw pool1 done\n");
}

/* ---- Software FC: 256 → 10 ---- */
static int32_t sw_fc_out[10];

static void sw_fc(const int8_t *input) {
    for (int o = 0; o < 10; o++) {
        int32_t acc = fc_bias[o];
        for (int i = 0; i < 256; i++)
            acc += (int32_t)fc_weights[o * 256 + i] * (int32_t)input[i];
        sw_fc_out[o] = acc;
    }
}

static int32_t sw_argmax(const int32_t *v, int n) {
    int32_t best_idx = 0, best_val = v[0];
    for (int i = 1; i < n; i++)
        if (v[i] > best_val) { best_val = v[i]; best_idx = i; }
    return best_idx;
}

/* ---- Load weights into SRAM for NVDLA DMA ---- */
static void load_weights(void) {
    memcpy((void *)WEIGHT_CONV2_ADDR,  conv2_weights,  sizeof(conv2_weights));
    memcpy((void *)WEIGHT_CONV2B_ADDR, conv2_bias,     sizeof(conv2_bias));
    memcpy((void *)WEIGHT_FC_ADDR,     fc_weights,     sizeof(fc_weights));
    memcpy((void *)WEIGHT_FCB_ADDR,    fc_bias,        sizeof(fc_bias));
    uart_puts("[fw] weights loaded\n");
}

/* ---- Main ---- */
int main(void) {
    uart_init();
    uart_puts("\n============================\n");
    uart_puts(" NVDLA MNIST Inference Demo \n");
    uart_puts(" ZCU104 + CVA6 RV64IMAC    \n");
    uart_puts("============================\n");

    /* Init NVDLA (clears IRQs, masks interrupts for polling mode) */
    nvdla_init();

    /* --- Software layers (conv1 + pool1) --- */
    uart_puts("[fw] running sw conv1...\n");
    sw_conv1();
    uart_puts("[fw] running sw pool1...\n");
    sw_pool1();

    /* Copy pool1 output to BUF0 for NVDLA DMA */
    memcpy((void *)BUF0, sw_pool1_out, sizeof(sw_pool1_out));

    /* Load NVDLA weights */
    load_weights();

    /* --- NVDLA Conv2: 8x12x12 → 16x8x8 (5x5, stride=1, no pad) --- */
    uart_puts("[fw] running nvdla conv2...\n");
    nvdla_conv_cfg_t c2 = {
        .src_addr     = BUF0,
        .in_width     = 12, .in_height = 12, .in_channels = 8,
        .wt_addr      = WEIGHT_CONV2_ADDR,
        .kernel_w     = 5,  .kernel_h  = 5,
        .out_channels = 16,
        .pad          = 0,  .stride    = 1,
        .dst_addr     = BUF1,
        .out_width    = 8,  .out_height = 8,
        .bias_addr    = WEIGHT_CONV2B_ADDR,
        .input_scale  = 127,
    };
    if (nvdla_run_conv(&c2) != NVDLA_OK) {
        uart_puts("[fw] conv2 FAILED\n");
        goto fail;
    }

    /* --- NVDLA Pool2: 16x8x8 → 16x4x4 (max, 2x2, stride=2) --- */
    uart_puts("[fw] running nvdla pool2...\n");
    nvdla_pool_cfg_t p2 = {
        .src_addr   = BUF1,
        .in_width   = 8, .in_height = 8, .channels = 16,
        .dst_addr   = BUF0,
        .pool_w     = 2, .pool_h    = 2,
        .stride_w   = 2, .stride_h  = 2,
        .pad        = 0,
    };
    if (nvdla_run_pool(&p2) != NVDLA_OK) {
        uart_puts("[fw] pool2 FAILED\n");
        goto fail;
    }

    /* --- Software FC: 256 → 10 (16x4x4=256 bytes in BUF0) --- */
    uart_puts("[fw] running sw fc...\n");
    sw_fc((const int8_t *)BUF0);

    /* --- Result --- */
    int32_t pred = sw_argmax(sw_fc_out, 10);

    uart_puts("\n--- RESULT ---\n");
    uart_puts("Scores: ");
    for (int i = 0; i < 10; i++) {
        uart_put_dec(i);
        uart_putc(':');
        uart_put_dec(sw_fc_out[i] >> 7);   /* rough scale */
        uart_putc(' ');
    }
    uart_putc('\n');
    uart_puts("Predicted digit: ");
    uart_put_dec(pred);
    uart_puts("  (expected: ");
    uart_put_dec(mnist_test_label);
    uart_puts(")\n");

    if (pred == mnist_test_label)
        uart_puts(">>> PASS: Correct prediction!\n");
    else
        uart_puts(">>> WARN: Wrong prediction (model may need calibration)\n");

    nvdla_dump_status();
    uart_puts("[fw] done.\n");

    /* Spin forever */
    while (1) { for (volatile int i = 0; i < 1000000; i++); uart_putc('.'); }
    return 0;

fail:
    nvdla_dump_status();
    uart_puts("[fw] FAILED.\n");
    while (1);
    return 1;
}
