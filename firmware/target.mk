# Target (Cortex-M4) build.
#   make -f target.mk                         -> build_target/kws.elf
#   make -f target.mk LDSCRIPT=link/generated.ld    -> use a tier-generated linker script
#   make -f target.mk EXTRA_CFLAGS="-DOFFLOAD_PW1=1"
# Requires arm-none-eabi-gcc (Ubuntu: apt install gcc-arm-none-eabi).
# Run from firmware/.

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
BUILD   ?= build_target
LDSCRIPT ?= link/cm4_template.ld
GEN     ?= ../model/generated
CPU_HZ  ?= 100000000u

CMSIS_CORE := third_party/cmsis-core
CMSIS_DSP  := third_party/cmsis-dsp
CMSIS_NN   := third_party/cmsis-nn

ARCH    := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
# Numeric determinism vs host: -ffp-contract=off (no VFMA), no fast-math.
CFLAGS  := $(EXTRA_CFLAGS) $(ARCH) -O2 -std=gnu11 -ffp-contract=off -fno-fast-math -fno-strict-aliasing \
           -ffunction-sections -fdata-sections -Wall -Wno-unused-function \
           -DCPU_HZ=$(CPU_HZ) -DARM_MATH_CM4 -DARM_MATH_DSP -DARM_MATH_LOOPUNROLL -DCMSIS_NN_USE_SINGLE_ROUNDING \
           -Iapp -Ihal -Ihal/target -Ikernels -I$(GEN) \
           -I$(CMSIS_CORE)/Include -I$(CMSIS_DSP)/Include -I$(CMSIS_DSP)/PrivateInclude -I$(CMSIS_NN)/Include
LDFLAGS := $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD)/kws.map --specs=nano.specs --specs=nosys.specs -lc -lm

ifeq ($(SMOKE),1)
BUILD   := build_smoke
APP_SRC := ../tests/target_smoke/smoke.c hal/target/hal_target.c startup/startup_cm4.c
GEN_SRC :=
else
APP_SRC := $(wildcard app/*.c) $(wildcard kernels/*.c) hal/target/hal_target.c startup/startup_cm4.c
GEN_SRC := $(GEN)/kws_weights.c $(GEN)/mfcc_tables.c
endif
DSP_SRC := $(CMSIS_DSP)/Source/TransformFunctions/TransformFunctions.c \
           $(CMSIS_DSP)/Source/CommonTables/CommonTables.c \
           $(CMSIS_DSP)/Source/FastMathFunctions/FastMathFunctions.c \
           $(CMSIS_DSP)/Source/BasicMathFunctions/BasicMathFunctions.c \
           $(CMSIS_DSP)/Source/ComplexMathFunctions/ComplexMathFunctions.c \
           $(CMSIS_DSP)/Source/StatisticsFunctions/StatisticsFunctions.c \
           $(CMSIS_DSP)/Source/SupportFunctions/SupportFunctions.c \
           $(CMSIS_DSP)/Source/MatrixFunctions/MatrixFunctions.c
NN_SRC  := $(wildcard $(CMSIS_NN)/Source/ConvolutionFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/FullyConnectedFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/PoolingFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/SoftmaxFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/ActivationFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/NNSupportFunctions/*.c) \
           $(wildcard $(CMSIS_NN)/Source/BasicMathFunctions/*.c)

SRC := $(APP_SRC) $(DSP_SRC) $(NN_SRC)
OBJ := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(SRC))) $(patsubst %.c,$(BUILD)/gen/%.o,$(notdir $(GEN_SRC)))

all: $(BUILD)/kws.elf $(BUILD)/kws.bin
	$(SIZE) $(BUILD)/kws.elf

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/%.o: ../tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gen/%.o: $(GEN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kws.elf: $(OBJ) $(LDSCRIPT)
	$(CC) $(OBJ) $(LDFLAGS) -o $@
$(BUILD)/kws.bin: $(BUILD)/kws.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
.PHONY: all clean
