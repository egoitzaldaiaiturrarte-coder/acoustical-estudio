# build_all.ps1 — compilación de un paso para Windows.
# Descarga JUCE si falta, compila app + plugin (x64) y el driver ASIO (x64 + Win32),
# y deja todo listo para el instalador en dist\.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Host "=== Acoustical Estudio — build de un paso ===" -ForegroundColor Cyan

# 1. Configurar + compilar app, engine y plugin (x64)
Write-Host "== App + plugin (x64) ==" -ForegroundColor Yellow
if (-not (Test-Path "build")) {
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
}
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando la app o el plugin" }

# 2. Driver ASIO x64
Write-Host "== Acoustical Bridge (x64) ==" -ForegroundColor Yellow
if (-not (Test-Path "build-bridge64")) {
    cmake -S asio-bridge -B build-bridge64 -G "Visual Studio 17 2022" -A x64
}
cmake --build build-bridge64 --config Release
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando el bridge x64" }

# 3. Driver ASIO Win32 (Cubase 5 de 32 bits)
Write-Host "== Acoustical Bridge (Win32) ==" -ForegroundColor Yellow
if (-not (Test-Path "build-bridge32")) {
    cmake -S asio-bridge -B build-bridge32 -G "Visual Studio 17 2022" -A Win32
}
cmake --build build-bridge32 --config Release
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando el bridge Win32" }

# 4. Reunir dist\
Write-Host "== Preparando dist\ ==" -ForegroundColor Yellow
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path "$dist\app", "$dist\plugin", `
    "$dist\bridge\x64", "$dist\bridge\x86" | Out-Null

Copy-Item "build\app\Release\AcousticalEstudio.exe" "$dist\app\" -Force
# adb incluido (si está en tools\adb, se copia con todo)
if (Test-Path "tools\adb") { Copy-Item "tools\adb" "$dist\app\adb" -Recurse -Force }

Get-ChildItem "build\plugin" -Recurse -Include *.vst3,*.dll |
    Where-Object { $_.Name -like "Acoustical*" } |
    Copy-Item -Destination "$dist\plugin\" -Force -ErrorAction SilentlyContinue

Copy-Item "build-bridge64\asio-bridge\Release\AcousticalBridge.dll" "$dist\bridge\x64\" -Force
Copy-Item "build-bridge32\asio-bridge\Release\AcousticalBridge.dll" "$dist\bridge\x86\" -Force

Write-Host ""
Write-Host "=== Todo compilado en dist\ ===" -ForegroundColor Green
Write-Host "Siguiente paso: compilar el instalador con Inno Setup:"
Write-Host "  ISCC installer\acoustical.iss"
if (-not $env:VST2_SDK_PATH) {
    Write-Host ""
    Write-Warning "VST2_SDK_PATH no definido: el plugin se compila solo en VST3 (Cubase 5 lo soporta)."
    Write-Warning "Para VST2.4: define la variable con la carpeta del SDK legacy y vuelve a ejecutar."
}
