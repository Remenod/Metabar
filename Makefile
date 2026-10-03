BOOT_DIR := boot
SRC_DIR := src
BUILD_DIR := build
INCLUDE_DIRS := include include/arch/x86

ASM := nasm
CC := i386-elf-gcc
CXX := i386-elf-g++
LD := i386-elf-ld
OBJCOPY := i386-elf-objcopy

CFLAGS := -ffreestanding -O2 -Wall -Wextra -m32 $(foreach dir,$(INCLUDE_DIRS),-I$(dir))
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LDFLAGS := -T $(SRC_DIR)/kernel/linker.ld

BOOT_SRC := $(BOOT_DIR)/boot.asm
STAGE2_SRC := $(BOOT_DIR)/stage2.asm
DISK_INC := $(BOOT_DIR)/disk.inc
ENTRY_SRC := $(SRC_DIR)/kernel/kernel_entry.asm

BOOT_BIN := $(BUILD_DIR)/boot.bin
STAGE2_BIN := $(BUILD_DIR)/stage2.bin
ENTRY_OBJ := $(BUILD_DIR)/kernel/kernel_entry.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
IMAGE := $(BUILD_DIR)/metabar.img

C_SRCS := $(shell find $(SRC_DIR) -name "*.c")
CPP_SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
ASM_SRCS := $(shell find $(SRC_DIR) -name "*.asm")

C_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
CPP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SRCS))
ASM_OBJS := $(patsubst $(SRC_DIR)/%.asm,$(BUILD_DIR)/%.o,$(ASM_SRCS))

.PHONY: all clean run pad_kernel

all: $(IMAGE)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@

$(KERNEL_ELF): $(ENTRY_OBJ) $(C_OBJS) $(CPP_OBJS) $(ASM_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(ENTRY_OBJ) $(filter-out $(ENTRY_OBJ),$(C_OBJS) $(CPP_OBJS) $(ASM_OBJS))

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	$(MAKE) pad_kernel

# image layout: MBR | stage 2 | kernel
# stage 2 is assembled twice: the first pass only measures how many sectors it takes,
# which both stages need to know to find what follows it on the disk
$(IMAGE): $(BOOT_SRC) $(STAGE2_SRC) $(DISK_INC) $(KERNEL_BIN) | $(BUILD_DIR)
	@ksectors=$$(( ($$(stat -c%s $(KERNEL_BIN)) + 511)/512 )); \
	defines="-DKERNEL_SECTORS=$$ksectors"; \
	$(ASM) -f bin $(STAGE2_SRC) -o $(STAGE2_BIN) -DSTAGE2_SECTORS=1 $$defines; \
	ssectors=$$(( ($$(stat -c%s $(STAGE2_BIN)) + 511)/512 )); \
	$(ASM) -f bin $(STAGE2_SRC) -o $(STAGE2_BIN) -DSTAGE2_SECTORS=$$ssectors $$defines; \
	if [ $$(stat -c%s $(STAGE2_BIN)) -gt $$(( ssectors * 512 )) ]; then \
		echo "stage 2 grew between passes"; exit 1; fi; \
	truncate -s $$(( ssectors * 512 )) $(STAGE2_BIN); \
	$(ASM) -f bin $(BOOT_SRC) -o $(BOOT_BIN) -DSTAGE2_SECTORS=$$ssectors; \
	cat $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) > $@; \
	echo "Sectors - stage2: $$ssectors, kernel: $$ksectors"

pad_kernel:
	@size=$$(stat -c%s $(KERNEL_BIN)); \
	pad=$$(( (512 - (size % 512)) % 512 )); \
	if [ $$pad -ne 0 ]; then \
		dd if=/dev/zero bs=1 count=$$pad >> $(KERNEL_BIN); \
	fi

run: $(IMAGE)
	qemu-system-i386 -serial stdio -drive file=$(IMAGE),format=raw 
	# -d int,cpu_reset -no-reboot -no-shutdown

run_debug: $(IMAGE)
	qemu-system-i386 -serial stdio -drive file=$(IMAGE),format=raw -d int,cpu_reset -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)
