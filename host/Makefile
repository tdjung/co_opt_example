# Host (x86) build of the firmware for golden generation.
#   make            -> build/kws_host
#   make run WAV=../data/audio_synth/tone440.wav
# Requires: gcc, python3 (+numpy) for generated sources.

FW      := ../firmware
GEN     := ../model/generated
BUILD   := build$(VARIANT)
CC      ?= gcc

CMSIS_DSP := $(FW)/third_party/cmsis-dsp
CMSIS_NN  := $(FW)/third_party/cmsis-nn

# Same numeric flags as the target build (see firmware/target.mk): no FMA
# contraction, no fast-math -> bit-identical float results.
CFLAGS  := $(EXTRA_CFLAGS) -O2 -std=gnu11 -ffp-contract=off -fno-fast-math -fno-strict-aliasing -Wall -Wno-unused-function \
           -DHOST_BUILD -DCPU_HZ=100000000u -D__GNUC_PYTHON__ -DARM_MATH_LOOPUNROLL \
           -I$(FW)/app -I$(FW)/hal -I$(FW)/kernels -I$(GEN) \
           -I$(CMSIS_DSP)/Include -I$(CMSIS_DSP)/PrivateInclude -I$(CMSIS_NN)/Include

APP_SRC := $(wildcard $(FW)/app/*.c) $(wildcard $(FW)/kernels/*.c) $(FW)/hal/host/hal_host.c \
           $(GEN)/kws_weights.c $(GEN)/mfcc_tables.c

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
OBJ := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(SRC)))

all: $(BUILD)/kws_host

$(GEN)/kws_weights.c $(GEN)/kws_weights.h:
	python3 ../model/gen_weights.py
$(GEN)/mfcc_tables.c $(GEN)/mfcc_tables.h:
	python3 ../model/gen_mfcc_tables.py

$(BUILD)/%.o: ../%.c $(GEN)/kws_weights.h $(GEN)/mfcc_tables.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kws_host: $(OBJ)
	$(CC) $(OBJ) -o $@ -lm

run: $(BUILD)/kws_host
	./$(BUILD)/kws_host --wav $(WAV)

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
