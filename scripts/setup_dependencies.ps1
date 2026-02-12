# kale Dependency Setup Script
# This script automatically downloads and configures all required third-party libraries

param(
    [switch]$SkipGLM = $false,
    [switch]$SkipSDL = $false,
    [switch]$SkipSDLImage = $false,
    [switch]$SkipSDLTTF = $false,
    [switch]$SkipJSON = $false,
    [switch]$SkipAssimp = $false,
    [switch]$SkipMeshOptimizer = $false,
    [switch]$SkipBullet3 = $false,
    [switch]$SkipImGui = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ThirdPartyDir = Join-Path $ProjectRoot "third_party"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "kale Dependency Setup Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check required tools
Write-Host "Checking required tools..." -ForegroundColor Yellow
$toolsOk = $true

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "  Error: Git not found, please install Git first" -ForegroundColor Red
    $toolsOk = $false
} else {
    Write-Host "  [OK] Git is installed" -ForegroundColor Green
}

if (-not $toolsOk) {
    Write-Host ""
    Write-Host "Please install missing tools before running this script" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Ensure third_party directory exists
if (-not (Test-Path $ThirdPartyDir)) {
    Write-Host "Creating third_party directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $ThirdPartyDir | Out-Null
}

Push-Location $ThirdPartyDir

# 1. Clone GLM
if (-not $SkipGLM) {
    $GLMDir = "glm"
    $needsClone = $false

    if (Test-Path $GLMDir) {
        $glmItems = Get-ChildItem -Path $GLMDir -ErrorAction SilentlyContinue
        if ($null -eq $glmItems -or $glmItems.Count -eq 0) {
            Write-Host "glm directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $GLMDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "glm already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning GLM (v1.0.1)..." -ForegroundColor Yellow
        git clone --depth 1 --branch 1.0.1 https://github.com/g-truc/glm.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: GLM clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping GLM (using --SkipGLM)" -ForegroundColor Gray
}

# 2. Clone SDL3
if (-not $SkipSDL) {
    $SDLDir = "SDL"
    $needsClone = $false

    if (Test-Path $SDLDir) {
        $sdlItems = Get-ChildItem -Path $SDLDir -ErrorAction SilentlyContinue
        if ($null -eq $sdlItems -or $sdlItems.Count -eq 0) {
            Write-Host "SDL directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $SDLDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "SDL already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning SDL3 (release-3.2.0)..." -ForegroundColor Yellow
        git clone --depth 1 --branch release-3.2.0 https://github.com/libsdl-org/SDL.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: SDL clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping SDL (using --SkipSDL)" -ForegroundColor Gray
}

# 3. Clone SDL_image
if (-not $SkipSDLImage) {
    $SDLImageDir = "SDL_image"
    $needsClone = $false

    if (Test-Path $SDLImageDir) {
        $sdlImageItems = Get-ChildItem -Path $SDLImageDir -ErrorAction SilentlyContinue
        if ($null -eq $sdlImageItems -or $sdlImageItems.Count -eq 0) {
            Write-Host "SDL_image directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $SDLImageDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "SDL_image already exists, checking completeness..." -ForegroundColor Yellow
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning SDL_image (release-3.4.0)..." -ForegroundColor Yellow
        git clone --depth 1 --branch release-3.4.0 https://github.com/libsdl-org/SDL_image.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: SDL_image clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }

    # Check and setup SDL_image external submodules
    $SDLImageExternalDir = Join-Path $SDLImageDir "external"
    $GetGitModulesScript = Join-Path $SDLImageExternalDir "Get-GitModules.ps1"
    $needsGitModules = $false

    if (Test-Path $SDLImageExternalDir) {
        $requiredSubmodules = @("jpeg", "libpng", "zlib")
        $missingSubmodules = @()
        $emptySubmodules = @()

        foreach ($submodule in $requiredSubmodules) {
            $submoduleDir = Join-Path $SDLImageExternalDir $submodule
            if (-not (Test-Path $submoduleDir)) {
                $missingSubmodules += $submodule
            } else {
                $submoduleItems = Get-ChildItem -Path $submoduleDir -ErrorAction SilentlyContinue
                if ($null -eq $submoduleItems -or $submoduleItems.Count -eq 0) {
                    $emptySubmodules += $submodule
                }
            }
        }

        if ($missingSubmodules.Count -gt 0) {
            Write-Host "  Missing submodules: $($missingSubmodules -join ', '), need to run Get-GitModules.ps1" -ForegroundColor Yellow
            $needsGitModules = $true
        }

        if ($emptySubmodules.Count -gt 0) {
            Write-Host "  Empty submodules: $($emptySubmodules -join ', '), need to run Get-GitModules.ps1" -ForegroundColor Yellow
            $needsGitModules = $true
        }

        if (-not $needsGitModules) {
            Write-Host "  All external submodules are present and non-empty" -ForegroundColor Green
        }
    } else {
        Write-Host "  External directory does not exist, need to run Get-GitModules.ps1" -ForegroundColor Yellow
        $needsGitModules = $true
    }

    if ($needsGitModules) {
        if (Test-Path $GetGitModulesScript) {
            Write-Host "Running SDL_image's Get-GitModules.ps1..." -ForegroundColor Yellow
            Push-Location $SDLImageExternalDir
            PowerShell -ExecutionPolicy Bypass -File ".\Get-GitModules.ps1"
            Pop-Location
        } else {
            Write-Host "  Warning: Get-GitModules.ps1 script not found" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "Skipping SDL_image (using --SkipSDLImage)" -ForegroundColor Gray
}

# 4. Clone SDL_ttf
if (-not $SkipSDLTTF) {
    $SDLTTFDir = "SDL_ttf"
    $needsClone = $false

    if (Test-Path $SDLTTFDir) {
        $sdlTtfItems = Get-ChildItem -Path $SDLTTFDir -ErrorAction SilentlyContinue
        if ($null -eq $sdlTtfItems -or $sdlTtfItems.Count -eq 0) {
            Write-Host "SDL_ttf directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $SDLTTFDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "SDL_ttf already exists, checking completeness..." -ForegroundColor Yellow
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning SDL_ttf (release-3.2.2)..." -ForegroundColor Yellow
        git clone --depth 1 --branch release-3.2.2 https://github.com/libsdl-org/SDL_ttf.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: SDL_ttf clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }

    # Check and setup SDL_ttf external submodules
    $SDLTTFExternalDir = Join-Path $SDLTTFDir "external"
    $GetGitModulesScript = Join-Path $SDLTTFExternalDir "Get-GitModules.ps1"
    $SDLTTFCMakeDir = Join-Path $SDLTTFDir "cmake"
    $needsGitModules = $false

    if (Test-Path $SDLTTFExternalDir) {
        $requiredSubmodules = @("freetype", "harfbuzz", "plutosvg", "plutovg")
        $missingSubmodules = @()
        $emptySubmodules = @()

        foreach ($submodule in $requiredSubmodules) {
            $submoduleDir = Join-Path $SDLTTFExternalDir $submodule
            if (-not (Test-Path $submoduleDir)) {
                $missingSubmodules += $submodule
            } else {
                $submoduleItems = Get-ChildItem -Path $submoduleDir -ErrorAction SilentlyContinue
                if ($null -eq $submoduleItems -or $submoduleItems.Count -eq 0) {
                    $emptySubmodules += $submodule
                }
            }
        }

        if ($missingSubmodules.Count -gt 0) {
            Write-Host "  Missing submodules: $($missingSubmodules -join ', '), need to run Get-GitModules.ps1" -ForegroundColor Yellow
            $needsGitModules = $true
        }

        if ($emptySubmodules.Count -gt 0) {
            Write-Host "  Empty submodules: $($emptySubmodules -join ', '), need to run Get-GitModules.ps1" -ForegroundColor Yellow
            $needsGitModules = $true
        }

        if (-not $needsGitModules) {
            Write-Host "  All external submodules are present and non-empty" -ForegroundColor Green
        }
    } else {
        Write-Host "  External directory does not exist, need to run Get-GitModules.ps1" -ForegroundColor Yellow
        $needsGitModules = $true
    }

    $requiredCMakeFiles = @(
        "GetGitRevisionDescription.cmake",
        "PkgConfigHelper.cmake",
        "PrivateSdlFunctions.cmake",
        "sdlcpu.cmake",
        "sdlplatform.cmake",
        "sdlmanpages.cmake"
    )

    $needsCMakeFiles = $false
    if (-not (Test-Path $SDLTTFCMakeDir)) {
        Write-Host "  CMake directory does not exist, need to copy cmake files" -ForegroundColor Yellow
        $needsCMakeFiles = $true
    } else {
        $missingFiles = @()
        foreach ($cmakeFile in $requiredCMakeFiles) {
            $cmakeFilePath = Join-Path $SDLTTFCMakeDir $cmakeFile
            if (-not (Test-Path $cmakeFilePath)) {
                $missingFiles += $cmakeFile
            }
        }
        if ($missingFiles.Count -gt 0) {
            Write-Host "  Missing cmake files: $($missingFiles -join ', '), need to copy" -ForegroundColor Yellow
            $needsCMakeFiles = $true
        } else {
            Write-Host "  All required cmake files are present" -ForegroundColor Green
        }
    }

    if ($needsGitModules) {
        if (Test-Path $GetGitModulesScript) {
            Write-Host "Running SDL_ttf's Get-GitModules.ps1..." -ForegroundColor Yellow
            Push-Location $SDLTTFExternalDir
            PowerShell -ExecutionPolicy Bypass -File ".\Get-GitModules.ps1"
            Pop-Location
        } else {
            Write-Host "  Warning: Get-GitModules.ps1 script not found" -ForegroundColor Yellow
        }
    }

    if ($needsCMakeFiles) {
        Write-Host "Copying required cmake files for SDL_ttf..." -ForegroundColor Yellow
        $SDLCMakeDir = Join-Path "SDL" "cmake"
        $SDLImageCMakeDir = Join-Path "SDL_image" "cmake"

        if (-not (Test-Path $SDLCMakeDir)) {
            Write-Host "Error: SDL cmake directory does not exist" -ForegroundColor Red
            Pop-Location
            exit 1
        }

        if (-not (Test-Path $SDLTTFCMakeDir)) {
            New-Item -ItemType Directory -Path $SDLTTFCMakeDir | Out-Null
        }

        $CMakeFiles = @(
            @{Source = "GetGitRevisionDescription.cmake"; Dest = "GetGitRevisionDescription.cmake"},
            @{Source = "PkgConfigHelper.cmake"; Dest = "PkgConfigHelper.cmake"},
            @{Source = "sdlcpu.cmake"; Dest = "sdlcpu.cmake"},
            @{Source = "sdlplatform.cmake"; Dest = "sdlplatform.cmake"},
            @{Source = "sdlmanpages.cmake"; Dest = "sdlmanpages.cmake"}
        )

        foreach ($file in $CMakeFiles) {
            $sourcePath = Join-Path $SDLCMakeDir $file.Source
            $destPath = Join-Path $SDLTTFCMakeDir $file.Dest
            if (Test-Path $sourcePath) {
                Copy-Item -Path $sourcePath -Destination $destPath -Force
                Write-Host "  Copied: $($file.Dest)" -ForegroundColor Gray
            } else {
                Write-Host "  Warning: Source file does not exist: $sourcePath" -ForegroundColor Yellow
            }
        }

        $privateFunctionsSource = Join-Path $SDLImageCMakeDir "PrivateSdlFunctions.cmake"
        $privateFunctionsDest = Join-Path $SDLTTFCMakeDir "PrivateSdlFunctions.cmake"
        if (Test-Path $privateFunctionsSource) {
            Copy-Item -Path $privateFunctionsSource -Destination $privateFunctionsDest -Force
            Write-Host "  Copied: PrivateSdlFunctions.cmake" -ForegroundColor Gray
        } else {
            Write-Host "  Warning: PrivateSdlFunctions.cmake does not exist" -ForegroundColor Yellow
        }
    }

    if (-not $needsGitModules -and -not $needsCMakeFiles) {
        Write-Host "SDL_ttf is complete and ready" -ForegroundColor Green
    }
} else {
    Write-Host "Skipping SDL_ttf (using --SkipSDLTTF)" -ForegroundColor Gray
}

# 5. Clone nlohmann/json
if (-not $SkipJSON) {
    $JSONDir = "json"
    $needsClone = $false

    if (Test-Path $JSONDir) {
        $jsonItems = Get-ChildItem -Path $JSONDir -ErrorAction SilentlyContinue
        if ($null -eq $jsonItems -or $jsonItems.Count -eq 0) {
            Write-Host "nlohmann/json directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $JSONDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "nlohmann/json already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning nlohmann/json (v3.11.3)..." -ForegroundColor Yellow
        git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: nlohmann/json clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping nlohmann/json (using --SkipJSON)" -ForegroundColor Gray
}

# 6. Clone Assimp
if (-not $SkipAssimp) {
    $AssimpDir = "assimp"
    $needsClone = $false

    if (Test-Path $AssimpDir) {
        $assimpItems = Get-ChildItem -Path $AssimpDir -ErrorAction SilentlyContinue
        if ($null -eq $assimpItems -or $assimpItems.Count -eq 0) {
            Write-Host "Assimp directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $AssimpDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "Assimp already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning Assimp (v5.4.3)..." -ForegroundColor Yellow
        git clone --depth 1 --branch v5.4.3 https://github.com/assimp/assimp.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: Assimp clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping Assimp (using --SkipAssimp)" -ForegroundColor Gray
}

# 7. Clone meshoptimizer
if (-not $SkipMeshOptimizer) {
    $MeshOptimizerDir = "meshoptimizer"
    $needsClone = $false

    if (Test-Path $MeshOptimizerDir) {
        $meshOptimizerItems = Get-ChildItem -Path $MeshOptimizerDir -ErrorAction SilentlyContinue
        if ($null -eq $meshOptimizerItems -or $meshOptimizerItems.Count -eq 0) {
            Write-Host "meshoptimizer directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $MeshOptimizerDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "meshoptimizer already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning meshoptimizer (v0.20)..." -ForegroundColor Yellow
        git clone --depth 1 --branch v0.20 https://github.com/zeux/meshoptimizer.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: meshoptimizer clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping meshoptimizer (using --SkipMeshOptimizer)" -ForegroundColor Gray
}

# 8. Clone bullet3
if (-not $SkipBullet3) {
    $Bullet3Dir = "bullet3"
    $needsClone = $false

    if (Test-Path $Bullet3Dir) {
        $bullet3Items = Get-ChildItem -Path $Bullet3Dir -ErrorAction SilentlyContinue
        if ($null -eq $bullet3Items -or $bullet3Items.Count -eq 0) {
            Write-Host "bullet3 directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $Bullet3Dir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "bullet3 already exists and is complete, skipping clone" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning bullet3 (3.25)..." -ForegroundColor Yellow
        git clone --depth 1 --branch 3.25 https://github.com/bulletphysics/bullet3.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: bullet3 clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
} else {
    Write-Host "Skipping bullet3 (using --SkipBullet3)" -ForegroundColor Gray
}

# 9. Clone ImGui and switch to docking branch
if (-not $SkipImGui) {
    $ImGuiDir = "imgui"
    $needsClone = $false
    $needsBranchSwitch = $false

    if (Test-Path $ImGuiDir) {
        $imguiItems = Get-ChildItem -Path $ImGuiDir -ErrorAction SilentlyContinue
        if ($null -eq $imguiItems -or $imguiItems.Count -eq 0) {
            Write-Host "ImGui directory exists but is empty, removing and re-cloning..." -ForegroundColor Yellow
            Remove-Item -Path $ImGuiDir -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "ImGui already exists, checking branch..." -ForegroundColor Yellow
            Push-Location $ImGuiDir
            $currentBranch = git rev-parse --abbrev-ref HEAD 2>$null
            if ($LASTEXITCODE -eq 0) {
                if ($currentBranch -ne "docking") {
                    Write-Host "  Current branch is '$currentBranch', need to switch to 'docking'" -ForegroundColor Yellow
                    $needsBranchSwitch = $true
                } else {
                    Write-Host "  Already on 'docking' branch" -ForegroundColor Green
                }
            } else {
                Write-Host "  Warning: Could not determine current branch, will attempt to switch" -ForegroundColor Yellow
                $needsBranchSwitch = $true
            }
            Pop-Location
        }
    } else {
        $needsClone = $true
    }

    if ($needsClone) {
        Write-Host "Cloning ImGui (docking branch)..." -ForegroundColor Yellow
        git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: ImGui clone failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }
        Write-Host "ImGui cloned (docking branch) successfully" -ForegroundColor Green
    } elseif ($needsBranchSwitch) {
        Write-Host "Switching ImGui to 'docking' branch..." -ForegroundColor Yellow
        Push-Location $ImGuiDir
        $oldErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "SilentlyContinue"
        git fetch origin docking *>$null
        $ErrorActionPreference = $oldErrorAction
        git checkout docking
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Error: Failed to switch to 'docking' branch" -ForegroundColor Red
            Pop-Location
            Pop-Location
            exit 1
        }
        git pull origin docking
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Warning: Failed to pull latest changes from 'docking' branch" -ForegroundColor Yellow
        } else {
            Write-Host "ImGui switched to 'docking' branch successfully" -ForegroundColor Green
        }
        Pop-Location
    }
} else {
    Write-Host "Skipping ImGui (using --SkipImGui)" -ForegroundColor Gray
}

Pop-Location

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Dependency setup completed!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Run cmake to configure the project" -ForegroundColor White
Write-Host "  2. Build the project" -ForegroundColor White
Write-Host ""
Write-Host "Example commands:" -ForegroundColor Yellow
Write-Host "  mkdir build; cd build" -ForegroundColor White
Write-Host "  cmake .." -ForegroundColor White
Write-Host "  cmake --build . --config Release" -ForegroundColor White
Write-Host ""
