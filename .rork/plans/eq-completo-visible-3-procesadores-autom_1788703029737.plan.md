---
name: "EQ completo visible + 3 procesadores automáticos"
overview: "Rediseño del ecualizador para ver las 124 bandas a la vez en tu Redmi, tres procesadores automatizados (Auto ayuda, EQ normal y Auto-chequeo) con parámetros que se autoajustan por frecuencia, y todos los ajustes manuales concentrados en Ruteos con nuevas entradas y salidas digitales."
createdAt: 2026-09-06T13:57:09.737Z
---
# EQ completo visible + 3 procesadores automáticos

Rediseño del ecualizador para ver las 124 bandas a la vez en tu Redmi, tres procesadores automatizados (Auto ayuda, EQ normal y Auto-chequeo) con parámetros que se autoajustan por frecuencia, y todos los ajustes manuales concentrados en Ruteos con nuevas entradas y salidas digitales.

# Plan: Acoustical — EQ completo + automatización

## 1. EQ compacto: todas las bandas visibles
- [x] Nueva tira de faders compacta: **las 124 bandas se ven todas a la vez**, sin scroll horizontal, en una sola fila de mini-faders finos.
- [x] Se ajustan arrastrando el dedo verticalmente sobre cada banda (táctil directo, como un canvas).
- [x] Etiquetas de frecuencia solo en marcas clave (100 Hz, 1 kHz, 10 kHz) para ganar espacio; el número exacto aparece al tocar una banda.
- [x] El gráfico de espectro se reduce a una tira estrecha arriba; más altura para los faders.
- [x] En general: **botones más grandes y textos informativos más pequeños** en las pantallas de EQ, para manejo con una mano.
- [x] Se mantiene Link/Unlink L/R y los ajustes independientes por canal.

## 2. Tres ecuas dinámicos idénticos con distintos ajustes
- [x] **El mismo corrector automático de frecuencias libres, repetido tres veces** (Ecu dinámico 1/2/3) con el mismo ecualizador gráfico pero distintos ajustes: más control y proceso más acelerado.
- [x] Cada uno decide a su intervalo (por defecto 800 ms, ajustable 100–2000 ms) mientras **los valores se ajustan cada 10 ms** hacia su objetivo.
- [x] Direcciones: EQ 1 va donde más se necesita, EQ 2 empieza por los graves, EQ 3 empieza por los agudos.
- [x] **Barridos extra** configurables por EQ (0–6 pares de bandas por decisión) para acelerar el proceso.
- [x] **Varias bandas de apoyo de frecuencia libre en cada EQ** (slider logarítmico 20 Hz–20 kHz, ±12 dB).
- [x] Cada EQ con **mezclador propio** (0–100%) y **ganancia máxima propia** (1–50 dB).
- [x] **Suavizado automático por frecuencia** (rápido en agudos, relajado en graves) con multiplicador de velocidad ×0.5/×1/×2/×4.
- [x] **Modo L o R**: cada EQ aplica sus parámetros por canal (los canales alternan la corrección) y **las bandas cambian de color** para que se aprecie.
- [x] **Todo el proceso se refleja**: tres mini-EQs en vivo debajo del EQ principal (cian, ámbar, magenta) con marcador en la banda que corrige ahora; las bandas del EQ principal se tiñen con el color del EQ que está trabajando en ellas.
- [x] Tras cada captura (ruido o referencia), las correcciones se reinician y relanzan solas.

## 3. Menú de Ajustes restaurado, con sección por ecu dinámico
- [x] **Menú de Ajustes restaurado** con los parámetros generales: muestreo, FFT, intervalo de análisis, ganancia máxima, suavizado y umbral de ruido.
- [x] **Sección específica para cada ecu dinámico**: intervalo de decisión, ganancia máxima, mezclador propio, velocidad del suavizado, barridos extra y sus bandas de apoyo de frecuencia libre.
- [x] Ruteos queda con la activación de cada ecu y su estado en vivo (más el ciclo de verificación del tercero).

## 4. Entradas y salidas digitales
- [x] **Entradas nuevas, varias a la vez (mezcladas)**:
  - Audio interno de apps del mismo móvil (Spotify, YouTube…) capturado digitalmente, con su permiso de captura.
  - Micrófono del propio móvil.
  - Detección de entrada externa enviada desde otro móvil (por la sesión/red ya existente), que aparece como entrada disponible.
  - Se pueden activar varias a la vez, cada una con su control en Ruteos.
- [x] **Salidas**: todas las salidas digitales internas disponibles del móvil (altavoz, auricular, Bluetooth, USB, cable) seleccionables y simultáneas como ya hace el sistema multiruta.

## 5. Validación
- [x] Compilación verificada con la herramienta de build.
- [ ] Prueba final en tu Redmi Note 15 4G: ver las 124 bandas, mover faders, y los tres procesos corriendo a la vez.
