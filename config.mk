# Get the directory where this config.mk resides
ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Define global variables using the root path
CC         := gcc
CFLAGS     := -Wall -Wextra
UNITY_DIR  := $(ROOT_DIR)/deps/unity
SRC_DIR    := $(ROOT_DIR)/core/src
TEST_DIR   := $(ROOT_DIR)/test
OBJ_DIR    := $(ROOT_DIR)/obj
BIN_DIR    := $(ROOT_DIR)/bin
RESULT_DIR := $(ROOT_DIR)/result

INCLDUES = $(UNITY_DIR)/src/
CPPFLAGS = $(addprefix -I, $(INCLDUES))
