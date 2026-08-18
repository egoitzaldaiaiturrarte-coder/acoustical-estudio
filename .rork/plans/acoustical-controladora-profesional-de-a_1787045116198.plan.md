---
name: "AcoustiCal: Controladora Profesional de Audio"
overview: "Transformar la app actual de análisis acústico en una controladora profesional de audio completa: una DAW por sí sola que también controla consolas, DAWs y dispositivos Bluetooth/WiFi. Con joystick espacial, faders con compensación de SPL, mapa de ruteos visual, gestión multi-dispositivo con sesiones, y modo músico con geolocalización."
createdAt: 2026-08-18T09:25:16.198Z
---
# AcoustiCal: Controladora Profesional de Audio

Transformar la app actual de análisis acústico en una controladora profesional de audio completa: una DAW por sí sola que también controla consolas, DAWs y dispositivos Bluetooth/WiFi. Con joystick espacial, faders con compensación de SPL, mapa de ruteos visual, gestión multi-dispositivo con sesiones, y modo músico con geolocalización.

## Visión General

AcoustiCal pasa de ser una app de análisis a una **controladora de audio profesional** que funciona como:
- Una DAW por sí sola (puede procesar y corregir audio independientemente)
- Una controladora compatible con todas las DAWs y consolas digitales profesionales
- Un gestor de hasta una docena de dispositivos (móviles, altavoces Bluetooth, PAs, monitores)
- Un corrector acústico automático con EQ dinámico en tiempo real

Nota importante: La app Android que construyamos aquí será el cerebro completo del sistema. La app nativa Mac y el VST plugin para DAWs requieren desarrollo aparte en C++/JUCE, pero la app Android podrá controlar todo vía OSC/WiFi/Bluetooth/USB.

---

## Paleta Visual: Deep Ocean Refinado

Mantiene la estética oscura teal/cian actual pero refinada para parecer hardware de estudio de gama alta:
- Fondo: negro abisal con sutiles gradientes teal profundo
- Superficies: tarjetas con efecto cristal mate, bordes finos cian
- Acentos: cian brillante para controles activos, ámbar para advertencias, lima para estado correcto
- Sliders y faders: look metálico con glow cian al tocar
- Joystick: área circular con rejilla radial, thumb semitransparente con halo
- EQ a pantalla completa: fondo negro puro, bandas brillantes, controles en esquinas

---

## Pantalla 1: Sesión y Login

Al abrir la app, aparece la pantalla de sesión:
- Campo **Nombre de sesión** (ej: "Estudio A", "Concierto Plaza")
- Campo **Contraseña/PIN** (numérico, 4-6 dígitos)
- Botón grande "Crear sesión" o "Unirse a sesión"

**Cómo se conectan los dispositivos:**
- El primer móvil crea la sesión y se convierte en **host**
- Otros móviles, ordenadores o dispositivos se unen introduciendo el mismo nombre + PIN
- La conexión se hace por **WiFi local** (NSD autodescubrimiento) — los dispositivos se encuentran automáticamente en la misma red
- Para Bluetooth: un asistente guía al usuario paso a paso para emparejar altavoces o interfaces
- Para consolas: muestra instrucciones visuales de cómo conectar (IP + puerto OSC, cable USB, etc.)
- Para cada tipo de dispositivo, un **panel de ayuda** explica la forma más estable de conectarlo

**Estados de conexión visibles:**
- Lista de dispositivos conectados con icono de tipo (móvil, consola, altavoz BT, USB, ordenador)
- Indicador de señal/latencia por dispositivo
- Botón "Añadir dispositivo" que abre el asistente de configuración

---

## Pantalla 2: Añadir Dispositivo y Configurar Trabajo

Al añadir un dispositivo o crear un trabajo, se configuran tres parámetros:

**A. Entorno virtual**
- Seleccionar el tipo de espacio: Sala, Estudio, Escenario, Descampado/Exterior
- Esto genera el entorno virtual donde se ubicarán los elementos con el joystick
- Dimensiones aproximadas (ancho x largo x alto) para cálculos de compensación

**B. Referencia de medición**
- Elegir qué se va a comparar al recibir:
  - Archivo de audio (pinky noise, sweep, pista de referencia)
  - El propio amplificador/consola (loop de prueba interno)
  - Secuencia de muestra predefinida
  - Envío desde la consola/DAW (canal auxiliar)
- Selector de profundidad de bits: **16, 24 o 32-bit float**
- Frecuencia de muestreo: hasta 96 kHz

**C. Salida(s) de actuación**
- Seleccionar por qué salida(s) se quiere actuar:
  - Consola (canal/bus/main/matrix específico)
  - Altavoces Bluetooth (uno o varios)
  - Monitores USB
  - Dispositivos móviles remotos
  - Salida del propio móvil
- Cada salida se puede configurar independientemente

**D. Especificaciones del trabajo**
- Nombre del trabajo
- Estéreo linkado o mono (si estéreo, opción de liberar L/R independientes)
- Modo: Controlador (operador) o Músico (monitor personal)
- Intervalo de auto-chequeo (cada X segundos, se generan "sondas" de medición automáticas)

---

## Pantalla 3: Joystick y Faders (Control Principal)

La pantalla principal de trabajo. Layout: **joystick a la izquierda, dos faders apilados a la derecha**.

### Joystick Espacial (izquierda)
- Área circular táctil que representa el espacio del entorno elegido
- 4 direcciones: arriba, abajo, izquierda, derecha
- Posiciona el elemento (instrumento, PA, monitor) en el espacio físico
- Rejilla radial de fondo con marcadores de posición
- Muestra la posición actual con coordenadas (ej: "Centro", "Izq 2m", "Frente")
- Al mover el joystick, se actualiza la corrección acústica en tiempo real

### Fader 1: Adelante/Atrás (derecha, superior)
- Fader vertical para profundidad en el espacio
- Muestra distancia en metros
- Compensa SPL automáticamente al mover (más atrás = más ganancia para mantener presión)

### Fader 2: Dimensionar (derecha, inferior)
- Fader vertical para tamaño del elemento
- Hace grande o pequeño el instrumento/PA/monitor
- Al dimensionar, mantiene la presión sonora (sube/baja ganancia proporcionalmente)
- Muestra tamaño relativo (ej: "PA pequeño", "PA grande", "Monitor")

### Compensación de SPL (en todos los controles)
- **Siempre activa**: cualquier movimiento del joystick o faders mantiene la presión sonora objetivo
- Si haces grande el bajo → compensa SPL para no saturar
- Si lo echas atrás → sube ganancia para mantener presión en el punto de escucha
- Mismo para arriba/abajo e izquierda/derecha
- Indicador visual de compensación activa (glow cian en el control)

### Botón de Auto-Chequeo (en pantalla joystick)
- Botón flotante que abre el panel de configuración de auto-medición:
  - Intervalo: cada X segundos (configurable, 10s a 10min)
  - Calidad: rápida, normal, alta (afecta FFT size y bandas)
  - Profundidad de bits: 16, 24, 32-bit float
  - Genera "sondas" automáticas que miden y corrigen sin intervención
  - Resultados visibles como puntos/bolas en el mapa de ruteos

### Modo Estéreo/Mono
- Toggle en la parte superior: Estéreo linkado / Estéreo libre / Mono
- En estéreo libre, cada lado (L/R) tiene sus propios controles de joystick y faders
- En linkado, ambos canales se ajustan juntos
- Indicador visual de modo activo

### Traducción y Ejecución
- Todos los movimientos del joystick y faders se **traducen automáticamente** a comandos OSC/MIDI/USB
- Se envían a la consola, DAW o dispositivos en tiempo real
- Latencia visible en pantalla (< 50ms ideal)
- Si hay múltiples salidas, se envía a todas las seleccionadas con compensación de delay

---

## Pantalla 4: Modo Músico

Cuando un dispositivo está asignado a un músico:
- El móvil del músico muestra sus monitores personales
- **Geolocalización activa**: el sistema sigue al músico por el escenario
- A medida que se mueve, ajusta automáticamente la presión de su mezcla
- Mide SPL en su posición en tiempo real
- Compensa volumen/delay según su ubicación relativa a las PAs y monitores
- El músico ve: SPL actual, posición en escenario, estado de monitores
- Botón de "Más yo" / "Menos yo" para ajustar su mezcla rápidamente
- Alerta visual si el SPL en su posición excede límites seguros

---

## Pantalla 5: Mapa de Ruteos

Visualización del esquema completo de conexiones y procesos:

### Layout: Entrada | Proceso | Salida + Referencia
- **Columna izquierda — Entradas**: dispositivos de captura (móviles mic, USB audio, consola input)
- **Columna centro — Proceso**: EQs dinámicos, correctores de presión, noise profiler, RT60
- **Columna derecha — Salidas**: consola, altavoces BT, monitores, PAs, móviles remotos
- **Línea inferior — Referencia**: fuente de referencia, muestra, secuencia de test

### Interacción
- Tocar cualquier bloque para entrar en su vista detallada
- Líneas de conexión entre bloques muestran el flujo de señal
- Color de líneas: cian = activo, gris = inactivo, ámbar = error
- Sondas de auto-chequeo aparecen como puntos animados que viajan por las líneas

### EQ a Pantalla Completa
- Al tocar un EQ del mapa, se abre a pantalla completa
- Fondo negro puro, bandas como barras verticales brillantes
- Controles de ajuste pequeños en las esquinas (gain, freq, Q, bypass)
- Spectro superpuesto en tiempo real
- Botón de cerrar en esquina superior derecha
- Gestión de bandas: añadir, eliminar, cambiar tipo (peaking, shelving, HPF, LPF)

### Gestión de Dispositivos desde el Mapa
- Desde aquí se gestionan todos los móviles con todas sus funciones
- Altavoces Bluetooth se asignan y posicionan desde aquí
- Cada dispositivo muestra su estado, SPL, y posición
- Se puede arrastrar para reordenar el flujo de señal

---

## Funciones de Audio Heredadas (ya construidas)

Todo el motor acústico que ya creamos se integra como capa base:
- **FFT y análisis espectral**: hasta 96 kHz, FFT configurable
- **EQ dinámico de bandas**: 8/10/16/31 bandas con corrección automática
- **Noise profiler**: sustracción de ruido ambiente con spectral gate
- **Dos tipos de ruido gestionados**:
  1. **Sonido ambiente**: se excluye de la mezcla, se auto-corregirá en cada silencio
  2. **Ruidos no deseados**: se auto-corregirán con el EQ dinámico en tiempo real
- **SPL meter y calibración**: hasta 97 dB, con compensación activa
- **RT60 estimator**: tiempo de reverberación en tiempo real
- **Geolocalización**: ajuste automático por altitud y entorno
- **Console integration**: OSC para X32/M32/A&H SQ-QU/Yamaha TF/genérico
- **USB Audio Class**: captura digital directa de consolas/interfaces
- **Mesh network**: multi-dispositivo vía WiFi (NSD + TCP)
- **Scenario presets**: Concierto, Teatro, Estudio, Exterior, Cine, Conferencia

### Auto-chequeo de Sondas
- Cada X tiempo definido, se generan "sondas" de medición automáticas
- Cada sonda mide: SPL, espectro, RT60, ruido ambiente, corrección necesaria
- Los resultados se visualizan como "bolas" en el mapa de ruteos
- Se puede configurar: intervalo, calidad (FFT size), profundidad de bits (16/24/32 float)
- Automatización completa: corrige sin intervención del usuario

---

## Tipos de Conexión Soportados

| Tipo | Dispositivos | Estabilidad |
|------|-------------|-------------|
| WiFi/OSC | Consolas X32/M32, A&H, Yamaha TF, DAWs | Muy estable, baja latencia |
| WiFi Mesh | Móviles con la app, ordenadores | Estable, autodescubrimiento NSD |
| Bluetooth | Altavoces BT, interfaces, monitores | Estable con asesoramiento |
| USB Audio | Interfaces, consolas con USB card | Muy estable, latencia mínima |
| MIDI CC | DAWs, controladores hardware | Estable, bidireccional |

Cada conexión incluye un **asistente visual** que guía al usuario paso a paso para configurarla de la forma más estable posible.

---

## Permisos Necesarios

- RECORD_AUDIO (micrófono)
- BLUETOOTH_CONNECT, BLUETOOTH_SCAN (altavoces BT)
- ACCESS_FINE_LOCATION, ACCESS_COARSE_LOCATION (geolocalización músico)
- INTERNET, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE, CHANGE_WIFI_MULTICAST_STATE (OSC/mesh)
- FOREGROUND_SERVICE, FOREGROUND_SERVICE_MICROPHONE (segundo plano)
- POST_NOTIFICATIONS (notificaciones de estado)
- USB host feature (captura USB Audio)

---

## Navegación (5 pestañas inferiores)

1. **Sesión** — Login, lista de dispositivos conectados, añadir dispositivo
2. **Control** — Joystick + faders (pantalla principal de trabajo)
3. **Ruteos** — Mapa visual de entradas/proceso/salidas + referencia
4. **EQ** — EQ dinámico a pantalla completa (accesible también desde el mapa)
5. **Ajustes** — Configuración de audio, scenario presets, calibración, geolocalización

---

## Fases de Implementación

**Fase 1 — Foundation (esta iteración):**
- Nuevos modelos de datos: sesión, dispositivo, trabajo, entorno virtual, referencia, salida
- Sistema de sesión con host/listener vía WiFi
- Asistente de conexión de dispositivos (Bluetooth + WiFi + USB + OSC)
- Refactor del ViewModel para soportar múltiples dispositivos y trabajos

**Fase 2 — Controlador:**
- Pantalla de joystick espacial (4 direcciones, área circular táctil)
- Dos faders (adelante/atrás, dimensionar) con compensación de SPL
- Traducción de movimientos a comandos OSC/MIDI
- Modo estéreo linkado/libre/mono
- Panel de auto-chequeo (intervalo, calidad, bits)

**Fase 3 — Mapa de Ruteos:**
- Visualización de cadena de señal: Entradas | Proceso | Salidas + Referencia
- Nodos interactivos para entrar a cada EQ/medidor/corrector
- EQ a pantalla completa con controles en esquinas
- Gestión de altavoces Bluetooth y móviles desde el mapa

**Fase 4 — Modo Músico y Automatización:**
- Modo músico con geolocalización y seguimiento por escenario
- Auto-chequeo de sondas con visualización en mapa
- Sustracción automática de ruido ambiente en silencios
- Corrección automática con EQ dinámico en tiempo real
- Integración completa con consola/DAW bidireccional

**Fase 5 — Pulido:**
- Animaciones de transición entre pantallas
- Micro-interacciones en joystick y faders (haptics, glow)
- Optimización de rendimiento para tiempo real
- Refinamiento visual Deep Ocean
