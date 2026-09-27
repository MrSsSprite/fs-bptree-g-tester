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

# Reveal the `BPTR_STATIC' internals (`bptr_node_split', `bptr_find_node',
# `bptr_node_prealloc') to the units: with the macro defined, `bptr_internal.h'
# expands `BPTR_STATIC' to nothing instead of `static', so those functions keep
# external linkage and can be called from a test.  It is set here, for every
# unit, and not in one unit's Makefile: the core objects in `obj/core/' are
# shared by every unit and make does not track the flags they were built with,
# so a per-unit macro would silently link objects built against the other
# setting.  A test that needs one of these symbols declares it in the unit's own
# header (see `test/node_split/src/bptr_static.h').
CPPFLAGS += -DBPTR_TESTING

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
