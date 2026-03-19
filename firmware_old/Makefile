# Makefile — Bare-metal NVDLA firmware for ZCU104 CVA6 RV64IMAC
#
# Targets:
#   make            → firmware.bin  (ROM bake-in, linked at 0x10000000)
#   make serial     → firmware_serial.bin  (BIOS serial load, linked at 0x40000000)
#   make clean      → remove all build artifacts

CC      = /home/harsh/riscv/bin/riscv64-unknown-elf-gcc
OBJCOPY = /home/harsh/riscv/bin/riscv64-unknown-elf-objcopy
OBJDUMP = /home/harsh/riscv/bin/riscv64-unknown-elf-objdump
SIZE    = /home/harsh/riscv/bin/riscv64-unknown-elf-size

ARCH    = rv64imac
ABI     = lp64

CFLAGS  = -mcmodel=medany -march=$(ARCH) -mabi=$(ABI) -O2 -g \
          -nostdlib -ffreestanding -fno-builtin \
          -Wall -Wno-unused-function \
          -I. -Ibsp -Invdla_driver

SRCS    = bsp/crt0.S \
          bsp/uart.c \
          bsp/memutils.c \
          nvdla_driver/nvdla_drv.c \
          main.c \
          weights.c

OBJS    = $(SRCS:.S=.o)
OBJS   := $(OBJS:.c=.o)

# ---- ROM build (baked into bitstream at 0x10000000) ----
TARGET  = firmware
LDFLAGS = -T link.ld -nostdlib -static

.PHONY: all serial clean disasm

all: $(TARGET).bin
	@echo ""
	@$(SIZE) $(TARGET).elf
	@echo ""
	@echo "=== ROM build complete ==="
	@echo "  Use: python3 soc/zcu104_cva6.py --firmware-init firmware_old/firmware.bin --build"

$(TARGET).elf: $(OBJS) link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# ---- Serial build (loaded by BIOS at 0x40000000) ----
SERIAL_TARGET  = firmware_serial
SERIAL_LDFLAGS = -T link_ram.ld -nostdlib -static

serial: $(SERIAL_TARGET).bin
	@echo ""
	@$(SIZE) $(SERIAL_TARGET).elf
	@echo ""
	@echo "=== Serial build complete ==="
	@echo "  Upload with: scripts/upload_firmware.sh /dev/ttyUSB3"

$(SERIAL_TARGET).elf: $(OBJS) link_ram.ld
	$(CC) $(CFLAGS) $(SERIAL_LDFLAGS) $(OBJS) -o $@

$(SERIAL_TARGET).bin: $(SERIAL_TARGET).elf
	$(OBJCOPY) -O binary $< $@

# ---- Common rules ----
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

disasm: $(TARGET).elf
	$(OBJDUMP) -d -S $< > $(TARGET).disasm
	$(OBJDUMP) -d -S $(SERIAL_TARGET).elf >> $(SERIAL_TARGET).disasm 2>/dev/null || true

clean:
	rm -f $(OBJS) \
	      $(TARGET).elf $(TARGET).bin \
	      $(SERIAL_TARGET).elf $(SERIAL_TARGET).bin \
	      *.disasm
