######################################
# target
######################################
TARGET = test_automl


######################################
# building variables
######################################
# debug build?
DEBUG = 1
# optimization
OPT = -O0

#######################################
# paths
#######################################
BUILD_DIR = build

######################################
# source
######################################
SOURCE_ROOT = ../..

# 只编译 ML 核心文件 + 预置原语 + 测试
C_SOURCES = \
$(SOURCE_ROOT)/bos/algorithm/algo_crc.c \
$(SOURCE_ROOT)/bos/algorithm/algo_ml.c \
$(SOURCE_ROOT)/bos/algorithm/algo_signal.c \
$(SOURCE_ROOT)/bos/algorithm/algo_fft.c \
./algo_e2e_test_automl.c \
./test_automl_e2e.c \
./test_algo_signal.c \
./test_algo_fft.c \
./automl_e2e_main.c

# ASM sources
ASM_SOURCES =

#######################################
# binaries
#######################################
CC = gcc
CFLAGS = -I$(SOURCE_ROOT)/bos -I$(SOURCE_ROOT)/bos/algorithm/inc -I./_config/ $(OPT) -Wall -std=c99 -ffp-contract=off -g
CFLAGS += -D_ALGO_ML_ENABLE=1 -D_ALGO_SIGNAL_ENABLE=1 -D_ALGO_FFT_ENABLE=1

ifeq ($(DEBUG), 1)
CFLAGS += -g
endif

LDFLAGS = -lm

#######################################
# build
#######################################
all: $(BUILD_DIR)/$(TARGET)

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo ""
	@echo "✅ 构建成功: $@"
	@echo ""

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

test: $(BUILD_DIR)/$(TARGET)
	@echo ""
	@echo "运行测试..."
	@./$(BUILD_DIR)/$(TARGET)

-include $(wildcard $(BUILD_DIR)/*.d)
