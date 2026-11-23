param(
    [string]$OutputName = "optiland_cpu.exe",
    [string]$PythonExecutable = $env:LENSDE_PYTHON
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Project = (Resolve-Path (Join-Path $Root "..")).Path
if (!$PythonExecutable) {
    $ProjectPython = Join-Path $Project ".venv/Scripts/python.exe"
    $PythonExecutable = if (Test-Path -LiteralPath $ProjectPython) { $ProjectPython } else { "python" }
}
# This common builder validates native/sources.json and reuses verified CPU objects.
& $PythonExecutable (Join-Path $Project "scripts/build.py") --backend cpu --output-name $OutputName
exit $LASTEXITCODE
