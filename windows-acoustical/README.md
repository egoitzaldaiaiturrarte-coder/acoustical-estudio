# Acoustical Estudio para Windows + plugin VST

Versión de escritorio de Acoustical para Windows 10 (y 11). Mismo motor, mismos
tres ecuas dinámicos idénticos con distintos ajustes, mismas 124 bandas, en
pantalla grande.

## Qué incluye

| Componente | Carpeta | Qué hace |
|---|---|---|
| **Acoustical Estudio** (app) | `app/` | EQ compacto de 124 bandas, 3 ecuas dinámicos, matriz de ruteo de tarjetas, Ajustes completos, RTA/osciloscopio/SPL, generador de señales, presets, enlace USB con el móvil |
| **Motor de audio** | `engine/` | Port exacto en C++ del motor de la app Android (FFT, corrector de sala, los 3 ecuas dinámicos, perfil de ruido, medidor SPL, generador de señales) |
| **Plugin VST** | `plugin/` | VST3 + VST2.4 (32 y 64 bits) para Cubase 5 con el mismo motor autocorrector |
| **Acoustical Bridge** | `asio-bridge/` | Driver ASIO virtual en modo usuario: Cubase 5 lo ve como interface de audio. No necesita firma de kernel |
| **Cable virtual de audio** | `virtual-cable/` | Dispositivo de audio virtual (driver de kernel) para meter el sonido de cualquier programa en la cadena |
| **Instalador todo-en-uno** | `installer/` | Inno Setup: app + plugin + ASIO + drivers ADB + cable virtual + arranque automático al conectar el móvil |

## Instalación automática al conectar el móvil

El instalador registra el programa residente **Acoustical Watchdog**, que vigila
el USB. Al conectar el móvil: instala el driver ADB (incluido, firmado por
Google), abre Acoustical Estudio y se empareja con la app Acoustical del móvil
por `adb` (reenvío de puertos + sincronización JSON).

## Cómo compilar

Ver **[COMO_COMPILAR.md](COMO_COMPILAR.md)** — una sola página, con el script
`tools/build_all.ps1` que lo hace casi todo solo.

## Aviso honesto sobre el cable virtual

El driver ASIO y toda la app funcionan sin firma de kernel. El **cable virtual**
sí es un driver de sistema: la primera vez hay que aceptar el certificado de
prueba de Windows (un solo clic, instrucciones incluidas). Si prefieres saltarte
ese paso, el instalador lo deja opcional.
