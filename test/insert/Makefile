include ../../config.mk

TARGET_NAME := insert
TARGET := $(BIN_DIR)/$(TARGET_NAME)
UNIT_OBJ_DIR := $(OBJ_DIR)/$(TARGET_NAME)
CPPFLAGS += -I$(ROOT_DIR)/utils/temp/src -Isrc -D_POSIX_C_SOURCE=200809L
UNIT_FLAGS := -std=c99 -Werror
UNIT_SRCS := main.c harness.c oracle.c scenarios.c failures.c
UNIT_OBJS := $(addprefix $(UNIT_OBJ_DIR)/src/,$(UNIT_SRCS:.c=.o))
IMAGE_OBJ := $(UNIT_OBJ_DIR)/common/temp_image.o
ORACLE_CHECK := $(UNIT_OBJ_DIR)/checks/oracle

.PHONY: all check check-oracle clean
all: $(TARGET)

$(TARGET): $(UNIT_OBJS) $(IMAGE_OBJ) $(PREREQ_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(UNIT_OBJ_DIR)/src/%.o: src/%.c $(wildcard src/*.h) $(wildcard $(CORE_DIR)/*.h) $(wildcard $(SRC_DIR)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(UNIT_FLAGS) $(CPPFLAGS) -c $< -o $@

$(IMAGE_OBJ): ../../utils/temp/src/temp_image.c $(wildcard ../../utils/temp/src/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(UNIT_FLAGS) $(CPPFLAGS) -c $< -o $@

$(ORACLE_CHECK): checks/oracle.c src/oracle.c src/oracle.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(UNIT_FLAGS) $(CPPFLAGS) checks/oracle.c src/oracle.c -o $@

check-oracle: $(ORACLE_CHECK)
	$(ORACLE_CHECK)

check: all check-oracle
	cd $(ROOT_DIR) && $(TARGET)

clean:
	rm -rf $(UNIT_OBJ_DIR) $(TARGET)
