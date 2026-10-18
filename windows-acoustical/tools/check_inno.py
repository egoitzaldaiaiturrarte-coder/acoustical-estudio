#!/usr/bin/env python3
# check_inno.py - valida las constantes Inno Setup ({...}) de un script .iss.
#
# Inno interpreta {NOMBRE} como constante ({app}, {autopf}, ...). Si el nombre
# no es una constante conocida, ISCC aborta:
#     Error on line N: Unknown constant "NOMBRE".
# Para incrustar un {literal} hay que doblar las llaves: {{literal}}.
# La corrida 7 de v1.9.0 murio exactamente aqui (un CLSID con { ... } sin
# doblar en [Registry]); este cheque se corre en el job dsp antes de quemar
# el job de Windows.
#
# Reglas validadas:
#  1. Todo {{ debe tener su }} (llaves literales balanceadas).
#  2. Todo {...} simple restante es o bien una constante Inno de la
#     whitelist, o bien una constante ISPP {#NOMBRE} definida en el propio
#     script con #define.
#  3. No sobra ningun { o } sin配对 (Inno aborta con "unexpected '}'").
#
# Salida: 0 = OK, 1 = error (con el detalle).
import re
import sys

# Constantes de Inno Setup 6 (https://jrsoftware.org/isinfo.php topics 8-9).
# Si algun dia el script usa una constante real no listada aqui, anadirla
# (un fallo del cheque con nombre valido es un fallo de esta whitelist).
WHITELIST = {
    # directorios
    "app", "userappdata", "userdesktop", "userdocs", "usermusic",
    "userpictures", "uservideos", "userprofile", "username", "temp",
    "windir", "system", "systemx86", "sysnative",
    "commonpf", "commonpf32", "commonpf64", "commonappdata",
    "commondesktop", "commondocs", "commonmusic", "commonpictures",
    "commonvideos", "commonfiles", "autodesktop", "group",
    "cf", "cf32", "cf64", "autopf", "autopf32", "autopf64",
    # varios
    "lang", "sourcepath", "exefile", "uninstall", "partial",
    "setup", "postinstallfile",
    # bloques de version (VersionInfo en [Setup])
    "v1", "v2", "v3", "v4",
}


def main(path):
    text = open(path, encoding="utf-8", errors="replace").read()

    # 1) Quitar los comentarios de linea fuera de [Code] (el [Code] es Pascal:
    #    alli los comentarios son // y (* *), y las llaves literales de Pascal
    #    serian falsos positivos).
    in_code = False
    kept = []
    for line in text.splitlines():
        m = re.match(r"\s*\[(\w+)\]", line)
        if m:
            in_code = m.group(1) == "Code"
        if in_code:
            kept.append(line)
        elif not line.lstrip().startswith(";"):
            kept.append(line)
    text = "\n".join(kept)

    # 2) #define del propio script (para validar {#NOMBRE}).
    defines = set(re.findall(r"#define\s+(\w+)", text))

    errors = []

    # 3) Llaves literales {{ }}: han de balancearse; se sustituyen para que
    #    el paso siguiente no las vea como constantes.
    if text.count("{{") != text.count("}}"):
        errors.append(
            "llaves literales desbalanceadas: hay %d '{{' pero %d '}}'"
            % (text.count("{{"), text.count("}}")))
    text = text.replace("{{", "\x00").replace("}}", "\x00")

    # 4) Constantes simples {NOMBRE} restantes.
    for m in re.finditer(r"\{([^{}]*)\}", text):
        inner = m.group(1)
        if inner.startswith("#"):
            if inner[1:] not in defines:
                errors.append(
                    "constante ISPP {#%s} sin #define en el script" % inner[1:])
        elif inner not in WHITELIST:
            errors.append(
                'constante Inno desconocida {%s} — si es un valor literal '
                "(un CLSID, un GUID, ...), doblar las llaves: {{%s}}"
                % (inner, inner))

    # 5) Cualquier { o } suelto que no encaje en lo de arriba rompe el parseo.
    leftover = re.sub(r"\{[^{}]*\}", "", text)
    for ch in ("{", "}"):
        if ch in leftover:
            errors.append("llave '%s' sin su par (Inno aborta al parsear)" % ch)

    if errors:
        print("check_inno: %s — %d problema(s):" % (path, len(errors)))
        for e in errors:
            print("  - " + e)
        return 1
    print("check_inno: %s OK (constantes Inno validas)" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
