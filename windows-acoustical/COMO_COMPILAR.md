# Cómo compilar Acoustical Estudio (Windows)

## Lo que necesitas (una sola vez)

1. **Windows 10 o 11** (64 bits).
2. **Visual Studio 2022 Community** (gratis): https://visualstudio.microsoft.com/
   - En el instalador marca **"Desarrollo para escritorio con C++"**.
3. **CMake** (viene incluido con Visual Studio; con marcar el componente basta).
4. **Git** (opcional, solo si quieres descargar JUCE por git; el script lo hace solo).
5. Para el plugin **VST2.4**: la carpeta del SDK VST2 de Steinberg
   (`aeffect.h`, `aeffectx.h`, `vst2.x`…). Steinberg ya no lo reparte
   públicamente; si tienes una copia, apunta la variable `VST2_SDK_PATH`
   hacia ella. **Sin ella el proyecto compila igualmente la versión VST3**
   (Cubase 5 la soporta) y el resto de componentes no la necesitan.
6. Para el **cable virtual**: Windows Driver Kit (WDK) — opcional, ver abajo.

## Compilar todo (un paso)

Abre PowerShell en esta carpeta y ejecuta:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_all.ps1
```

El script:
- Descarga JUCE (framework de audio) automáticamente si no está.
- Compila la app de escritorio (x64) y el plugin (x64 + Win32).
- Compila el driver ASIO "Acoustical Bridge" (x64 + Win32).
- Deja todo en la carpeta `dist\` listo para el instalador.

Si además quieres el plugin VST2.4, antes ejecuta:

```powershell
$env:VST2_SDK_PATH = "C:\ruta\a\vstsdk2.4"
```

## Compilar a mano (si prefieres paso a paso)

```powershell
# App + plugin + engine
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Driver ASIO 64 y 32 bits
cmake -S asio-bridge -B build-bridge64 -A x64
cmake --build build-bridge64 --config Release
cmake -S asio-bridge -B build-bridge32 -A Win32
cmake --build build-bridge32 --config Release
```

## El cable virtual (opcional)

Necesita el **WDK** (Windows Driver Kit). Ver
`virtual-cable/README.md`: se basa en el ejemplo oficial `sysvad` de
Microsoft (código abierto), renombrado como "Acoustical Cable", y se firma
con un certificado de prueba (Windows pide aceptarlo una vez).

## Instalar en Windows

Con todo en `dist\`, compila el instalador con [Inno Setup](https://jrsoftware.org/isinfo.php):

```
ISCC installer\acoustical.iss
```

y ejecuta el `AcousticalEstudioSetup.exe` resultante. El instalador:
- instala app, plugin (VST3 y VST2), driver ASIO y sus entradas de registro,
- registra el driver ADB de Google incluido (`pnputil`),
- crea el arranque automático al conectar el móvil,
- opcionalmente instala el cable virtual (pide aceptar el certificado).
