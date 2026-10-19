include config.mk

# 1. Discover runnable tests by their Makefiles, excluding support directories
# such as test/helper/.
TEST_UNITS := $(notdir $(patsubst %/,%,$(dir $(wildcard test/*/Makefile))))
UTIL_UNITS := $(notdir $(patsubst %/,%,$(wildcard utils/*/)))

.PHONY: all clean $(TEST_UNITS) $(UTIL_UNITS)

# 2. General 'make' builds everything
all: $(TEST_UNITS) $(UTIL_UNITS)

# 3. Dynamic target creation
# This allows you to run 'make flush' or 'make node' directly from root
$(TEST_UNITS):
	@echo "Building test unit: $@"
	@$(MAKE) -C test/$@

# ... and 'make temp' for a utility directory
$(UTIL_UNITS):
	@echo "Building utility: $@"
	@$(MAKE) -C utils/$@

clean:
	@echo "Cleaning all units..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)
