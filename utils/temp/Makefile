include ../../config.mk

# The template utilities: stand-alone programs on top of the library, built like
# a test unit but with one binary per tool.  `make' from the repository root
# builds every directory under `utils/'.
TU_NAME    := temp
TU_SRC_DIR := src
TU_OBJ_DIR := $(OBJ_DIR)/utils/$(TU_NAME)

TARGETS := temp_gen temp_inst temp_verify
BINS    := $(addprefix $(BIN_DIR)/, $(TARGETS))

.PHONY: all check clean

all: $(BINS)

# Keep arithmetic boundaries testable without allocating enormous templates.
check: $(BINS) $(TU_OBJ_DIR)/test_pointer_limits
	$(TU_OBJ_DIR)/test_pointer_limits
	python3 test/pointer_overflow.py $(BIN_DIR)/temp_gen $(BIN_DIR)/temp_verify
	python3 tests/test_kv_overflow.py
	python3 tests/test_verify_records.py

$(TU_OBJ_DIR)/test_pointer_limits: test/pointer_limits.c src/temp_full.c \
                                 src/temp_full.h $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(CORE_OBJS) -o $@

# `temp_full.o' is the generator, the name convention, the status names and the
# parent directory helper every tool shares; the library objects are the same
# ones the units link.
$(BIN_DIR)/temp_gen: $(TU_OBJ_DIR)/main_gen.o $(TU_OBJ_DIR)/temp_full.o \
                     $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $^ -o $@

$(BIN_DIR)/temp_inst: $(TU_OBJ_DIR)/main_inst.o $(TU_OBJ_DIR)/temp_inst.o \
                      $(TU_OBJ_DIR)/temp_full.o $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $^ -o $@

# the verifier is the one tool that runs Unity: `temp_full_verify' asserts
$(BIN_DIR)/temp_verify: $(TU_OBJ_DIR)/main_verify.o $(TU_OBJ_DIR)/temp_verify.o \
                        $(TU_OBJ_DIR)/temp_full.o $(UNITY_OBJ) $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $^ -o $@

$(TU_OBJ_DIR)/%.o: $(TU_SRC_DIR)/%.c
	@echo "Compiling template utility: $<"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(TU_OBJ_DIR) $(BINS)
