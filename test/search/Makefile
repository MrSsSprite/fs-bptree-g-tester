include ../../config.mk

TARGET_NAME := search
TARGET := $(BIN_DIR)/$(TARGET_NAME)
SHAPE_SRC := ../../utils/temp/src/temp_shape.c
SHAPE_OBJ := $(OBJ_DIR)/$(TARGET_NAME)/common/temp_shape.o
CPPFLAGS += -I$(ROOT_DIR)/utils/temp/src

SRCS := main.c search.c cache_checks.c templates.c tools.c progress.c
OBJS := $(addprefix $(OBJ_DIR)/$(TARGET_NAME)/src/,$(SRCS:.c=.o)) \
        $(SHAPE_OBJ) $(PREREQ_OBJS)

.PHONY: all check clean
all: $(TARGET)

check: all
	$(MAKE) -C $(ROOT_DIR)/utils/temp
	python3 checks/integration.py

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(SHAPE_OBJ): $(SHAPE_SRC) ../../utils/temp/src/temp_shape.h ../../utils/temp/src/temp_full.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/$(TARGET_NAME)/src/%.o: src/%.c $(wildcard src/*.h) ../../utils/temp/src/temp_shape.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/$(TARGET_NAME) $(TARGET)
