include ../../config.mk

# The template utilities: stand-alone programs on top of the library, built like
# a test unit but with one binary per tool.  `make' from the repository root
# builds every directory under `utils/'.
TU_NAME    := temp
TU_SRC_DIR := src
TU_OBJ_DIR := $(OBJ_DIR)/utils/$(TU_NAME)

TARGETS := temp_gen temp_inst temp_verify
BINS    := $(addprefix $(BIN_DIR)/, $(TARGETS))
SHAPE_OBJ := $(TU_OBJ_DIR)/temp_shape.o
IMAGE_OBJS := $(addprefix $(TU_OBJ_DIR)/,temp_image.o temp_compare.o temp_json.o temp_manifest.o)

# dlopen is in libc on some POSIX systems and libdl on others. Probe the linker
# rather than assume Linux or Darwin. Cross builds may override DL_LIBS and the
# compiler-specific shared-library flags on the make command line.
DL_LIBS ?= $(shell t=$${TMPDIR:-/tmp}/bptr-dl-probe.$$$$; \
 if (umask 077 && mkdir "$$t"); then \
 printf '%s\n' '\#include <dlfcn.h>' 'int main(void) { return dlopen(0, RTLD_NOW) == 0; }' > "$$t/probe.c"; \
 if $(CC) "$$t/probe.c" -o "$$t/probe" >/dev/null 2>&1; then :; else printf '%s' '-ldl'; fi; \
 rm -rf "$$t"; else printf '%s' '-ldl'; fi)
PLUGIN_CFLAGS ?= -fPIC
PLUGIN_LDFLAGS ?= $(if $(filter Darwin,$(shell uname -s)),-dynamiclib,-shared)

.PHONY: all check clean

all: $(BINS)

# Keep arithmetic boundaries testable without allocating enormous templates.
check: $(BINS) $(TU_OBJ_DIR)/test_pointer_limits $(TU_OBJ_DIR)/test_shape_limits check-comparator check-image check-manifest check-empty check-json
	$(TU_OBJ_DIR)/test_pointer_limits
	$(TU_OBJ_DIR)/test_shape_limits
	python3 test/pointer_overflow.py $(BIN_DIR)/temp_gen $(BIN_DIR)/temp_verify
	python3 tests/test_kv_overflow.py
	python3 tests/test_verify_records.py
	python3 tests/test_verify_parents.py
	python3 tests/test_verify_overflow.py
	python3 tests/test_verify_occupancy.py
	python3 tests/test_shapes.py

.PHONY: check-comparator
check-comparator: $(TU_OBJ_DIR)/test_comparator $(TU_OBJ_DIR)/comparator_plugin.so
	$(TU_OBJ_DIR)/test_comparator $(TU_OBJ_DIR)/comparator_plugin.so

$(TU_OBJ_DIR)/test_comparator: test/comparator.c src/temp_compare.c src/temp_compare.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isrc test/comparator.c src/temp_compare.c $(DL_LIBS) -o $@

$(TU_OBJ_DIR)/comparator_plugin.so: tests/comparator_plugin.c src/temp_compare.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) -Isrc $< -o $@

.PHONY: check-image
check-image: $(TU_OBJ_DIR)/test_image
	$(TU_OBJ_DIR)/test_image

$(TU_OBJ_DIR)/test_image: test/image.c src/temp_image.c src/temp_image.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isrc test/image.c src/temp_image.c -o $@

.PHONY: check-json
check-json: $(TU_OBJ_DIR)/test_json
	$(TU_OBJ_DIR)/test_json

$(TU_OBJ_DIR)/test_json: test/json.c src/temp_json.c src/temp_json.h src/temp_sort.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isrc test/json.c -o $@

.PHONY: check-empty
check-empty: $(TU_OBJ_DIR)/test_empty_image
	$(TU_OBJ_DIR)/test_empty_image

$(TU_OBJ_DIR)/test_empty_image: test/empty_image.c $(TU_OBJ_DIR)/temp_image.o $(CORE_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc $^ $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: check-manifest
check-manifest: $(BINS) check-comparator $(TU_OBJ_DIR)/manifest_gen $(TU_OBJ_DIR)/manifest_verify
	CC='$(CC)' TEMP_PLUGIN_FLAGS='$(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS)' python3 tests/test_manifest.py
	MANIFEST_GEN='$(TU_OBJ_DIR)/manifest_gen' MANIFEST_VERIFY='$(TU_OBJ_DIR)/manifest_verify' \
	 CC='$(CC)' TEMP_PLUGIN_FLAGS='$(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS)' python3 tests/test_manifest.py

$(TU_OBJ_DIR)/test_pointer_limits: test/pointer_limits.c src/temp_full.c \
                                 src/temp_full.h src/temp_shape.h $(SHAPE_OBJ) $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(SHAPE_OBJ) $(CORE_OBJS) -o $@

$(TU_OBJ_DIR)/test_shape_limits: test/shape_limits.c src/temp_shape.h $(SHAPE_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(SHAPE_OBJ) -o $@

# `temp_full.o' is the generator, the name convention, the status names and the
# parent directory helper every tool shares; the library objects are the same
# ones the units link.
$(BIN_DIR)/temp_gen: $(TU_OBJ_DIR)/main_gen.o $(TU_OBJ_DIR)/temp_full.o \
                     $(SHAPE_OBJ) $(CORE_OBJS) $(IMAGE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(DL_LIBS) $(LDLIBS) -o $@

$(BIN_DIR)/temp_inst: $(TU_OBJ_DIR)/main_inst.o $(TU_OBJ_DIR)/temp_inst.o \
                      $(TU_OBJ_DIR)/temp_full.o $(SHAPE_OBJ) $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

# the verifier is the one tool that runs Unity: `temp_full_verify' asserts
$(BIN_DIR)/temp_verify: $(TU_OBJ_DIR)/main_verify.o $(TU_OBJ_DIR)/temp_verify.o \
                        $(TU_OBJ_DIR)/temp_full.o $(SHAPE_OBJ) $(UNITY_OBJ) $(CORE_OBJS) $(IMAGE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(DL_LIBS) $(LDLIBS) -o $@

# These two drivers prove that the manifest backend has no core/Unity linkage.
$(TU_OBJ_DIR)/manifest_gen: test/manifest_main.c $(IMAGE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -DTEMP_MANIFEST_GENERATE $^ $(DL_LIBS) $(LDLIBS) -o $@

$(TU_OBJ_DIR)/manifest_verify: test/manifest_main.c $(IMAGE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc $^ $(DL_LIBS) $(LDLIBS) -o $@

$(TU_OBJ_DIR)/%.o: $(TU_SRC_DIR)/%.c $(wildcard src/*.h)
	@echo "Compiling template utility: $<"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(TU_OBJ_DIR) $(BINS)
