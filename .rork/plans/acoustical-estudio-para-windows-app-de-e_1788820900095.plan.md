---
name: "Acoustical Estudio para Windows: app de escritorio + plugin VST para Cubase 5"
overview: "Versión de escritorio de Acoustical para Windows 10: se instala sola al conectar el móvil por USB, gestiona las tarjetas de audio, funciona como interface de audio con Cubase 5 e instala un plugin VST que se auto-corrige solo, con exactamente los mismos tres ecuas dinámicos y todos los parámetros de la app del móvil."
createdAt: 2026-09-07T22:41:40.095Z
---
# Acoustical Estudio para Windows: app de escritorio + plugin VST para Cubase 5

Versión de escritorio de Acoustical para Windows 10: se instala sola al conectar el móvil por USB, gestiona las tarjetas de audio, funciona como interface de audio con Cubase 5 e instala un plugin VST que se auto-corrige solo, con exactamente los mismos tres ecuas dinámicos y todos los parámetros de la app del móvil.

**Acoustical Estudio para Windows + plugin VST**

## 1. Instalación automática al conectar el móvil
- Programa residente que vigila el puerto USB: en cuanto conectas el móvil, Windows reconoce la conexión y se abre Acoustical Estudio solo, sin instalar nada a mano.
- El instalador incluye los drivers USB necesarios (driver ADB oficial firmado por Google) y los registra la primera vez que se conecta el móvil.
- Al detectar el móvil, se empareja con la app Acoustical del móvil automáticamente por USB y sincroniza: perfiles, correcciones actuales, geometría de la sala, retardo y ajustes.

## 2. Gestión de tarjetas de audio
- Lista de todas las entradas y salidas del PC (altavoces, auriculares, HDMI, Bluetooth, USB, interfaces externas) con estado en vivo.
- Matriz de ruteo: cualquier entrada hacia cualquier salida, varias a la vez (multiruta simultánea, igual que en el móvil).
- Selección de modo por tarjeta: compartido (se mezcla con Windows) o exclusivo (bit-perfect, menor latencia).
- Control de volumen, mute y latencia por ruta; espectro en vivo de cada entrada.

## 3. Modo interface de audio (Cubase 5)
- Driver "Acoustical Bridge" ASIO virtual: Cubase 5 lo ve como una interface de audio más y saca/entra el audio a través de él, 32 y 64 bits.
- Cable virtual de audio incluido en el instalador: crea un dispositivo de audio nuevo en Windows por donde se puede meter el sonido de cualquier programa (Spotify, navegador…) dentro de la cadena de corrección.
- Nota honesta: el cable virtual es un driver de sistema; en el primer ordenador donde se instale habrá que aceptar el certificado de firma (Windows lo avisa con un solo clic). El driver ASIO y todo lo demás no lo necesitan.
- Los tres ecuas dinámicos pueden corregir el audio que entra desde Cubase a través del bridge, igual que en el móvil.

## 4. Plugin VST para Cubase (actualizado: VST3 nativo 64 bits, sin enlaces)
- Foco exclusivo en el VST3 nativo de 64 bits (Cubase 5 y Cubase 15); el VST2.4 de 32 bits se sigue compilando como extra para Cubase 5. Sin sincronización con la app de escritorio: el plugin es autosuficiente y todo se edita desde su propia ventana.
- Bucle cerrado como el flujo del móvil: captura la REFERENCIA del espectro ANTES de salir (pre-EQ, lo que la fuente envía, con ayuda del generador integrado), mide el espectro DESPUÉS (post-EQ, lo que sale hacia los altavoces) y lo corrige hacia la referencia en tiempo real con cuatro procesos: el motor (corrector de dos bandas round-robin) más los tres ecuas dinámicos como tres pasadas extra, cada uno con su manera de arrancar y velocidades.
- Ruido trabajado de dos maneras: perfil capturado en los silencios (resta espectral por compuerta, 50 fotogramas) para que no contamine la corrección, y corrección de picos y reflexiones durante la reproducción por los ecuas dinámicos (cortan lo que sobresale de la media), con RT60 visible como indicador de la sala.
- Inventario completo en la ventana: intervalo de análisis, bandas (8–124), FFT (512–8K), ganancia máxima (hasta 50 dB), suavizado, SPL objetivo, retardo global (0,01 ms, con PDC al host) y por ecu: activo, arranque (necesidad/graves/agudos), intervalo, ganancia, mezclador, velocidad (×0,5–×4), barridos extra (0–6) y 4 bandas de apoyo de frecuencia libre con su Q.

## 5. Todo lo de la app móvil, en pantalla grande
- EQ compacto con las 124 bandas visibles a la vez, arrastre táctil con ratón y colores por ecu dinámico que está trabajando en cada banda.
- Los tres ecuas dinámicos idénticos con distintos ajustes, sus tres mini-EQs en vivo (cian, ámbar, magenta) y marcador de banda activa.
- Link/Unlink L/R, retardo global en pasos de 0,01 ms, sustracción de ruido, SPL objetivo, posición espacial con compensación, ciclo de verificación automática.

## 6. Inventario completo de parámetros (no se pierde ninguno)
- Motor: muestreo (44,1/48/88,2/96 kHz), FFT (512–8K), intervalo de análisis (25–500 ms), número de bandas (8/10/16/31/124), ganancia máxima, suavizado, umbral de ruido, corrección on/off.
- Por cada ecu dinámico: intervalo de decisión (100–2000 ms), ganancia máxima (1–50 dB), mezclador propio (0–100 %), velocidad (×0,5/×1/×2/×4), barridos extra (0–6), bandas de apoyo de frecuencia libre con su Q.
- Trabajo: modo L/R con Link, retardo 0,01 ms, bits (16/24), ciclo de verificación (15/30/60/120 s) con calidad, entorno y dimensiones de sala, fuente de referencia, SPL objetivo y posición espacial (x/y/z/tamaño).

## 7. Extras para sorprenderte
- Analizador de espectro (RTA) a pantalla completa con decaimiento y promedios, osciloscopio y medidor de SPL.
- Generador de señales integrado: barrido de frecuencia, seno por banda, ruido rosa y blanco.
- Sistema de presets con nombre, exportable e importable, sincronizados con el móvil.
- Atajos de teclado (activar ecuas, congelar corrección, cambiar de preset) y modo oscuro profundo estilo "Deep Ocean", en español, igual que el móvil.

## 8. Cómo se entrega y se valida (actualizado: sin compilar nunca en Windows)
- El instalador todo-en-uno se compila automáticamente en la nube (flujo de CI en `.github/workflows/build-windows.yml`): el usuario solo descarga `AcousticalEstudioSetup.exe` de Actions/Releases y lo ejecuta una vez. En el PC no se compila nada, nunca.
- A partir de ahí, la actualización es automática desde el móvil: la app Android lleva una tarjeta "PC / Windows" en Ajustes donde se descarga el paquete nuevo (instalador con versión en el nombre); el móvil lo guarda verificado con SHA-256 y lo sirve al PC por USB.
- El programa residente de Windows, al detectar el móvil, pregunta la versión al móvil (manifest), se descarga el instalador por el propio cable, verifica tamaño y SHA-256 antes de ejecutar nada, y lanza la instalación silenciosa con el aviso de permiso de Windows (UAC). El usuario siempre da el visto bueno final.
- El móvil solo sirve su servidor en 127.0.0.1 (loopback): accesible únicamente por el reenvío de puertos de adb (cable), nunca por Wi-Fi.
- Lo compilable aquí queda verificado: la app Android compila en verde y el motor C++ pasa la comprobación de sintaxis. El ejecutable de Windows final lo produce la nube; si el CI reporta un fallo de compilación, se corrige y se repite.