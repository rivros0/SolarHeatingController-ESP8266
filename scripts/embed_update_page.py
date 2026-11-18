# -*- coding: utf-8 -*-
"""
PlatformIO pre-build: incorpora la pagina /update direttamente nel firmware.

Questo evita di dover sovrascrivere LittleFS quando cambia la procedura OTA.
Il contenuto HTML resta sorgente unico in data/update.html, mentre il file
include/generated_update_page.h e' creato automaticamente prima di compilare.
Non vengono incorporati dati personali, credenziali o file di configurazione.
"""
from pathlib import Path

Import("env")


def generate_update_page():
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    source = project / "data" / "update.html"
    destination = project / "include" / "generated_update_page.h"

    # La pagina deve essere leggibile e di dimensione contenuta per ESP8266.
    html = source.read_text(encoding="utf-8")
    if len(html.encode("utf-8")) > 32000:
        raise RuntimeError("Pagina OTA troppo grande per il firmware embedded.")

    delimiter = "OTAHTML"
    if ")" + delimiter + '"' in html:
        raise RuntimeError("La pagina OTA contiene il delimitatore C++ riservato.")

    header = (
        "#pragma once\n"
        "// Pagina OTA generata da data/update.html; non modificare a mano.\n"
        "#include <Arduino.h>\n"
        "static const char UPDATE_PAGE_HTML[] PROGMEM = R\"" + delimiter + "(\n"
        + html +
        "\n)" + delimiter + "\";\n"
    )

    if not destination.is_file() or destination.read_text(encoding="utf-8") != header:
        destination.write_text(header, encoding="utf-8")

    print("Pagina OTA incorporata nel firmware: " + str(len(html.encode("utf-8"))) + " byte")


generate_update_page()
