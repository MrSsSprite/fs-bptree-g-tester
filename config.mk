# Force the default goal to be 'all', even if other rules appear first
.DEFAULT_GOAL := all

# Get the directory where this config.mk resides
ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Define global variables using the root path
CC         := gcc
CFLAGS     := -Wall -Wextra -g
UNITY_DIR  := $(ROOT_DIR)/deps/unity
CORE_DIR   := $(ROOT_DIR)/core
SRC_DIR    := $(CORE_DIR)/src
TEST_DIR   := $(ROOT_DIR)/test
OBJ_DIR    := $(ROOT_DIR)/obj
BIN_DIR    := $(ROOT_DIR)/bin
RESULT_DIR := $(ROOT_DIR)/result

CORE_H_DIRS := $(shell find $(CORE_DIR) -name "*.h" -exec dirname {} + | sort -u)
TEST_UTIL_H_DIR := $(TEST_DIR)
INCLDUES = $(UNITY_DIR)/src $(CORE_H_DIRS) $(TEST_UTIL_H_DIR)
CPPFLAGS = $(addprefix -I, $(INCLDUES))

CORE_SRCS := $(shell find $(SRC_DIR) -name "*.c")
CORE_OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/core/%.o, $(CORE_SRCS))
$(OBJ_DIR)/core/%.o: $(SRC_DIR)/%.c
	@echo "Compiling core: $<"
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

TEST_UTIL_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_UTIL_OBJS := $(patsubst $(TEST_DIR)/%.c, $(OBJ_DIR)/test_util/%.o, $(TEST_UTIL_SRCS))
$(OBJ_DIR)/test_util/%.o: $(TEST_DIR)/%.c
	@echo "Compiling test utility function: $<"
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

UNITY_SRC := $(UNITY_DIR)/src/unity.c
UNITY_OBJ := $(OBJ_DIR)/unity.o
$(UNITY_OBJ): $(UNITY_SRC)
	@echo "Building Unity framework..."
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

PREREQ_OBJS := $(CORE_OBJS) $(UNITY_OBJ) $(TEST_UTIL_OBJS)
