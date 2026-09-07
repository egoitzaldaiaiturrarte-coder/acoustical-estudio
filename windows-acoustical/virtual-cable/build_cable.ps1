# build_cable.ps1 — construye el "Acoustical Cable" a partir del ejemplo
# sysvad de Microsoft, renombrando el dispositivo y preparando el paquete
# firmado para el instalador. Requiere WDK + Visual Studio 2022.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist\cable"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Write-Host "== 1. Ejemplos de drivers de Windows (sysvad) =="
$samples = Join-Path $env:TEMP "windows-driver-samples"
if (-not (Test-Path $samples)) {
    git clone --depth 1 https://github.com/microsoft/Windows-driver-samples.git $samples
}
$sysvad = Join-Path $samples "audio\sysvad"
if (-not (Test-Path $sysvad)) { throw "No se encontró sysvad en $samples" }

Write-Host "== 2. Renombrado a 'Acoustical Cable' =="
# Renombrado no destructivo: copia el proyecto simpleaudioendpoint y sustituye
# nombres visibles en el INF y en el título del miniport.
$work = Join-Path $env:TEMP "AcousticalCable"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
Copy-Item -Recurse (Join-Path $sysvad "simpleaudiosample") $work

$inf = Get-ChildItem $work -Recurse -Filter *.inf | Select-Object -First 1
if ($inf) {
    (Get-Content $inf.FullName -Raw) `
        -replace "Simple Audio Sample", "Acoustical Cable" `
        -replace "SimpleAudioSample", "AcousticalCable" |
        Set-Content $inf.FullName -Encoding UTF8
}
Get-ChildItem $work -Recurse -Include *.h,*.cpp,*.rc | ForEach-Object {
    (Get-Content $_.FullName -Raw) `
        -replace "Simple Audio Sample", "Acoustical Cable" `
        -replace "SimpleAudioSample", "AcousticalCable" |
        Set-Content $_.FullName -Encoding UTF8
}

Write-Host "== 3. Compilación (x64 Release con WDK) =="
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
    Select-Object -First 1
if (-not $msbuild) { throw "MSBuild no encontrado; instala Visual Studio 2022 + WDK" }
& $msbuild (Get-ChildItem $work -Recurse -Filter *.vcxproj | Select-Object -First 1).FullName `
    /p:Configuration=Release /p:Platform=x64 /m
if ($LASTEXITCODE -ne 0) { throw "Fallo de compilación del cable" }

Write-Host "== 4. Firma de prueba =="
$cert = "AcousticalCableTest"
$sys = Get-ChildItem $work -Recurse -Filter AcousticalCable.sys | Select-Object -First 1
if ($sys) {
    & signtool sign /a /n $cert /fd SHA256 $sys.FullName 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Sin cert de prueba disponible: crea uno con"
        Write-Warning "  makecert / self-signed y firma con signtool sign /fd SHA256"
    }
    Copy-Item $sys.FullName $dist -Force
}
Get-ChildItem $work -Recurse -Include *.inf,*.cat | Copy-Item -Destination $dist -Force

Write-Host "== 5. Listo en $dist =="
Write-Host "Instalar con:  pnputil /add-driver AcousticalCable.inf /install"
