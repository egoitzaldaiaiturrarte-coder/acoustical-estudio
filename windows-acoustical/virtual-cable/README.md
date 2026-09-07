# Cable virtual de audio "Acoustical Cable"

Dispositivo de audio virtual para Windows: aparece como una tarjeta de sonido
más (entrada + salida) por donde meter el audio de cualquier programa
(Spotify, navegador, un juego…) dentro de la cadena de corrección de
Acoustical Estudio.

## Base del driver

Se construye a partir del ejemplo oficial **sysvad** de Microsoft (código
abierto, incluido en los ejemplos de drivers de Windows). Renombramos el
dispositivo a "Acoustical Cable" y dejamos un par de endpoints:

- **Salida** "Acoustical Cable Salida": aquí apunta la reproducción de los
  programas; el audio queda disponible para la app.
- **Entrada** "Acoustical Cable Entrada": la app (o Cubase a través del
  bridge) la lee.

## Requisitos

- Windows Driver Kit (WDK): https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk
- Visual Studio 2022 con "Spectre-mitigated libs" del componente C++.

## Compilar

```powershell
powershell -ExecutionPolicy Bypass -File build_cable.ps1
```

El script clona los ejemplos de drivers de Windows, aplica el parche de
renombrado (INF + nombres de endpoints), compila el paquete y lo prepara
en `dist\` para el instalador.

## Firma (una sola vez)

Es un driver de kernel, así que Windows exige firma:

1. El script crea un **certificado de prueba** y firma el paquete
   (test-signing).
2. El instalador añade el certificado al almacén de confianza; Windows
   muestra un aviso y se acepta **una sola vez**.
3. Alternativa sin aviso: firma con un certificado EV real (si dispones de
   uno) o desactiva Secure Boot y activa `bcdedit /set testsigning on`.

## Instalación

```
pnputil /add-driver AcousticalCable.inf /install
```

(desde el instalador o a mano; luego "Acoustical Cable" aparece en la lista
de dispositivos de audio de Windows).
