# Project Settings
PROJECT = firmware
DEVICE = STM32F103xE 
CPU = cortex-m3

# Directories
SRCDIR = src drivers common/src/dev dsp_lib wizchip
INCDIR = include
BINDIR = bin
OBJDIR = obj

# Include paths
COM_DIR = src \
          drivers \
          common \
          common/include \
          common/cmsis \
          common/cmsis/Include \
          common/cmsis/Device/ST/STM32F1xx/Include \
          wizchip \
          third_party/stm32f1xx-hal-driver/Inc \
          uip/uip

INCLUDE = $(foreach dir, $(COM_DIR), -I$(dir)) -I$(INCDIR)

# Linker Script
LSCRIPT = STM32F103XD_FLASH.ld

# Sources
SRC = $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
ASM = $(wildcard src/*.s)

# Object files
objs_comp = $(addprefix $(OBJDIR)/, $(notdir $(SRC:.c=.o)))
# FORCE LINKING: Explicitly adding no_os_util.o to fix undefined reference errors

objs_comp += $(OBJDIR)/all_asm.o

# Defines
DEFINE = -D$(DEVICE)
DEFINE += -DSTM32F1
DEFINE += -DUSE_USB_FS
DEFINE += -DHSE_VALUE=8000000UL
DEFINE += -D_WIZCHIP_=W5500
DEFINE += -D_WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_
DEFINE += -D__STATIC_FORCEINLINE='static inline __attribute__((always_inline))'
DEFINE += -DETHERNET_LED_GPIO=GPIOA -DETHERNET_LED_PIN=GPIO_PIN_5
DEFINE += -DETHERNET_CS_GPIO=GPIOA -DETHERNET_CS_PIN=GPIO_PIN_4
DEFINE += -DETHERNET_INT_GPIO=GPIOA -DETHERNET_INT_PIN=GPIO_PIN_3

# Tools
CC = arm-none-eabi-gcc
AS = arm-none-eabi-as
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size
OBJDUMP = arm-none-eabi-objdump

# Flags
CCOMMONFLAGS = -Wall -Os -fno-common -fno-keep-inline-functions -mthumb -mcpu=$(CPU) --specs=nosys.specs --specs=nano.specs -g
GCFLAGS = -std=c11 $(CCOMMONFLAGS) $(INCLUDE) $(DEFINE)
LDFLAGS = -T$(LSCRIPT) -mthumb -mcpu=$(CPU) --specs=nosys.specs --specs=nano.specs \
          -Wl,-Map,$(BINDIR)/$(PROJECT).map -Wl,--gc-sections

# Search paths for source files
vpath %.c $(SRCDIR)
vpath %.s src

## Build Rules

all: $(BINDIR)/$(PROJECT).bin $(BINDIR)/$(PROJECT).hex

$(BINDIR)/$(PROJECT).elf: $(objs_comp)
	@mkdir -p $(dir $@)
	@echo "=== Linking $@ ==="
	$(CC) $(objs_comp) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BINDIR)/$(PROJECT).bin: $(BINDIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BINDIR)/$(PROJECT).hex: $(BINDIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

# Explicit rule for the Analog Devices utility file inside drivers/
$(OBJDIR)/no_os_util.o: drivers/no_os_util.c
	@mkdir -p $(dir $@)
	@echo "=== Compiling Explicit Target $< ==="
	$(CC) $(GCFLAGS) -c $< -o $@

# General compilation rule for C files
$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "=== Compiling $< ==="
	$(CC) $(GCFLAGS) -c $< -o $@

# Compile Assembly files (Startup code)
$(OBJDIR)/all_asm.o: $(ASM)
	@mkdir -p $(dir $@)
	@echo "=== Assembling $< ==="
	$(AS) -mcpu=$(CPU) -o $@ -c $^

clean:
	$(RM) -rf $(BINDIR) $(OBJDIR)

flash: all
	openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c "program $(BINDIR)/$(PROJECT).bin 0x08000000 verify reset exit"

reset:
	openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c "adapter_khz 1000" -c "reset_config srst_only" -c "init; reset run; exit"