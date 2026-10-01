include ../../config.mk

# The template utilities: stand-alone programs on top of the library, built like
# a test unit but with one binary per tool.  `make' from the repository root
# builds every directory under `utils/'.
TU_NAME    := temp
TU_SRC_DIR := src
TU_OBJ_DIR := $(OBJ_DIR)/utils/$(TU_NAME)

TARGETS := temp_gen temp_inst
BINS    := $(addprefix $(BIN_DIR)/, $(TARGETS))

.PHONY: all clean

all: $(BINS)

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

$(TU_OBJ_DIR)/%.o: $(TU_SRC_DIR)/%.c
	@echo "Compiling template utility: $<"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(TU_OBJ_DIR) $(BINS)
