#!/bin/bash
# Kale Rendering Engine - Initialization Script
# This script sets up the environment and runs basic tests
# Usage: ./init.sh

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Kale Rendering Engine - Initialization Script ===${NC}"
echo ""

# Project root directory
KALE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$KALE_ROOT/build"

echo "Project Root: $KALE_ROOT"
echo "Build Directory: $BUILD_DIR"
echo ""

# Step 1: Check for required dependencies
echo -e "${YELLOW}[1/5] Checking dependencies...${NC}"

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake not found. Please install CMake 3.16+${NC}"
    exit 1
fi
CMAKE_VERSION=$(cmake --version | grep -oP '\d+\.\d+' | head -1)
echo "✓ CMake version: $CMAKE_VERSION"

# Check for C++20 compiler
if command -v g++ &> /dev/null; then
    GCC_VERSION=$(g++ --version | grep -oP '\d+\.\d+' | head -1)
    echo "✓ GCC version: $GCC_VERSION"
elif command -v clang++ &> /dev/null; then
    CLANG_VERSION=$(clang++ --version | grep -oP '\d+\.\d+' | head -1)
    echo "✓ Clang version: $CLANG_VERSION"
else
    echo -e "${RED}Error: No C++ compiler found. Please install GCC or Clang${NC}"
    exit 1
fi

# Check for Vulkan
if command -v vkbc &> /dev/null || pkg-config --exists vulkan; then
    echo "✓ Vulkan SDK found"
else
    echo -e "${YELLOW}Warning: Vulkan SDK not found. Device layer features will be limited.${NC}"
fi

# Check for SDL3 (may need to be built from source)
if pkg-config --exists sdl3 || [ -f "$KALE_ROOT/third_party/SDL3/build/libSDL3.so" ]; then
    echo "✓ SDL3 found"
else
    echo -e "${YELLOW}Warning: SDL3 not found. Run scripts/setup_dependencies.sh to install dependencies.${NC}"
fi

# Check for executor library
if [ -n "$KALE_EXECUTOR_PATH" ] && [ -d "$KALE_EXECUTOR_PATH" ]; then
    echo "✓ Executor library path: $KALE_EXECUTOR_PATH"
else
    echo -e "${YELLOW}Warning: KALE_EXECUTOR_PATH not set. Set it with: export KALE_EXECUTOR_PATH=/path/to/executor${NC}"
fi

echo ""

# Step 2: Configure CMake if needed
echo -e "${YELLOW}[2/5] Configuring CMake...${NC}"

if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Build directory not configured. Running CMake configuration..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    CMAKE_ARGS="-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

    # Add executor path if set
    if [ -n "$KALE_EXECUTOR_PATH" ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DKALE_EXECUTOR_PATH=$KALE_EXECUTOR_PATH"
    fi

    # Enable vcpkg if KALE_USE_VCPKG is set
    if [ "$KALE_USE_VCPKG" = "ON" ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DKALE_USE_VCPKG=ON"
    fi

    cmake $CMAKE_ARGS "$KALE_ROOT"
    echo "✓ CMake configuration complete"
else
    echo "✓ Build directory already configured"
fi

echo ""

# Step 3: Build the project
echo -e "${YELLOW}[3/5] Building Kale...${NC}"
cd "$BUILD_DIR"
cmake --build . -j$(nproc)
echo "✓ Build complete"
echo ""

# Step 4: Run basic tests
echo -e "${YELLOW}[4/5] Running tests...${NC}"

# Check if tests are available
if [ -f "$BUILD_DIR/tests/kale_test" ] || [ -f "$BUILD_DIR/tests/kale_test.exe" ]; then
    ctest --output-on-failure
    echo "✓ Tests complete"
else
    echo -e "${YELLOW}No tests found. Skipping test execution.${NC}"
fi

echo ""

# Step 5: Run example application for basic verification
echo -e "${YELLOW}[5/5] Running example application...${NC}"

# Check if hello_kale is built
if [ -f "$BUILD_DIR/apps/hello_kale/hello_kale" ] || [ -f "$BUILD_DIR/apps/hello_kale/hello_kale.exe" ]; then
    echo "Starting hello_kale application..."
    echo "Expected: Window should open with Vulkan rendering"
    echo "Press ESC or close window to exit"
    echo ""

    # Run the application in background
    "$BUILD_DIR/apps/hello_kale/hello_kale" &
    APP_PID=$!

    # Wait a bit for the app to start
    sleep 2

    # Check if the app is still running
    if ps -p $APP_PID > /dev/null; then
        echo "✓ hello_kale is running (PID: $APP_PID)"
        echo ""
        echo "Note: The application is running in the background."
        echo "To stop it manually, run: kill $APP_PID"
        echo "Or wait for it to finish/close automatically."
    else
        echo -e "${YELLOW}Note: hello_kale exited quickly. This may be expected if it's a minimal demo.${NC}"
    fi
else
    echo -e "${YELLOW}hello_kale application not found. Skipping application test.${NC}"
fi

echo ""
echo -e "${GREEN}=== Initialization Complete ===${NC}"
echo ""
echo "Next Steps:"
echo "1. Review feature_list.json to see all project features"
echo "2. Review claude-progress.txt to see current development status"
echo "3. Start implementing features from Phase 0 or Phase 1"
echo ""
echo "Quick Commands:"
echo "  Build:   cd build && cmake --build . -j\$(nproc)"
echo "  Clean:   rm -rf build/*"
echo "  Rebuild: rm -rf build && ./init.sh"
echo ""
echo "For more information, see README.md and docs/design/"
