param(
    [switch]$HostOnly,
    [ValidateSet(0, 1, 2, 3)]
    [int]$PtxasOptLevel = 3,
    [switch]$EnableExpensiveOptimizations,
    [switch]$DisableExpensiveOptimizations,
    [switch]$PtxasVerbose,
    [switch]$ExperimentalMultiIsland,
    [switch]$ProductionOnly,
    [ValidateRange(0, 255)]
    [int]$MaxRegisterCount = 0,
    [ValidatePattern('^$|^\d{2}$')]
    [string]$CudaArch = $env:OPTILAND_CUDA_ARCH,
    [string]$OutputName = "optiland_de_cuda.exe",
    [string]$PythonExecutable = $env:LENSDE_PYTHON
)

$ErrorActionPreference = "Stop"

$TargetDescription = if ($ExperimentalMultiIsland) {
    "multi-island CUDA-DE broker (optiland_multi_island_experiment.exe)"
} else {
    "production CUDA-DE scorer ($OutputName)"
}
Write-Host "CUDA-DE build target: $TargetDescription"

if ($ExperimentalMultiIsland -and $OutputName -eq "optiland_de_cuda.exe") {
    $OutputName = "optiland_multi_island_experiment.exe"
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Build = Join-Path $Root "build"
New-Item -ItemType Directory -Force -Path $Build | Out-Null

$Nvcc = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.2\bin\nvcc.exe"
if (!(Test-Path $Nvcc)) {
    $Nvcc = "nvcc"
}

$ArchFlags = @()
$ArchName = "default"
if ($CudaArch) {
    $Arch = [Math]::Min([int]$CudaArch, 86)
    $ArchName = "$Arch"
    $ArchFlags += "-gencode"
    $ArchFlags += "arch=compute_$ArchName,code=sm_$ArchName"
    Write-Host "CUDA target architecture: sm_$ArchName native-only (explicit override)"
} else {
try {
    $ComputeCap = (& nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1)
    if ($ComputeCap) {
        $ComputeCap = $ComputeCap.Trim()
        if ($ComputeCap -match '^(\d+)\.(\d+)') {
            $Major = [int]$Matches[1]
            $Minor = [int]$Matches[2]
            $Arch = [Math]::Min(($Major * 10) + $Minor, 86)
            $ArchName = "$Arch"
            $ArchFlags += "-gencode"
            $ArchFlags += "arch=compute_$ArchName,code=sm_$ArchName"
            Write-Host "CUDA target architecture: sm_$ArchName native-only (detected compute capability $ComputeCap)"
        }
    }
} catch {
    Write-Warning "Could not query CUDA compute capability from nvidia-smi."
}
}

if ($ArchFlags.Count -eq 0) {
    throw (
        "CUDA target architecture could not be detected. " +
        "Pass -CudaArch explicitly (for example -CudaArch 86 for RTX 30-series) " +
        "or set OPTILAND_CUDA_ARCH. Refusing nvcc's silent sm_52 default."
    )
}

$CompileDefinitions = @(
    "-DOPTILAND_WITH_CUDA",
    "-DOPTILAND_DE_CUDA_ONLY",
    "-DOPTILAND_ASPHERE_UC10_RELEASE"
)
$IncludeFlags = @("-I", (Join-Path $Root "include"))
$Project = (Resolve-Path (Join-Path $Root "..")).Path
if (!$PythonExecutable) {
    $ProjectPython = Join-Path $Project ".venv/Scripts/python.exe"
    $PythonExecutable = if (Test-Path -LiteralPath $ProjectPython) { $ProjectPython } else { "python" }
}
$BuildTarget = if ($ExperimentalMultiIsland) { "broker" } else { "cuda" }
$RegistryReader = Join-Path $Project "scripts/native_sources.py"
$SourcePlanText = & $PythonExecutable $RegistryReader --native-root $Root --target $BuildTarget
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$SourcePlan = ($SourcePlanText | Out-String) | ConvertFrom-Json
$HostSources = @($SourcePlan.host_sources)
$CudaSources = @($SourcePlan.device_sources)
$UseExpensiveOptimizations = if ($DisableExpensiveOptimizations) {
    $false
} elseif ($EnableExpensiveOptimizations) {
    $true
} else {
    $PtxasOptLevel -eq 0 -or $PtxasOptLevel -eq 3
}
# CUDA 11.2 asks vcvars64.bat to initialize MSVC for every nvcc process. Some
# otherwise usable VS2017 installations return a failure when an unrelated
# optional component (for example TypeScript) is absent. Import the environment
# that vcvars produced anyway, then tell nvcc to reuse it instead of rerunning
# the failing batch file.
$env:SystemRoot = if ($env:SystemRoot) { $env:SystemRoot } else { "C:\Windows" }
$env:windir = if ($env:windir) { $env:windir } else { $env:SystemRoot }
$env:SystemDrive = if ($env:SystemDrive) { $env:SystemDrive } else { "C:" }
$env:ComSpec = if ($env:ComSpec) { $env:ComSpec } else { Join-Path $env:SystemRoot "System32\cmd.exe" }
$env:PROCESSOR_ARCHITECTURE = if ($env:PROCESSOR_ARCHITECTURE) { $env:PROCESSOR_ARCHITECTURE } else { "AMD64" }
$env:NUMBER_OF_PROCESSORS = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "1" }
$CurrentWindowsUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name.Split('\')[-1]
$CurrentProfile = Join-Path "C:\Users" $CurrentWindowsUser
if (Test-Path -LiteralPath $CurrentProfile) {
    $env:USERNAME = $CurrentWindowsUser
    $env:USERPROFILE = $CurrentProfile
    $env:HOMEDRIVE = "C:"
    $env:HOMEPATH = "\Users\$CurrentWindowsUser"
    $env:LOCALAPPDATA = Join-Path $CurrentProfile "AppData\Local"
    $env:APPDATA = Join-Path $CurrentProfile "AppData\Roaming"
    $env:TEMP = Join-Path $env:LOCALAPPDATA "Temp"
    $env:TMP = $env:TEMP
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue) -or !$env:INCLUDE -or !$env:LIB) {
    $ProgramFilesX86 = ${env:ProgramFiles(x86)}
    if (!$ProgramFilesX86) {
        $ProgramFilesX86 = "C:\Program Files (x86)"
    }
    $VisualStudioRoot = Join-Path $ProgramFilesX86 "Microsoft Visual Studio"
    $VcVars = Get-ChildItem -LiteralPath $VisualStudioRoot -Filter "vcvars64.bat" -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($VcVars) {
        $EnvironmentCommand = "call `"$($VcVars.FullName)`" >nul 2>&1 & set"
        $CommandProcessor = if ($env:ComSpec) { $env:ComSpec } else { "C:\Windows\System32\cmd.exe" }
        $EnvironmentLines = & $CommandProcessor /d /s /c $EnvironmentCommand
        foreach ($Line in $EnvironmentLines) {
            if ($Line -match '^([^=][^=]*)=(.*)$') {
                [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
            }
        }
    }
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue) -or !$env:INCLUDE -or !$env:LIB) {
    throw "A usable x64 MSVC environment is required for CUDA-DE compilation."
}
$NvccEnvironmentFlags = @("--use-local-env")

# The shared registry fixes link order. Each object fingerprints only its
# recursively quoted project dependencies, compiler identity and original flags.
$CacheBuilder = Join-Path $Project "scripts/cuda_build_cache.py"
$CacheArguments = @(
    $CacheBuilder, "--native-root", $Root, "--target", $BuildTarget,
    "--output-name", $OutputName, "--nvcc", $Nvcc, "--arch", $ArchName,
    "--ptxas-opt", "$PtxasOptLevel", "--expensive", "$UseExpensiveOptimizations".ToLowerInvariant(),
    "--max-register-count", "$MaxRegisterCount"
)
foreach ($Definition in $CompileDefinitions) { $CacheArguments += "--define=$Definition" }
if ($HostOnly) { $CacheArguments += "--host-only" }
if ($PtxasVerbose) { $CacheArguments += "--verbose" }
if ($env:OPTILAND_CUDA_DE_FORCE_REBUILD -in @("1", "true", "yes", "on")) {
    $CacheArguments += "--force"
}
& $PythonExecutable @CacheArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($HostOnly) { exit 0 }
$Out = Join-Path $Build $OutputName

# The production checkpoint route enables the validated E2 broker by default.
# Build its companion executable alongside the normal CUDA-DE binary so a
# clean production build does not silently depend on a stale manual artifact.
if (!$ExperimentalMultiIsland -and !$ProductionOnly) {
    $CompanionParams = @{
        ExperimentalMultiIsland = $true
        PtxasOptLevel = $PtxasOptLevel
        MaxRegisterCount = $MaxRegisterCount
        CudaArch = $CudaArch
        PythonExecutable = $PythonExecutable
    }
    if ($EnableExpensiveOptimizations) {
        $CompanionParams.EnableExpensiveOptimizations = $true
    }
    if ($DisableExpensiveOptimizations) {
        $CompanionParams.DisableExpensiveOptimizations = $true
    }
    if ($PtxasVerbose) {
        $CompanionParams.PtxasVerbose = $true
    }
    Write-Host "Building default E2 multi-island companion executable"
    & $PSCommandPath @CompanionParams
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    # The checkpoint launcher intentionally rejects a broker executable older
    # than any runtime input.  The object cache is content-addressed, so a
    # touched-but-unchanged source can legitimately reuse every object and
    # otherwise leave the executable timestamp stale.  Stamp the successfully
    # verified companion at least one millisecond past the newest launcher
    # contract input, then assert the same freshness invariant used at runtime.
    $CompanionOut = Join-Path $Build "optiland_multi_island_experiment.exe"
    $CompanionRuntimeInputs = @(
        (Join-Path $Root "cuda/de_cuda_scorer.cu"),
        (Join-Path $Root "cuda/de_cuda_device_common.cuh"),
        (Join-Path $Root "cuda/de_cuda_asphere_intersection.cu"),
        (Join-Path $Root "include/optiland/population_broker.hpp"),
        (Join-Path $Root "src/population_broker.cpp"),
        (Join-Path $Root "src/multi_island_experiment_main.cpp")
    )
    if (!(Test-Path -LiteralPath $CompanionOut)) {
        throw "Multi-island companion build completed without producing $CompanionOut"
    }
    $NewestRuntimeInput = $CompanionRuntimeInputs |
        Where-Object { Test-Path -LiteralPath $_ } |
        Get-Item |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    $CompanionItem = Get-Item -LiteralPath $CompanionOut
    if (
        $null -ne $NewestRuntimeInput -and
        $CompanionItem.LastWriteTimeUtc -le $NewestRuntimeInput.LastWriteTimeUtc
    ) {
        $CompanionItem.LastWriteTimeUtc =
            $NewestRuntimeInput.LastWriteTimeUtc.AddMilliseconds(1)
        $CompanionItem = Get-Item -LiteralPath $CompanionOut
    }
    if (
        $null -ne $NewestRuntimeInput -and
        $CompanionItem.LastWriteTimeUtc -le $NewestRuntimeInput.LastWriteTimeUtc
    ) {
        throw (
            "Multi-island companion remains stale after build: " +
            "$($NewestRuntimeInput.FullName) is not older than $CompanionOut"
        )
    }
    Write-Host (
        "Verified multi-island companion freshness against checkpoint runtime inputs: " +
        $CompanionItem.LastWriteTimeUtc.ToString("o")
    )
}
