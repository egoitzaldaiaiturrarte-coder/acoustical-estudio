; acoustical.iss — instalador todo-en-uno de Acoustical Estudio.
; Compilar con Inno Setup 6:  ISCC acoustical.iss
; Espera en dist\ lo generado por tools\build_all.ps1.

#define AppName "Acoustical Estudio"
#define AppVersion "1.0.0"
#define AppExe "AcousticalEstudio.exe"

[Setup]
AppId={{A7C3E8F1-5B2D-4A9C-8E1F-Acoustical01}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Acoustical
DefaultDirName={autopf}\Acoustical Estudio
DefaultGroupName={#AppName}
OutputBaseFilename=AcousticalEstudioSetup
OutputDir=..\dist\installer
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequired=admin

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; \
  GroupDescription: "Tareas:"
Name: "autostart"; Description: "Abrir Acoustical Estudio al iniciar Windows (recomendado: vigila la conexión del móvil)"; \
  GroupDescription: "Tareas:"; Flags: checkedonce
Name: "cable"; Description: "Instalar el cable virtual de audio (pide aceptar el certificado de prueba una vez)"; \
  GroupDescription: "Componentes extra:"; Flags: unchecked

[Files]
; App de escritorio + adb + drivers ADB
Source: "..\dist\app\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
; Plugin VST3 (x64) — Cubase 5 lo busca en Common Files\VST3
Source: "..\dist\plugin\Acoustical Dynamic EQ.vst3"; DestDir: "{cf}\VST3"; \
  Flags: recursesubdirs ignoreversion; Check: Is64BitInstallMode
; Plugin VST2 (x64), si se compiló
Source: "..\dist\plugin\AcousticalDynamicEq.dll"; DestDir: "{cf}\Steinberg\VstPlugins"; \
  Flags: ignoreversion skipifsourcedoesntexist; Check: Is64BitInstallMode
; Driver ASIO x64 y x86 (Cubase 5 de 32 y 64 bits)
Source: "..\dist\bridge\x64\AcousticalBridge.dll"; DestDir: "{app}\bridge\x64"; Flags: ignoreversion
Source: "..\dist\bridge\x86\AcousticalBridge.dll"; DestDir: "{app}\bridge\x86"; Flags: ignoreversion
; Cable virtual (opcional)
Source: "..\dist\cable\*"; DestDir: "{app}\cable"; Flags: recursesubdirs ignoreversion; \
  Tasks: cable

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Arranque automático: la app arranca al iniciar Windows y su vigilante USB
; abre/sincroniza en cuanto se conecta el móvil
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "AcousticalEstudio"; \
  ValueData: """{app}\{#AppExe}"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; 1. Registrar los drivers ADB incluidos (firmados por Google) para el móvil
Filename: "pnputil"; Parameters: "/add-driver ""{app}\adb\android_winusb.inf"" /install"; \
  Description: "Registrando drivers USB del móvil…"; Flags: runhidden

; 2. Registrar el driver ASIO (x64) con regsvr32 — escribe SOFTWARE\ASIO\Acoustical Bridge
Filename: "regsvr32"; Parameters: "/s ""{app}\bridge\x64\AcousticalBridge.dll"""; \
  StatusMsg: "Registrando Acoustical Bridge (ASIO 64 bits)…"; Check: Is64BitInstallMode
Filename: "regsvr32"; Parameters: "/s ""{app}\bridge\x86\AcousticalBridge.dll"""; \
  StatusMsg: "Registrando Acoustical Bridge (ASIO 32 bits)…"

; 3. Cable virtual (opcional)
Filename: "pnputil"; Parameters: "/add-driver ""{app}\cable\AcousticalCable.inf"" /install"; \
  StatusMsg: "Instalando el cable virtual de audio…"; Tasks: cable

Filename: "{app}\{#AppExe}"; Description: "Abrir {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "regsvr32"; Parameters: "/s /u ""{app}\bridge\x64\AcousticalBridge.dll"""; RunOnceId: "Unreg64"
Filename: "regsvr32"; Parameters: "/s /u ""{app}\bridge\x86\AcousticalBridge.dll"""; RunOnceId: "Unreg32"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\cable"
