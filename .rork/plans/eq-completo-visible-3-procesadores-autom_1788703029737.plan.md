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

## 2. Tres procesadores automatizados
- [x] **Proceso 1 — Auto ayuda**: barrido automático cada **10 ms** en agudos, con velocidad y suavizado que se ajustan de forma lógica y progresiva hacia los graves (agudos: barrido 0,10 ms y suavizado máximo 4 ms; cada vez más lentos y suaves cuanto más graves). Sigue el patrón actual: va siempre donde más se necesita (recorta la banda con más SPL, sube la más baja), con rastreo por ranking. Tendrá **su propio mezclador** (nivel y envío propio, independiente del resto).
- [x] **Proceso 2 — EQ normal**: el ecualizador de siempre con sus ajustes (faders L/R, Link), más **4 bandas de apoyo de frecuencia libre** (ajustas tú su frecuencia y ganancia, para dar empuje puntual donde las bandas fijas no llegan).
- [x] **Proceso 3 — Auto-chequeo** (verificación): lo actual con **sus propios ajustes** y también sus **4 bandas de apoyo de frecuencia libre**.
- [x] Tras cada captura (ruido o referencia), los barridos y correcciones se reinician y relanzan solos.
- [x] **Auto-ajuste por frecuencia**: cada EQ adapta solo su velocidad de barrido y suavizado (rápido en agudos, lento en graves). Sin controles manuales para esto.
- [x] **Fijo y sin control**: frecuencia de muestreo siempre 96 kHz; umbral de ruido fijo en 120 (desaparece ese slider de Ajustes).
- [x] Cada proceso muestra su estado en vivo (activo, a qué banda está corrigiendo ahora).

## 3. Ajustes manuales → Ruteos
- [x] Se quitan de Ajustes todos los parámetros que pasan a automático o fijo (muestreo, FFT, intervalo, límite de ganancia, suavizado, umbral).
- [x] Lo que queda de manual se concentra en la pantalla **Ruteos**: activación de cada proceso, sus mezcladores, bandas de apoyo, retardos y ganancias.
- [x] Ajustes queda como pantalla de estado, no de configuración.

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
