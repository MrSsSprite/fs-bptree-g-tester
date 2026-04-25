include ../../config.mk

# Config Arguments
TARGET_NAME := node_split
# Test Unit Source Directory
TU_SRC_DIR := src
# Source files which need to be compiled
SRCS +=

# Auto Expansions (update not required)
TARGET      := $(BIN_DIR)/$(TARGET_NAME)
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
