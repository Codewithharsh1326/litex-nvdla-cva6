/* nvdla_loadable.h — Parse NVDLA flatbuffer loadable and dispatch operations */
#ifndef NVDLA_LOADABLE_H
#define NVDLA_LOADABLE_H

#include <stdint.h>

/* NVDLA flatbuffer loadable file starts with a 4-byte size + "NVDA" magic */
#define NVDLA_LOADABLE_MAGIC    0x4144564E  /* "NVDA" */

/* Flatbuffer table IDs for NVDLA loadable (from loadable_generated.h) */
typedef enum {
    NVDLA_OP_CDMA_WEIGHT  = 0,
    NVDLA_OP_CDMA_DATA    = 1,
    NVDLA_OP_CONV         = 2,
    NVDLA_OP_SDP_X        = 3,
    NVDLA_OP_SDP_Y        = 4,
    NVDLA_OP_SDP          = 5,
    NVDLA_OP_PDP          = 6,
    NVDLA_OP_CDP          = 7,
    NVDLA_OP_RBK          = 8,
    NVDLA_OP_SDP_EW       = 9,
} nvdla_op_type_t;

/* Simple register write list entry (from NVDLA loadable) */
typedef struct {
    uint32_t reg_offset;
    uint32_t val;
} nvdla_reg_write_t;

/* Task descriptor */
typedef struct {
    uint32_t num_reg_writes;
    nvdla_reg_write_t *reg_writes;
    uint32_t num_mem_transfers;
} nvdla_task_t;

/* High-level API */
int  nvdla_loadable_init(const uint8_t *buf, uint32_t size);
int  nvdla_loadable_run(void);

/* Internal: apply a list of register writes */
void nvdla_apply_reg_writes(const nvdla_reg_write_t *writes, uint32_t count);

#endif
