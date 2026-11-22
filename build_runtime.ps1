$ErrorActionPreference = "Stop"
$vsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsPath)) {
    Write-Host "ERROR: VsDevCmd.bat not found at $vsPath"
    exit 1
}
cmd /c "`"$vsPath`" -arch=x64 >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^(.+?)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}
Set-Location "D:\Port GTA VI\AI\dedsec"
if (Test-Path runtime\build) { Remove-Item -Recurse -Force runtime\build }
cmake -S runtime -B runtime/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if ($LASTEXITCODE -ne 0) { Write-Host "CMAKE_CONFIG_FAILED"; exit $LASTEXITCODE }
ninja -C runtime/build
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD_FAILED"; exit $LASTEXITCODE }
Write-Host "BUILD_SUCCESS"