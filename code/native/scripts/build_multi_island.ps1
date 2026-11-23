param(
    [ValidatePattern('^\d{2}$')]
    [string]$CudaArch = $(if ($env:OPTILAND_CUDA_ARCH) { $env:OPTILAND_CUDA_ARCH } else { "86" }),
    [ValidateSet(0, 1, 2, 3)]
    [int]$PtxasOptLevel = 3
)

$ErrorActionPreference = "Stop"

Write-Host (
    "Building GlassChoice Scout / population broker target only: " +
    "optiland_multi_island_experiment.exe"
)
& (Join-Path $PSScriptRoot "build_cuda_de.ps1") `
    -ExperimentalMultiIsland `
    -CudaArch $CudaArch `
    -PtxasOptLevel $PtxasOptLevel
exit $LASTEXITCODE
