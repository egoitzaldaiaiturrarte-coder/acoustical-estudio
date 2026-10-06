# build_all.ps1 - compilación de un paso para Windows.
# Descarga JUCE si falta, compila app + plugin (x64) y el driver ASIO (x64 + Win32),
# y deja todo listo para el instalador en dist\.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Host "=== Acoustical Estudio - build de un paso ===" -ForegroundColor Cyan

# 0. adb (platform-tools oficiales de Google): sin él el PC no ve el móvil
if (-not (Test-Path "tools\adb\adb.exe")) {
    Write-Host "== Descargando adb (platform-tools de Google) ==" -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path "tools\adb" | Out-Null
    $zip = Join-Path $env:TEMP "platform-tools.zip"
    Invoke-WebRequest -Uri "https://dl.google.com/android/repository/platform-tools-latest-windows.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath "$env:TEMP\platform-tools" -Force
    Copy-Item "$env:TEMP\platform-tools\platform-tools\adb.exe" "tools\adb\" -Force
    Copy-Item "$env:TEMP\platform-tools\platform-tools\AdbWinApi.dll" "tools\adb\" -Force
    Copy-Item "$env:TEMP\platform-tools\platform-tools\AdbWinUsbApi.dll" "tools\adb\" -Force
}

# 1. Configurar + compilar app, engine y plugin (x64)
# Ojo: se re-configura si falta el CMakeCache.txt (no solo si falta la
# carpeta): la CI cachea build\_deps (el clon de JUCE de FetchContent) y al
# restaurarla la carpeta build\ existe SIN cache — re-configurar es lo que
# hace que FetchContent reaproveche el clon en vez de clonar de nuevo.
Write-Host "== App + plugin (x64) ==" -ForegroundColor Yellow
if (-not (Test-Path "build\CMakeCache.txt")) {
    cmake -S . -B build -A x64
}
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando la app o el plugin" }

# 2. Driver ASIO x64 (mismo criterio de re-config que el build principal)
Write-Host "== Acoustical Bridge (x64) ==" -ForegroundColor Yellow
if (-not (Test-Path "build-bridge64\CMakeCache.txt")) {
    cmake -S asio-bridge -B build-bridge64 -A x64
}
cmake --build build-bridge64 --config Release
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando el bridge x64" }

# 3. Plugin Win32 (Cubase 5 de 32 bits only loads 32-bit plugins)
Write-Host "== App + plugin (Win32) ==" -ForegroundColor Yellow
if (-not (Test-Path "build-plugin32\CMakeCache.txt")) {
    cmake -S . -B build-plugin32 -A Win32
}
cmake --build build-plugin32 --config Release --target AcousticalDynamicEq_VST AcousticalDynamicEq_VST3 --parallel
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando el plugin Win32" }

# 4. Driver ASIO Win32 (Cubase 5 de 32 bits)
Write-Host "== Acoustical Bridge (Win32) ==" -ForegroundColor Yellow
if (-not (Test-Path "build-bridge32\CMakeCache.txt")) {
    cmake -S asio-bridge -B build-bridge32 -A Win32
}
cmake --build build-bridge32 --config Release
if ($LASTEXITCODE -ne 0) { throw "Fallo compilando el bridge Win32" }

# 5. Reunir dist\
Write-Host "== Preparando dist\ ==" -ForegroundColor Yellow
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path "$dist\app", "$dist\plugin", `
    "$dist\plugin32", "$dist\bridge\x64", "$dist\bridge\x86" | Out-Null

# JUCE deja los artefactos en <target>_artefacts\<config>; buscamos en todo build\
$appExe = Get-ChildItem "build" -Recurse -Filter "Acoustical*Estudio*.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if (-not $appExe) { throw "No se encontró el ejecutable de Acoustical Estudio en build\" }
Copy-Item $appExe.FullName "$dist\app\" -Force
# PDB de la app: subido como artefacto para poder mapear el offset del crash
# (startup.log: "CRASH … = Acoustical Estudio.exe +0x…") a función/línea.
$appPdb = [System.IO.Path]::ChangeExtension($appExe.FullName, ".pdb")
if (Test-Path $appPdb) { Copy-Item $appPdb "$dist\app\" -Force }
# adb incluido (si está en tools\adb, se copia con todo)
if (Test-Path "tools\adb") { Copy-Item "tools\adb" "$dist\app\adb" -Recurse -Force }

# Plugin VST3 (carpeta-bundle) y VST2 (dll), si se compiló
$vst3 = Get-ChildItem "build" -Recurse -Directory -Filter "*.vst3" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if ($vst3) { Copy-Item $vst3.FullName "$dist\plugin\" -Recurse -Force }
$vst2 = Get-ChildItem "build" -Recurse -Filter "Acoustical*Dynamic*EQ*.dll" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if ($vst2) { Copy-Item $vst2.FullName "$dist\plugin\" -Force }

# Plugin Win32 (Cubase 5 de 32 bits): VST2.4 dll + VST3 bundle
$vst32 = Get-ChildItem "build-plugin32" -Recurse -Directory -Filter "*.vst3" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if ($vst32) { Copy-Item $vst32.FullName "$dist\plugin32\" -Recurse -Force }
$vst2_32 = Get-ChildItem "build-plugin32" -Recurse -Filter "Acoustical*Dynamic*EQ*.dll" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Release\\VST\\" } | Select-Object -First 1
if ($vst2_32) { Copy-Item $vst2_32.FullName "$dist\plugin32\" -Force }

$bridge64 = Get-ChildItem "build-bridge64" -Recurse -Filter "AcousticalBridge.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
$bridge32 = Get-ChildItem "build-bridge32" -Recurse -Filter "AcousticalBridge.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $bridge64) { throw "No se encontró AcousticalBridge.dll x64" }
if (-not $bridge32) { throw "No se encontró AcousticalBridge.dll Win32" }
Copy-Item $bridge64.FullName "$dist\bridge\x64\" -Force
Copy-Item $bridge32.FullName "$dist\bridge\x86\" -Force
# PDBs del driver ASIO (el CMakeLists del asio-bridge genera PDB en Debug y
# Release a propósito): se copian junto a cada dll para que el instalador
# (installer\acoustical.iss) las empaquete, igual que la PDB de la app:
# un crash dentro del DAW con un offset del driver pasa a ser resoluble.
$bridge64Pdb = [System.IO.Path]::ChangeExtension($bridge64.FullName, ".pdb")
if (Test-Path $bridge64Pdb) { Copy-Item $bridge64Pdb "$dist\bridge\x64\" -Force }
$bridge32Pdb = [System.IO.Path]::ChangeExtension($bridge32.FullName, ".pdb")
if (Test-Path $bridge32Pdb) { Copy-Item $bridge32Pdb "$dist\bridge\x86\" -Force }

Write-Host ""
Write-Host "=== Todo compilado en dist\ ===" -ForegroundColor Green
Write-Host "Siguiente paso: compilar el instalador con Inno Setup:"
Write-Host "  ISCC installer\acoustical.iss"
if (-not (Test-Path "$dist\plugin32\Acoustical Dynamic EQ.dll")) {
    Write-Warning "El plugin VST2.4 Win32 no se ha generado: Cubase 5 de 32 bits no lo cargará."
}
