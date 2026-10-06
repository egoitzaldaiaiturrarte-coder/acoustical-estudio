# Keystore de AcoustiCal

## ¿Por qué este directorio no está en git?

El keystore es la **identidad de firma** de la app: con él (y sus passwords)
cualquiera puede firmar APKs que Android aceptará como "la misma app", e
instalarse encima de la que ya llevan tus usuarios. Por eso:

- `acoustical.jks` **ya no se versiona** (se sacó del índice con
  `git rm --cached`; sigue existiendo en el historial de versiones pasadas,
  así que el repo debe mantenerse privado).
- Los passwords de firma (store/key) **no se escriben en `build.gradle.kts`**:
  se leen de `local.properties` (este directorio) o de variables de entorno.
- `local.properties` y `keystore/` están en el `.gitignore` de la raíz.

## Cómo configurar tu copia (o la CI)

Opción A — `local.properties` en la raíz de `android-acoustical/`:

```properties
acoustical.storePassword=MI_PASSWORD_DE_STORE
acoustical.keyPassword=MI_PASSWORD_DE_KEY
```

Opción B — variables de entorno (recomendada para CI):

```
AcousticalStorePassword=MI_PASSWORD_DE_STORE
AcousticalKeyPassword=MI_PASSWORD_DE_KEY
```

Comportamiento del build:

| Password | `keystore/acoustical.jks` | Resultado |
|----------|---------------------------|-----------|
| ✅ | ✅ | Firma normal (debug + release) |
| ✅ | ❌ | `logger.warn`: compila **sin firma de release** |
| ❌ | ✅ | `GradleException`: hay keystore pero sin password |
| ❌ | ❌ | `GradleException` con estas instrucciones |

## Listar / verificar el keystore (keytool)

```bash
# Listar alias, tipo de clave, huellas y validez
keytool -list -v -keystore acoustical.jks

# Exportar solo el CERTIFICADO (no la clave) para verificar huellas
keytool -exportcert -alias acoustical -keystore acoustical.jks \
    -file acoustical.cer
```

> **Advertencia:** exportar el certificado NO respalda la clave privada. El
> único respaldo real del keystore es **copiar el fichero `.jks`** (y
> memorizar/escribir en un gestor de passwords sus dos passwords). Guárdalo
> fuera del equipo de trabajo (disco cifrado / vault) — no en el repo.

## Si el keystore se pierde: cómo regenerarlo

```bash
keytool -genkeypair -alias acoustical \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Acoustical, OU=Audio, O=Rork, L=Madrid, C=ES" \
    -keystore acoustical.jks \
    -storepass <NUEVO_PASSWORD> -keypass <NUEVO_PASSWORD>
```

**Consecuencia (irreversible):** con una firma nueva, **los dispositivos con
la versión anterior instalada NO podrán actualizarse encima** — Android
rechaza el paquete ("el paquete no está bien") y habría que desinstalar la
app antes de instalar la nueva. Por eso la pérdida del keystore se trata
como incidente grave: antes de regenerar, agotar todas las vías de
recuperación (respaldos, historial del repo privado, equipos antiguos).
