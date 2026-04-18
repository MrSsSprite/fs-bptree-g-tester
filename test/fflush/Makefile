include ../../config.mk

TARGET_NAME := fflush
TARGET      := $(BIN_DIR)/$(TARGET_NAME)

SRCS :=
OBJS := $(SRCS:%.c=$(OBJ_DIR)/$(TARGET_NAME)/%.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(OBJS) -o $@

$(OBJ_DIR)/$(TARGET_NAME)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/$(TARGET_NAME) $(TARGET)
