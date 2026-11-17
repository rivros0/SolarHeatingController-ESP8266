# -*- coding: utf-8 -*-
"""PlatformIO pre-build: incorpora solo CA pubbliche attendibili nel firmware.

Le CA sono lette dall'archivio di sistema, NON da GitHub e NON dal dispositivo.
In mancanza di certificati attendibili la compilazione termina con errore:
non viene mai usato TLS in modalita' setInsecure().
"""
from pathlib import Path

Import("env")

ROOTS = (
    "ISRG_Root_X1",
    "Sectigo_Public_Server_Authentication_Root_E46",
    "Sectigo_Public_Server_Authentication_Root_R46",
    "DigiCert_Global_Root_CA",
)

def prepare_ca_header():
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    output = project / "include" / "github_ca_roots.h"
    certificates = []

    for name in ROOTS:
        candidates = [
            Path("/etc/ssl/certs") / (name + ".pem"),
            project / "scripts" / "ca" / (name + ".pem"),
        ]
        found = False
        for candidate in candidates:
            if candidate.is_file():
                pem = candidate.read_text(encoding="ascii").strip()
                if pem.startswith("-----BEGIN CERTIFICATE-----") and pem.endswith("-----END CERTIFICATE-----"):
                    certificates.append(pem)
                    found = True
                    break
        if not found:
            raise RuntimeError(
                "CA di sistema assente: " + name +
                ". Su Windows copiare i PEM pubblici verificati nella cartella scripts/ca/. "
                "Non disabilitare la verifica TLS."
            )
    body = "\\n".join(certificates) + "\\n"
    code = (
        "#pragma once\\n"
        "// Generato automaticamente dalle CA di sistema. Non modificare.\\n"
        "#include <Arduino.h>\\n"
        "static const char GH_TLS_CA_PEM[] PROGMEM = R\"GHCERT(\\n"
        + body +
        ")GHCERT\";\\n"
    )
    output.write_text(code.replace("\\n", "\n"), encoding="ascii")
    print("CA firmware: " + str(len(certificates)) + " trust anchors verificabili")

prepare_ca_header()
