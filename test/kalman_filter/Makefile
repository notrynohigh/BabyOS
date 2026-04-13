######################################
# target
######################################
TARGET = test_kalman_filter

######################################
# building variables
######################################
DEBUG = 1
OPT = -O0

#######################################
# paths
#######################################
BUILD_DIR = build
SOURCE_ROOT = ../..

######################################
# source
######################################
C_SOURCES = \
$(SOURCE_ROOT)/bos/algorithm/algo_kalman_filter.c \
$(SOURCE_ROOT)/test/kalman_filter/test.c

#######################################
# binaries
#######################################
CC = gcc
CFLAGS = -I$(SOURCE_ROOT)/bos/algorithm/inc \
         -I$(SOURCE_ROOT)/bos/core/inc \
         -I$(SOURCE_ROOT)/test \
         -D_ALGO_KALMAN_FILTER_ENABLE=1 \
         $(OPT) -Wall -std=c99 -g

LDFLAGS = -lm

#######################################
# build
#######################################
all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(C_SOURCES)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(C_SOURCES) -o $@ $(LDFLAGS)

clean:
	-rm -fR $(BUILD_DIR)

run: $(BUILD_DIR)/$(TARGET)
	./$(BUILD_DIR)/$(TARGET)

.PHONY: all clean run
