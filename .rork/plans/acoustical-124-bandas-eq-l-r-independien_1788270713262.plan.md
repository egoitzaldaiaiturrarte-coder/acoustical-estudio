---
name: "AcoustiCal: 124 bandas, EQ L/R independiente, multiruta y Ruteos con entradas"
overview: "Amplía el motor a 124 bandas con corrección de hasta 50 dB, rediseña Ruteos como dos columnas conectadas (entradas gestionables a la izquierda, salidas a la derecha), permite que cada salida suene a la vez por su propio dispositivo (altavoz + Bluetooth simultáneos con señal distinta), desvincula el ecualizador en L y R, y acelera la auto-corrección a 2 correcciones por ciclo (la banda más alta y la más baja) dos veces por segundo."
createdAt: 2026-09-01T13:51:53.262Z
---
# AcoustiCal: 124 bandas, EQ L/R independiente, multiruta y Ruteos con entradas

Amplía el motor a 124 bandas con corrección de hasta 50 dB, rediseña Ruteos como dos columnas conectadas (entradas gestionables a la izquierda, salidas a la derecha), permite que cada salida suene a la vez por su propio dispositivo (altavoz + Bluetooth simultáneos con señal distinta), desvincula el ecualizador en L y R, y acelera la auto-corrección a 2 correcciones por ciclo (la banda más alta y la más baja) dos veces por segundo.

## Features

**124 bandas de ecualización**
- Nueva opción "124 bandas (ultra)" en Ajustes, con frecuencias logarítmicas de 20 Hz a 20 kHz generadas automáticamente.
- El ecualizador se vuelve deslizable para manejar 124 faders sin romper el rendimiento.
- El indicador de banda "al límite" se calcula según el límite real configurado (ya no fijo en 12 dB).

**Corrección de ganancia máxima a 50 dB**
- El slider "Ganancia máxima" de Ajustes pasa de 0–24 a 0–50 dB.
- El ajuste manual por banda deja de estar clavado en ±12 dB y respeta el límite configurado.

**Ruteos en dos columnas con entradas**
- Rediseño: columna izquierda = ENTRADAS (micrófono del móvil, USB audio, consola In, archivo/referencia), columna derecha = SALIDAS (consola, BT, PA, in-ears, monitores, remotos). Líneas de conexión entre ambas.
- Cada entrada es tocable: hoja propia para activarla, ver su estado y ajustar ganancia/nivel.
- Cada salida conserva su hoja actual (comprobar que suena, ganancia, delay, mute).
- Se elimina la maraña de nodos de proceso: el flujo se entiende de un vistazo.

**Multiruta real: cada salida suena por su propio dispositivo**
- Nuevo reproductor multiruta: cada salida activa tiene su propia pista de audio asignada a su dispositivo físico (altavoz interno, Bluetooth, USB, cable).
- El altavoz del móvil y el Bluetooth suenan A LA VEZ, cada uno con su señal, volumen, ganancia y delay propios.
- Se pueden lanzar pruebas simultáneas distintas (ruido rosa en una salida, barrido en otra) desde Ruteos.

**Ecualizador L/R independiente**
- Botón "Link/Unlink" en el ecualizador: linkado (como ahora, ambas manos juntas) o libre.
- En modo libre se elige canal L o R y las bandas se ajustan por separado; cada canal conserva sus propios valores.
- La corrección automática sigue aplicándose a ambos canales; lo independiente es el ajuste manual.

**Auto-corrección rápida: 2 bandas por ciclo, 2 veces por segundo**
- Cada 500 ms se corrigen a la vez las dos bandas más desviadas: la que más sobra (se recorta) y la que más falta (se sube).
- Se acabó el ciclo de 10 s con una sola banda: corrección continua y visible al instante en los faders.

**Retardos de precisión centimétrica (0.01 ms)**
- Todos los retardos (global de Ajustes, por salida en Ruteos/Mix y Calibración) se ajustan en pasos de 0.01 ms, suficientes para alinear equipos por centímetros (1 cm ≈ 0.03 ms).
- Siempre se muestran los dos valores juntos: "12.45 ms · 4.27 m" (o cm en retardos cortos), usando 34.3 cm por ms.
- Internamente el retardo se redondea a muestras enteras a 48 kHz (1 muestra ≈ 0.021 ms): la pantalla muestra 0.01 ms y la reproducción es lo más fiel posible físicamente.
- El rango por salida queda 0–2000 ms con precisión fina; flechas +/− de 0.01 ms además del slider para ajuste exacto.

**Suavizado adaptado al límite de 50 dB**
- Con límites de corrección altos el motor amplía automáticamente el tiempo de suavizado (adaptación más lenta) para evitar oscilaciones: el suavizado efectivo se relaja proporcionalmente a partir de 24 dB de límite.
- En Ajustes se muestra un aviso bajo el suavizado cuando el límite alto exige suavizado extendido, con el valor efectivo.

## Design
- Paleta Deep Ocean actual (negro abisal, cian, ámbar, lima) sin cambios.
- Ruteos: dos paneles tipo columna con título "Entradas" / "Salidas", nodos compactos con pill de estado y líneas cian de conexión; botón "+ Añadir" al pie de cada columna.
- Ecualizador: fila de chips L / R / Link arriba de los faders; el canal activo se resalta en cian, el inactivo se atenúa.

## Pages / Screens
- **Ajustes**: chips de bandas (ahora con 124), slider de ganancia 0–50 dB, retardo con doble lectura tiempo/distancia y aviso de suavizado extendido.
- **Ruteos**: pantalla rehecha en dos columnas conectadas + hojas de detalle de entrada y salida (delay fino de 0.01 ms con distancia en cm).
- **Ecualizador (completo y simple)**: selector de canal y link/unlink; lista deslizable de bandas.
- **Control**: sin cambios estructurales; la auto-corrección rápida corre de fondo cuando el motor está activo.
- **Calibración**: retardo con doble lectura y pasos de 0.01 ms.

## Validación
- Compilación Android completa (runChecks) al terminar.
- Prueba de comportamiento en el simulador de nube: motor, señal de prueba multiruta, retardo fino y auto-corrección a 2 Hz.
- Importante: no tengo acceso a tu Redmi Note 15 4G físico, así que la verificación final en ese terminal te toca a ti (altavoz + Bluetooth a la vez y sensación del suavizado). En el momento en que detectes algo raro ahí, lo reportas y lo afino.