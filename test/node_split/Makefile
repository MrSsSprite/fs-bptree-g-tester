include ../../config.mk

TARGET_NAME := node_split
TARGET      := $(BIN_DIR)/$(TARGET_NAME)

TU_SRC_DIR := src

SRCS += main.c templates.c temp_split.c split_sampler.c tools.c progress.c
SRCS_PATH = $(addprefix $(TU_SRC_DIR)/, $(SRCS))
OBJS := $(SRCS_PATH:%.c=$(OBJ_DIR)/$(TARGET_NAME)/%.o) $(PREREQ_OBJS)

SAMPLER_TEST := $(BIN_DIR)/node_split_sampler
SAMPLER_TEST_OBJS := $(OBJ_DIR)/$(TARGET_NAME)/checks/sampler.o \
                     $(OBJ_DIR)/$(TARGET_NAME)/src/split_sampler.o $(UNITY_OBJ)
LIFECYCLE_TEST := $(BIN_DIR)/node_split_lifecycle
LIFECYCLE_TEST_OBJS := $(OBJ_DIR)/$(TARGET_NAME)/checks/lifecycle.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/temp_split.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/split_sampler.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/progress.o $(UNITY_OBJ)

.PHONY: all check clean

all: $(TARGET) $(SAMPLER_TEST) $(LIFECYCLE_TEST)

check: all
	$(SAMPLER_TEST)
	python3 checks/lifecycle.py $(LIFECYCLE_TEST)

$(SAMPLER_TEST): $(SAMPLER_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(SAMPLER_TEST_OBJS) -o $@

$(LIFECYCLE_TEST): $(LIFECYCLE_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LIFECYCLE_TEST_OBJS) -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(OBJS) -o $@

$(OBJ_DIR)/$(TARGET_NAME)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/$(TARGET_NAME) $(TARGET) $(SAMPLER_TEST) $(LIFECYCLE_TEST)
