include ../../config.mk

TARGET_NAME := node_split
TARGET      := $(BIN_DIR)/$(TARGET_NAME)

TU_SRC_DIR := src
SHAPE_SRC := ../../utils/temp/src/temp_shape.c
SHAPE_OBJ := $(OBJ_DIR)/$(TARGET_NAME)/common/temp_shape.o
CPPFLAGS += -I$(ROOT_DIR)/utils/temp/src

SRCS += main.c templates.c temp_split.c split_sampler.c tools.c progress.c
SRCS_PATH = $(addprefix $(TU_SRC_DIR)/, $(SRCS))
OBJS := $(SRCS_PATH:%.c=$(OBJ_DIR)/$(TARGET_NAME)/%.o) $(SHAPE_OBJ) $(PREREQ_OBJS)

SAMPLER_TEST := $(BIN_DIR)/node_split_sampler
SAMPLER_TEST_OBJS := $(OBJ_DIR)/$(TARGET_NAME)/checks/sampler.o \
                     $(OBJ_DIR)/$(TARGET_NAME)/src/split_sampler.o $(SHAPE_OBJ) $(UNITY_OBJ)
LIFECYCLE_TEST := $(BIN_DIR)/node_split_lifecycle
LIFECYCLE_TEST_OBJS := $(OBJ_DIR)/$(TARGET_NAME)/checks/lifecycle.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/temp_split.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/split_sampler.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/progress.o $(SHAPE_OBJ) $(UNITY_OBJ)
TEMPLATES_TEST := $(BIN_DIR)/node_split_templates
TEMPLATES_TEST_OBJS := $(OBJ_DIR)/$(TARGET_NAME)/checks/templates.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/templates.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/tools.o \
                       $(OBJ_DIR)/$(TARGET_NAME)/src/progress.o $(SHAPE_OBJ) $(UNITY_OBJ)

.PHONY: all check clean

all: $(TARGET) $(SAMPLER_TEST) $(LIFECYCLE_TEST) $(TEMPLATES_TEST)

check: all
	$(SAMPLER_TEST)
	python3 checks/lifecycle.py $(LIFECYCLE_TEST)
	python3 checks/templates.py $(TEMPLATES_TEST)

$(SAMPLER_TEST): $(SAMPLER_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(SAMPLER_TEST_OBJS) -o $@

$(LIFECYCLE_TEST): $(LIFECYCLE_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LIFECYCLE_TEST_OBJS) -o $@

$(TEMPLATES_TEST): $(TEMPLATES_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(TEMPLATES_TEST_OBJS) -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(OBJS) -o $@

$(SHAPE_OBJ): $(SHAPE_SRC) ../../utils/temp/src/temp_shape.h ../../utils/temp/src/temp_full.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/$(TARGET_NAME)/%.o: %.c src/templates.h src/tools.h src/split_sampler.h ../../utils/temp/src/temp_shape.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/$(TARGET_NAME) $(TARGET) $(SAMPLER_TEST) $(LIFECYCLE_TEST) $(TEMPLATES_TEST)
