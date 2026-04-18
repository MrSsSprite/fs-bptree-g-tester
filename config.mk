# Force the default goal to be 'all', even if other rules appear first
.DEFAULT_GOAL := all

# Get the directory where this config.mk resides
ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Define global variables using the root path
CC         := gcc
CFLAGS     := -Wall -Wextra
UNITY_DIR  := $(ROOT_DIR)/deps/unity
CORE_DIR   := $(ROOT_DIR)/core
SRC_DIR    := $(CORE_DIR)/src
TEST_DIR   := $(ROOT_DIR)/test
OBJ_DIR    := $(ROOT_DIR)/obj
BIN_DIR    := $(ROOT_DIR)/bin
RESULT_DIR := $(ROOT_DIR)/result

CORE_H_DIRS := $(shell find $(CORE_DIR) -name "*.h" -exec dirname {} + | sort -u)
INCLDUES = $(UNITY_DIR)/src/ $(CORE_H_DIRS)
CPPFLAGS = $(addprefix -I, $(INCLDUES))

UNITY_SRC := $(UNITY_DIR)/src/unity.c
UNITY_OBJ := $(OBJ_DIR)/unity.o
$(UNITY_OBJ): $(UNITY_SRC)
	@echo "Building Unity framework..."
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
