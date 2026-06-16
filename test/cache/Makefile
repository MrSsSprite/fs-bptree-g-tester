include ../../config.mk

TARGET_NAME := cache
TARGET      := $(BIN_DIR)/$(TARGET_NAME)

TU_SRC_DIR := src

SRCS += main.c cache.c
SRCS_PATH = $(addprefix $(TU_SRC_DIR)/, $(SRCS))
OBJS := $(SRCS_PATH:%.c=$(OBJ_DIR)/$(TARGET_NAME)/%.o) $(PREREQ_OBJS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(OBJS) -o $@

$(OBJ_DIR)/$(TARGET_NAME)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/$(TARGET_NAME) $(TARGET)
