# X Scope for the FNIRSI 1014D
#
#   make            build/x-scope-1014d.bin        SD card image, written at 8 KiB on the card
#                   build/x-scope-1014d-ram.bin     firmware only, for loading over USB (FEL)
#   make clean
#
# Needs arm-none-eabi-gcc (GNU Arm toolchain), GNU make and Python 3.

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
PYTHON  ?= python3
BUILD   := build

FW_DIR  := firmware
LD_DIR  := loader

FW_CFLAGS  := -Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector -DNO_STDLIB=1 \
              -mcpu=arm926ej-s -O2 -mfloat-abi=soft -Wno-error=implicit-function-declaration \
              -Wno-error=implicit-int -Wno-error=int-conversion -fcommon -fno-builtin-strcpy -g \
              -I$(BUILD)
FW_ASFLAGS := -x assembler-with-cpp -c -O0 -mcpu=arm926ej-s -mthumb -Wall -fmessage-length=0 -g

LD_CFLAGS  := -g -O0 -Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector -fcommon \
              -DNO_STDLIB=1 -mcpu=arm926ej-s
LD_ASFLAGS := -mcpu=arm926ej-s -g -x assembler-with-cpp -c

# Link order matters for the image layout
FW_OBJECTS := DS3231.o \
              PC_interface.o \
              ccu_control.o \
              cdc_class.o \
              diskio.o \
              display_control.o \
              display_lib.o \
              ff.o \
              ffunicode.o \
              fnirsi_101xd_scope.o \
              font_0.o \
              fonts_1014d.o \
              font_1.o \
              font_2.o \
              font_3.o \
              font_4.o \
              font_5.o \
              fpga_control.o \
              generator.o \
              icons.o \
              interrupt.o \
              mass_storage_class.o \
              ideal_1014d.o \
              xscope_logo.o \
              mmu.o \
              remote.o \
              main_menu_ideal_icons.o \
              memcmp.o \
              memcpy.o \
              memmove.o \
              memset.o \
              strlen.o \
              menu.o \
              menu_1014d.o \
              menu_ideal.o \
              power_and_battery.o \
              ref_and_math.o \
              scope_functions.o \
              sm_1014d.o \
              sd_card_interface.o \
              sin_cos_math.o \
              spi_control.o \
              start.o \
              statemachine.o \
              clock_synthesizer.o \
              uart.o \
              test.o \
              timer.o \
              touchpanel.o \
              usb_interface.o \
              variables.o
LD_OBJECTS := bl_display_lib.o \
              bl_fonts.o \
              bl_fpga_control.o \
              bl_sd_card_interface.o \
              bl_uart.o \
              bl_variables.o \
              ccu_control.o \
              display_control.o \
              dram_control.o \
              fnirsi_1014d_startup.o \
              spi_control.o \
              start.o \
              memcmp.o \
              memcpy.o \
              memset.o

FW_OBJS := $(addprefix $(BUILD)/fw/,$(FW_OBJECTS))
LD_OBJS := $(addprefix $(BUILD)/ld/,$(LD_OBJECTS))

.PHONY: all clean
.DELETE_ON_ERROR:

all: $(BUILD)/x-scope-1014d.bin

$(BUILD)/x-scope-1014d.bin: $(BUILD)/loader.bin $(BUILD)/x-scope-1014d-ram.bin tools/sunxi_pack.py
	$(PYTHON) tools/sunxi_pack.py pack $(BUILD)/loader.bin $(BUILD)/x-scope-1014d-ram.bin $@

$(BUILD)/x-scope-1014d-ram.bin: $(BUILD)/firmware.elf tools/sunxi_pack.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/sunxi_pack.py egon $@

$(BUILD)/firmware.elf: $(FW_OBJS) $(FW_DIR)/fnirsi_101xd.ld
	$(CC) -o $@ $(FW_OBJS) -T$(FW_DIR)/fnirsi_101xd.ld -nostdlib -lgcc

$(BUILD)/loader.bin: $(BUILD)/loader.elf tools/sunxi_pack.py
	$(OBJCOPY) -O binary --only-section=.text $< $@
	$(PYTHON) tools/sunxi_pack.py egon $@

$(BUILD)/loader.elf: $(LD_OBJS) $(LD_DIR)/fnirsi_sd_card_bootloader.ld
	$(CC) -o $@ $(LD_OBJS) -T$(LD_DIR)/fnirsi_sd_card_bootloader.ld -nostdlib -lgcc

# The build time shown on the scope
$(BUILD)/build_time.h: | $(BUILD)/fw
	$(PYTHON) -c "import time; t = time.localtime(); open('$@', 'w').write('#ifndef BUILD_TIME_H\n#define BUILD_TIME_H\n#define COMPILE_YEAR %d\n#define COMPILE_MONTH %d\n#define COMPILE_DAY %d\n#define COMPILE_HOUR %d\n#define COMPILE_MIN %d\n#define COMPILE_SEC %d\n#endif\n' % (t.tm_year % 100, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec))"

$(BUILD)/fw/%.o: $(FW_DIR)/%.c $(BUILD)/build_time.h | $(BUILD)/fw
	$(CC) $(FW_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/fw/%.o: $(FW_DIR)/%.s | $(BUILD)/fw
	$(CC) $(FW_ASFLAGS) -o $@ $<

$(BUILD)/ld/%.o: $(LD_DIR)/%.c | $(BUILD)/ld
	$(CC) $(LD_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/ld/%.o: $(LD_DIR)/%.s | $(BUILD)/ld
	$(CC) $(LD_ASFLAGS) $< -o $@

$(BUILD)/fw $(BUILD)/ld:
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(FW_OBJS:.o=.d) $(LD_OBJS:.o=.d)
