# ============================================================================
# Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
# Makefile: Builds the unified Sodium binary with or without the GUI
#
# Usage:
#   make           → Builds everything (GUI + CLI + Server daemon)
#   make server    → Builds headless CLI + embedded web server only (no GUI)
#   make clean     → Removes the built binary and object files
#   make deps      → Installs GLFW via Homebrew & clones Dear ImGui
#
# After building:
#   ./sodium --gui            → Launch native C++ Dear ImGui Desktop UI
#   ./sodium --serve          → Launch embedded HTTP web server on port 8080
#   ./sodium --file f.cnf     → Solve a DIMACS file from the terminal
# ============================================================================

# ---- Output Binary Name ----
TARGET      = sodium

# ---- Compiler & Standard ----
CXX         = clang++
CXXFLAGS    = -std=c++17 -O2 -Wall -Wextra -pthread

# ---- Include Paths ----
INCLUDES    = \
    -Isrc \
    -Isrc/SAT \
    -Isrc/SAT/CDCL \
    -Isrc/NLP \
    -Isrc/InterfaceAndUI \
    -Ithird_party/imgui \
    -Ithird_party/imgui/backends

# ---- macOS Frameworks (OpenGL + Window System) ----
FRAMEWORKS  = \
    -framework OpenGL \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreVideo

# ---- Core C Engine Sources (shared between all targets) ----
CORE_SRCS   = \
    src/sodium_orchestrator.c \
    src/API.c \
    src/SAT/sat_api.c \
    src/SAT/CDCL/CDCL.c \
    src/SAT/CDCL/Clauses/clause_array.c \
    src/SAT/CDCL/VSIDS.c \
    src/SAT/CDCL/Memory/memory.c \
    src/NLP/nlp_api.c

# ---- GUI-Specific Sources (Dear ImGui + GLFW backend + C++ UI) ----
GUI_SRCS    = \
    src/InterfaceAndUI/userInterface.cpp \
    third_party/imgui/imgui.cpp \
    third_party/imgui/imgui_draw.cpp \
    third_party/imgui/imgui_tables.cpp \
    third_party/imgui/imgui_widgets.cpp \
    third_party/imgui/backends/imgui_impl_glfw.cpp \
    third_party/imgui/backends/imgui_impl_opengl3.cpp

# ============================================================================
# TARGET: make  →  Full build (CLI + GUI + Web Server)
# ============================================================================
all: check_deps
	@echo ""
	@echo "========================================================"
	@echo " Building Sodium with Native Desktop GUI..."
	@echo "========================================================"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DSODIUM_ENABLE_GUI \
		$(CORE_SRCS) $(GUI_SRCS) \
		-lglfw $(FRAMEWORKS) \
		-o $(TARGET)
	@echo ""
	@echo "✓ Build complete → ./$(TARGET)"
	@echo ""
	@echo "  Run the Desktop UI:    ./$(TARGET) --gui"
	@echo "  Run the Web Server:    ./$(TARGET) --serve --port 8080"
	@echo "  Solve a DIMACS file:   ./$(TARGET) --file problem.cnf"
	@echo ""

# ============================================================================
# TARGET: make server  →  Headless build (no GUI, just CLI + Web Server)
# Useful if ImGui / GLFW are not installed yet.
# ============================================================================
server:
	@echo ""
	@echo "========================================================"
	@echo " Building Sodium (Headless CLI + Web Server)..."
	@echo "========================================================"
	$(CXX) $(CXXFLAGS) \
		-Isrc -Isrc/SAT -Isrc/SAT/CDCL -Isrc/NLP \
		$(CORE_SRCS) \
		-o $(TARGET)
	@echo ""
	@echo "✓ Build complete → ./$(TARGET)"
	@echo ""
	@echo "  Run the Web Server:    ./$(TARGET) --serve --port 8080"
	@echo "  Solve a DIMACS file:   ./$(TARGET) --file problem.cnf"
	@echo ""

# ============================================================================
# TARGET: make deps  →  Installs GLFW and clones Dear ImGui automatically
# ============================================================================
deps:
	@echo "Installing GLFW via Homebrew..."
	brew install glfw
	@echo "Cloning Dear ImGui into third_party/imgui..."
	mkdir -p third_party
	git clone https://github.com/ocornut/imgui.git --depth=1 third_party/imgui
	@echo ""
	@echo "✓ Dependencies ready. Now run:  make"
	@echo ""

# ============================================================================
# TARGET: make clean  →  Remove built binary
# ============================================================================
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET)
	@echo "✓ Clean complete."

# ============================================================================
# Internal: Checks that required dependencies are present before building
# ============================================================================
check_deps:
	@if [ ! -f "third_party/imgui/imgui.h" ]; then \
		echo ""; \
		echo "✗ Error: Dear ImGui not found at third_party/imgui/"; \
		echo "  Run:  make deps"; \
		echo ""; \
		exit 1; \
	fi
	@if ! pkg-config --exists glfw3 2>/dev/null && ! brew list glfw >/dev/null 2>&1; then \
		echo ""; \
		echo "✗ Error: GLFW not found."; \
		echo "  Run:  make deps"; \
		echo ""; \
		exit 1; \
	fi
	@echo "✓ Dependencies found."

.PHONY: all server deps clean check_deps
