---
name: "Ruteos que responden, salidas que suenan, perfiles que no se pierden"
overview: "Arreglo los tres frenos que reportas: la página de Ruteos pasará de verse a obedecer (todo tocable y con sonido real por salida), los perfiles guardados sobrevivirán al reinicio y se podrán exportar/importar, y la referencia de medición tendrá su sitio claro para elegirla y cambiarla."
createdAt: 2026-08-21T13:54:22.862Z
---
# Ruteos que responden, salidas que suenan, perfiles que no se pierden

Arreglo los tres frenos que reportas: la página de Ruteos pasará de verse a obedecer (todo tocable y con sonido real por salida), los perfiles guardados sobrevivirán al reinicio y se podrán exportar/importar, y la referencia de medición tendrá su sitio claro para elegirla y cambiarla.

## Qué se va a arreglar y añadir

### 1. Ruteos: que tocar haga algo
**Por qué ahora "no pasa nada":** en el mapa, los nodos que no tienen una salida configurada (PA, in-ears, Bluetooth, consola) simplemente ignoran el toque — parecen botones muertos. Y al silenciar o subir volumen, nada suena distinto porque no hay audio real conectado a esas salidas.

- Todos los nodos del mapa serán tocables siempre: si la salida no existe, al tocarla se abre un panel para **crearla ahí mismo** (nombre, tipo, ajustes)
- Cada nodo mostrará su estado al momento: silenciado en rojo, volumen activo en cian
- El Centro de Control ganará un botón **"Añadir salida"** para crear monitores, in-ears, PA, Bluetooth o altavoz del móvil sin salir de la pantalla
- Cada fila tendrá respuesta visual inmediata al tocar (se marca, abre su hoja de ajustes)

### 2. Salidas que suenan de verdad
- Generador de señal de prueba (ruido rosa, barrido, tono) con botón **"Comprobar"** por salida: reproduce 2 segundos por la salida elegida para confirmar que suena
- Silenciar / Activar todo afectará al audio real: lo que está mute no reproduce, lo activo sí
- Control real del dispositivo: volumen del altavoz del móvil y del altavoz Bluetooth conectado
- La reproducción respetará el volumen, la ganancia y el delay configurados por salida

### 3. Perfiles que no se pierden + exportar
**Por qué se pierden:** hoy los perfiles solo viven en memoria; al cerrar la app desaparecen.

- Los perfiles de sala se guardarán en el almacenamiento del dispositivo y seguirán ahí al reabrir la app
- El último perfil activo se restaura solo al abrir
- Botón **Exportar** para compartir un perfil como archivo con otro dispositivo o compañero
- Botón **Importar** para cargar perfiles recibidos
- Los perfiles se podrán borrar desde la lista

### 4. Referencia de muestra clara y cambiable
**Por qué no la encuentras:** la fuente de referencia solo se puede elegir al añadir un dispositivo, y la referencia de espectro capturada solo vive en la pantalla de EQ. En Ruteos se ven como texto de solo lectura.

- En la tarjeta "Referencia" de Ruteos: selector tocable de la fuente de señal (archivo de audio, loop de consola, secuencia predefinida, envío DAW) — se cambia al instante y afecta a todo el flujo
- La referencia de espectro capturada también se gestionará desde Ruteos: capturar, ver estado (activa / sin referencia) y borrar
- Cuando no haya referencia capturada, se explicará claramente que la corrección usa objetivo plano
- Ambas referencias se podrán cambiar sin salir de la pantalla

## Validación
- Compilación completa verificada antes de terminar
- Prueba manual guiada: crear salida desde el mapa, comprobar señal, silenciar/activar, guardar perfil, reiniciar y comprobar que sigue, exportar/importar