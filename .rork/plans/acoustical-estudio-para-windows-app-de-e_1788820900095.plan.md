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

## 4. Plugin VST para Cubase 5 (VST3 + VST2.4)
- Plugin en ambos formatos (VST3 y VST2.4, 32 y 64 bits) para máxima compatibilidad con Cubase 5.
- Dentro del plugin: el mismo motor automático que en el móvil. Los tres ecuas dinámicos analizan la señal que pasa por el canal de Cubase y se corrigen solos.
- Todos los parámetros del plugin editables desde la propia ventana del plugin y desde el panel de la app de escritorio, sincronizados en directo.

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

## 8. Cómo se entrega y se valida
- Código fuente completo de la app, del driver ASIO virtual, del cable virtual y del plugin VST, más el instalador todo-en-uno, listos para compilar en tu PC Windows con instrucciones de un solo paso.
- Aviso importante: en este entorno solo hay compiladores de móvil y web, no de Windows, así que no puedo compilar ni probar el ejecutable de Windows aquí. La compilación final se hará en tu PC siguiendo una guía sencilla que te dejaré preparada; si algo falla al compilarlo, lo corregimos juntos.