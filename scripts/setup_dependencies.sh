#!/usr/bin/env bash
# kale Dependency Setup Script (Linux/macOS)
# This script automatically downloads and configures all required third-party libraries

set -e

# Parse arguments
SKIP_STB=false
SKIP_TINYGLTF=false
SKIP_GLM=false
SKIP_SDL=false
SKIP_SDL_IMAGE=false
SKIP_SDL_TTF=false
SKIP_JSON=false
SKIP_ASSIMP=false
SKIP_MESH_OPTIMIZER=false
SKIP_BULLET3=false
SKIP_IMGUI=false
SKIP_VMA=false

for arg in "$@"; do
    case $arg in
        --SkipSTB) SKIP_STB=true ;;
        --SkipTinyGLTF) SKIP_TINYGLTF=true ;;
        --SkipGLM) SKIP_GLM=true ;;
        --SkipSDL) SKIP_SDL=true ;;
        --SkipSDLImage) SKIP_SDL_IMAGE=true ;;
        --SkipSDLTTF) SKIP_SDL_TTF=true ;;
        --SkipJSON) SKIP_JSON=true ;;
        --SkipAssimp) SKIP_ASSIMP=true ;;
        --SkipMeshOptimizer) SKIP_MESH_OPTIMIZER=true ;;
        --SkipBullet3) SKIP_BULLET3=true ;;
        --SkipImGui) SKIP_IMGUI=true ;;
        --SkipVMA) SKIP_VMA=true ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
GRAY='\033[0;90m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}kale Dependency Setup Script${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# Check required tools
echo -e "${YELLOW}Checking required tools...${NC}"
if ! command -v git &> /dev/null; then
    echo -e "${RED}  Error: Git not found, please install Git first${NC}"
    exit 1
fi
echo -e "${GREEN}  [OK] Git is installed${NC}"
echo ""

# Ensure third_party directory exists
if [[ ! -d "$THIRD_PARTY_DIR" ]]; then
    echo -e "${YELLOW}Creating third_party directory...${NC}"
    mkdir -p "$THIRD_PARTY_DIR"
fi

cd "$THIRD_PARTY_DIR"

# Helper: clone if needed
clone_if_needed() {
    local dir=$1
    local url=$2
    local branch=$3
    local desc=$4

    if [[ -d "$dir" ]]; then
        local count
        count=$(find "$dir" -maxdepth 1 -mindepth 1 2>/dev/null | wc -l)
        if [[ $count -eq 0 ]]; then
            echo -e "${YELLOW}$dir directory exists but is empty, removing and re-cloning...${NC}"
            rm -rf "$dir"
        else
            echo -e "${GREEN}$desc already exists and is complete, skipping clone${NC}"
            return 0
        fi
    fi

    echo -e "${YELLOW}Cloning $desc ($branch)...${NC}"
    git clone --depth 1 --branch "$branch" "$url" "$dir"
}

# 1. Clone stb（单头文件库，texture_loader 用于 PNG/JPG）
if [[ "$SKIP_STB" != "true" ]]; then
    clone_if_needed "stb" "https://github.com/nothings/stb.git" "master" "stb"
else
    echo -e "${GRAY}Skipping stb (using --SkipSTB)${NC}"
fi

# 2. Clone tinygltf（glTF 模型解析）
if [[ "$SKIP_TINYGLTF" != "true" ]]; then
    clone_if_needed "tinygltf" "https://github.com/syoyo/tinygltf.git" "v2.8.13" "tinygltf"
else
    echo -e "${GRAY}Skipping tinygltf (using --SkipTinyGLTF)${NC}"
fi

# 3. Clone GLM
if [[ "$SKIP_GLM" != "true" ]]; then
    clone_if_needed "glm" "https://github.com/g-truc/glm.git" "1.0.1" "glm"
else
    echo -e "${GRAY}Skipping GLM (using --SkipGLM)${NC}"
fi

# 4. Clone SDL3
if [[ "$SKIP_SDL" != "true" ]]; then
    clone_if_needed "SDL" "https://github.com/libsdl-org/SDL.git" "release-3.2.0" "SDL3"
else
    echo -e "${GRAY}Skipping SDL (using --SkipSDL)${NC}"
fi

# 5. Clone SDL_image
if [[ "$SKIP_SDL_IMAGE" != "true" ]]; then
    clone_if_needed "SDL_image" "https://github.com/libsdl-org/SDL_image.git" "release-3.4.0" "SDL_image"

    # Check and setup SDL_image external submodules
    SDL_IMAGE_EXTERNAL="$THIRD_PARTY_DIR/SDL_image/external"
    NEEDS_GIT_MODULES=false

    if [[ -d "$SDL_IMAGE_EXTERNAL" ]]; then
        for sub in jpeg libpng zlib; do
            subdir="$SDL_IMAGE_EXTERNAL/$sub"
            if [[ ! -d "$subdir" ]]; then
                echo -e "${YELLOW}  Missing submodule: $sub${NC}"
                NEEDS_GIT_MODULES=true
            elif [[ -z "$(ls -A "$subdir" 2>/dev/null)" ]]; then
                echo -e "${YELLOW}  Empty submodule: $sub${NC}"
                NEEDS_GIT_MODULES=true
            fi
        done
        [[ "$NEEDS_GIT_MODULES" != "true" ]] && echo -e "${GREEN}  All external submodules are present and non-empty${NC}"
    else
        echo -e "${YELLOW}  External directory does not exist, need to run Get-GitModules.ps1${NC}"
        NEEDS_GIT_MODULES=true
    fi

    if [[ "$NEEDS_GIT_MODULES" == "true" ]]; then
        GET_MODULES="$SDL_IMAGE_EXTERNAL/Get-GitModules.ps1"
        if [[ -f "$GET_MODULES" ]]; then
            echo -e "${YELLOW}Running SDL_image's Get-GitModules.ps1...${NC}"
            (cd "$SDL_IMAGE_EXTERNAL" && pwsh -ExecutionPolicy Bypass -File "./Get-GitModules.ps1" 2>/dev/null) || \
            (cd "$SDL_IMAGE_EXTERNAL" && powershell -ExecutionPolicy Bypass -File "./Get-GitModules.ps1" 2>/dev/null) || \
            echo -e "${YELLOW}  Warning: Install PowerShell (pwsh) to fetch SDL_image external deps, or run manually: cd $SDL_IMAGE_EXTERNAL && pwsh ./Get-GitModules.ps1${NC}"
        else
            echo -e "${YELLOW}  Warning: Get-GitModules.ps1 script not found${NC}"
        fi
    fi
else
    echo -e "${GRAY}Skipping SDL_image (using --SkipSDLImage)${NC}"
fi

# 6. Clone SDL_ttf
if [[ "$SKIP_SDL_TTF" != "true" ]]; then
    clone_if_needed "SDL_ttf" "https://github.com/libsdl-org/SDL_ttf.git" "release-3.2.2" "SDL_ttf"

    SDL_TTF_EXTERNAL="$THIRD_PARTY_DIR/SDL_ttf/external"
    SDL_TTF_CMAKE="$THIRD_PARTY_DIR/SDL_ttf/cmake"
    NEEDS_GIT_MODULES=false
    NEEDS_CMAKE_FILES=false

    if [[ -d "$SDL_TTF_EXTERNAL" ]]; then
        for sub in freetype harfbuzz plutosvg plutovg; do
            subdir="$SDL_TTF_EXTERNAL/$sub"
            if [[ ! -d "$subdir" ]]; then
                echo -e "${YELLOW}  Missing submodule: $sub${NC}"
                NEEDS_GIT_MODULES=true
            elif [[ -z "$(ls -A "$subdir" 2>/dev/null)" ]]; then
                echo -e "${YELLOW}  Empty submodule: $sub${NC}"
                NEEDS_GIT_MODULES=true
            fi
        done
        [[ "$NEEDS_GIT_MODULES" != "true" ]] && echo -e "${GREEN}  All external submodules are present and non-empty${NC}"
    else
        echo -e "${YELLOW}  External directory does not exist${NC}"
        NEEDS_GIT_MODULES=true
    fi

    REQUIRED_CMAKE_FILES=("GetGitRevisionDescription.cmake" "PkgConfigHelper.cmake" "PrivateSdlFunctions.cmake" "sdlcpu.cmake" "sdlplatform.cmake" "sdlmanpages.cmake")
    if [[ ! -d "$SDL_TTF_CMAKE" ]]; then
        NEEDS_CMAKE_FILES=true
    else
        for f in "${REQUIRED_CMAKE_FILES[@]}"; do
            [[ ! -f "$SDL_TTF_CMAKE/$f" ]] && NEEDS_CMAKE_FILES=true
        done
    fi

    if [[ "$NEEDS_GIT_MODULES" == "true" ]]; then
        GET_MODULES="$SDL_TTF_EXTERNAL/Get-GitModules.ps1"
        if [[ -f "$GET_MODULES" ]]; then
            echo -e "${YELLOW}Running SDL_ttf's Get-GitModules.ps1...${NC}"
            (cd "$SDL_TTF_EXTERNAL" && pwsh -ExecutionPolicy Bypass -File "./Get-GitModules.ps1" 2>/dev/null) || \
            (cd "$SDL_TTF_EXTERNAL" && powershell -ExecutionPolicy Bypass -File "./Get-GitModules.ps1" 2>/dev/null) || \
            echo -e "${YELLOW}  Warning: Install PowerShell (pwsh) to fetch SDL_ttf external deps${NC}"
        fi
    fi

    if [[ "$NEEDS_CMAKE_FILES" == "true" ]]; then
        echo -e "${YELLOW}Copying required cmake files for SDL_ttf...${NC}"
        SDL_CMAKE="$THIRD_PARTY_DIR/SDL/cmake"
        SDL_IMAGE_CMAKE="$THIRD_PARTY_DIR/SDL_image/cmake"

        if [[ ! -d "$SDL_CMAKE" ]]; then
            echo -e "${RED}Error: SDL cmake directory does not exist${NC}"
            exit 1
        fi

        mkdir -p "$SDL_TTF_CMAKE"
        for f in GetGitRevisionDescription.cmake PkgConfigHelper.cmake sdlcpu.cmake sdlplatform.cmake sdlmanpages.cmake; do
            [[ -f "$SDL_CMAKE/$f" ]] && cp "$SDL_CMAKE/$f" "$SDL_TTF_CMAKE/" && echo -e "  Copied: $f"
        done
        [[ -f "$SDL_IMAGE_CMAKE/PrivateSdlFunctions.cmake" ]] && cp "$SDL_IMAGE_CMAKE/PrivateSdlFunctions.cmake" "$SDL_TTF_CMAKE/"
    fi

    [[ "$NEEDS_GIT_MODULES" != "true" && "$NEEDS_CMAKE_FILES" != "true" ]] && echo -e "${GREEN}SDL_ttf is complete and ready${NC}"
else
    echo -e "${GRAY}Skipping SDL_ttf (using --SkipSDLTTF)${NC}"
fi

# 7. Clone nlohmann/json
if [[ "$SKIP_JSON" != "true" ]]; then
    clone_if_needed "json" "https://github.com/nlohmann/json.git" "v3.11.3" "nlohmann/json"
else
    echo -e "${GRAY}Skipping nlohmann/json (using --SkipJSON)${NC}"
fi

# 8. Clone Assimp
if [[ "$SKIP_ASSIMP" != "true" ]]; then
    clone_if_needed "assimp" "https://github.com/assimp/assimp.git" "v5.4.3" "Assimp"
else
    echo -e "${GRAY}Skipping Assimp (using --SkipAssimp)${NC}"
fi

# 9. Clone meshoptimizer
if [[ "$SKIP_MESH_OPTIMIZER" != "true" ]]; then
    clone_if_needed "meshoptimizer" "https://github.com/zeux/meshoptimizer.git" "v0.20" "meshoptimizer"
else
    echo -e "${GRAY}Skipping meshoptimizer (using --SkipMeshOptimizer)${NC}"
fi

# 10. Clone bullet3
if [[ "$SKIP_BULLET3" != "true" ]]; then
    clone_if_needed "bullet3" "https://github.com/bulletphysics/bullet3.git" "3.25" "bullet3"
else
    echo -e "${GRAY}Skipping bullet3 (using --SkipBullet3)${NC}"
fi

# 11. Clone ImGui (docking branch)
if [[ "$SKIP_IMGUI" != "true" ]]; then
    NEEDS_CLONE=false
    if [[ -d "imgui" ]]; then
        count=$(find imgui -maxdepth 1 -mindepth 1 2>/dev/null | wc -l)
        if [[ $count -eq 0 ]]; then
            rm -rf imgui
            NEEDS_CLONE=true
        else
            current_branch=$(cd imgui && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
            if [[ "$current_branch" == "docking" ]]; then
                echo -e "${GREEN}ImGui already exists on docking branch${NC}"
            else
                echo -e "${YELLOW}Switching ImGui to docking branch...${NC}"
                (cd imgui && git fetch origin docking && git checkout docking && git pull origin docking)
                echo -e "${GREEN}ImGui switched to docking branch successfully${NC}"
            fi
        fi
    else
        NEEDS_CLONE=true
    fi

    if [[ "$NEEDS_CLONE" == "true" ]]; then
        echo -e "${YELLOW}Cloning ImGui (docking branch)...${NC}"
        git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git
        echo -e "${GREEN}ImGui cloned (docking branch) successfully${NC}"
    fi
else
    echo -e "${GRAY}Skipping ImGui (using --SkipImGui)${NC}"
fi

# 12. Clone VulkanMemoryAllocator (VMA)
if [[ "$SKIP_VMA" != "true" ]]; then
    clone_if_needed "VulkanMemoryAllocator" "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" "v3.0.1" "VulkanMemoryAllocator (VMA)"
else
    echo -e "${GRAY}Skipping VulkanMemoryAllocator (using --SkipVMA)${NC}"
fi

cd - > /dev/null

echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${GREEN}Dependency setup completed!${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Run cmake to configure the project"
echo "  2. Build the project"
echo ""
echo -e "${YELLOW}Example commands:${NC}"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  cmake --build . --config Release"
echo ""
