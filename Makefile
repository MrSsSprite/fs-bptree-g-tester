include config.mk

# 1. Automatically find all sub-directories in test/
# This results in a list like: flush node socket
TEST_UNITS := $(notdir $(patsubst %/,%,$(wildcard test/*/)))

.PHONY: all clean $(TEST_UNITS)

# 2. General 'make' builds everything
all: $(TEST_UNITS)

# 3. Dynamic target creation
# This allows you to run 'make flush' or 'make node' directly from root
$(TEST_UNITS):
	@echo "Building test unit: $@"
	@$(MAKE) -C test/$@

clean:
	@echo "Cleaning all units..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)
