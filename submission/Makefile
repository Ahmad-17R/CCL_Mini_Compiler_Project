# =============================================================================
# Makefile — Mini Pascal Compiler
# CS-471L Compiler Construction Lab, UET Lahore, Spring 2026
# =============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -g

TARGET   := minicompiler
SRCDIR   := src

# Collect every .cpp file under src/ recursively
SRCS := $(shell find $(SRCDIR) -name '*.cpp')

# Derive object file paths (keep them alongside their sources)
OBJS := $(SRCS:.cpp=.o)

# =============================================================================
# Default target
# =============================================================================
.PHONY: all
all: $(TARGET)
	@echo ""
	@echo "  Build successful -> ./$(TARGET)"
	@echo "  Usage: ./$(TARGET) --parser <rd|ll1|lr> <source_file>"
	@echo ""

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Pattern rule: compile each .cpp to a .o (also emit dependency files)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# =============================================================================
# Convenience targets
# =============================================================================

# Run with a quick smoke test (requires a test file)
.PHONY: test
test: all
	@echo "--- Running RD parser on test/sample.pas ---"
	./$(TARGET) --parser rd test/sample.pas || true
	@echo "--- Running LL1 parser on test/sample.pas ---"
	./$(TARGET) --parser ll1 test/sample.pas || true
	@echo "--- Running LR parser on test/sample.pas ---"
	./$(TARGET) --parser lr test/sample.pas || true

# Remove all build artefacts
.PHONY: clean
clean:
	@echo "Cleaning build artefacts..."
	find $(SRCDIR) -name '*.o' -delete
	rm -f $(TARGET)
	@echo "Done."

# =============================================================================
# Dependency tracking (auto-generated .d files)
# =============================================================================
DEPS := $(OBJS:.o=.d)
-include $(DEPS)
