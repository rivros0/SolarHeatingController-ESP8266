#!/usr/bin/env python3
"""
Applica il profilo MQTT SolarAssistant già verificato al controller ESP8266.

La configurazione viene aggiornata tramite API HTTP Basic + CSRF:
  GET  /api/state
  GET  /api/config
  POST /api/confirm-physical-off
  POST /api/config
  POST /api/reboot
  GET  /api/config e /api/state (verifica finale)

Non modifica il filesystem LittleFS nel suo complesso e non carica firmware.
Non richiede né stampa la password MQTT. Il backend conserva le password
già registrate: il JSON letto tramite GET /api/config le omette.

La conferma fisica OFF compare in una finestra grafica kdialog o zenity.
Se non è disponibile una finestra grafica, il programma rifiuta di
effettuare modifiche. Nessun comando IR viene inviato.

Uso:
  python3 scripts/apply_solarassistant_profile.py --host 192.168.1.242
  python3 scripts/apply_solarassistant_profile.py --check
"""

import argparse
import base64
import copy
import getpass
import json
import os
import shutil
import subprocess
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


# ============================================================================
# PROFILO DOMESTICO: topic realmente pubblicati da SolarAssistant.
# Sette misure operative, import/export non utilizzati.
# ============================================================================
BROKER_HOST = "192.168.1.240"
BROKER_PORT = 1883
TOPICS = {
    "pv": "solar_assistant/inverter_1/pv_power/state",
    "load": "solar_assistant/inverter_1/load_power/state",
    "battery": "solar_assistant/total/battery_power/state",
    "voltage": "solar_assistant/battery_1/voltage/state",
    "current": "solar_assistant/battery_1/current/state",
    "soc": "solar_assistant/total/battery_state_of_charge/state",
    "import": "",
    "export": "",
    "inverter": "solar_assistant/inverter_1/device_mode/state",
}


def create_target_config(existing):
    """Cambia solo MQTT, convenzione batteria e flag automazione."""
    updated = copy.deepcopy(existing)

    updated["mqttHost"] = BROKER_HOST
    updated["mqttPort"] = BROKER_PORT
    updated["discovery"] = ""
    updated["topics"] = copy.deepcopy(TOPICS)

    if "energy" not in updated or not isinstance(updated["energy"], dict):
        raise ValueError("Oggetto energy assente dalla configurazione ESP.")

    updated["energy"]["negativeCharge"] = False
    updated["energy"]["negativeCurrentCharge"] = False
    updated["automationEnabled"] = False

    # Le password NON devono essere inviate al dispositivo. L'API ESP
    # parte da una copia di cfg e preserva tutti i campi omessi.
    for secret in ("mqttPassword", "wifiPassword", "adminPassword"):
        updated.pop(secret, None)

    return updated


def compare_profile(existing, updated):
    """Stampa soltanto campi non sensibili rilevanti."""
    print("\n=== CONFRONTO PROFILO MQTT ===")
    changed = False

    for field in ("mqttHost", "mqttPort", "discovery", "automationEnabled"):
        old = existing.get(field)
        new = updated.get(field)
        different = old != new
        changed = changed or different
        status = "CAMBIA" if different else "OK"
        print("{:<19} {:<7} {!r} -> {!r}".format(field, status, old, new))

    for field in TOPICS:
        old = existing.get("topics", {}).get(field, "")
        new = updated["topics"][field]
        different = old != new
        changed = changed or different
        status = "CAMBIA" if different else "OK"
        print("topics.{:<12} {:<7} {!r} -> {!r}".format(field, status, old, new))

    for field in ("negativeCharge", "negativeCurrentCharge"):
        old = existing.get("energy", {}).get(field)
        new = updated["energy"][field]
        different = old != new
        changed = changed or different
        status = "CAMBIA" if different else "OK"
        print("energy.{:<13} {:<7} {!r} -> {!r}".format(field, status, old, new))

    return changed


def request_json(base_url, path, authorization, csrf="", data=None, timeout=15):
    """Effettua richieste HTTP autorizzate, senza salvare credenziali."""
    body = None
    method = "GET"

    if data is not None:
        method = "POST"
        body = json.dumps(
            data,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")

    request = Request(base_url + path, data=body, method=method)
    request.add_header("Authorization", authorization)
    request.add_header("Accept", "application/json")
    request.add_header("Cache-Control", "no-store")

    if method == "POST":
        request.add_header("Content-Type", "application/json")
        request.add_header("X-CSRF-Token", csrf)

    print("{} {} ...".format(method, path), flush=True)

    try:
        with urlopen(request, timeout=timeout) as response:
            payload = response.read(32768)

        if len(payload) >= 32768:
            raise ValueError("Risposta del dispositivo troppo grande.")

        return json.loads(payload.decode("utf-8"))

    except HTTPError as error:
        details = error.read(512).decode("utf-8", errors="replace")
        if error.code == 401:
            raise RuntimeError(
                "Credenziali della dashboard WEB non corrette (HTTP 401)."
            ) from error
        raise RuntimeError(
            "HTTP {} su {}: {}".format(error.code, path, details)
        ) from error


def require_physical_off_popup(operation):
    """Richiede conferma grafica contestuale prima di ogni modifica."""
    message = (
        "Operazione: " + operation + "\n\n" +
        "Hai verificato personalmente che il climatizzatore sia " +
        "FISICAMENTE SPENTO?\n\n" +
        "Premi SÌ soltanto dopo averlo controllato.\n" +
        "Questa verifica non invia comandi infrarossi."
    )

    if shutil.which("kdialog"):
        command = [
            "kdialog",
            "--title", "SolarHeatingController - sicurezza",
            "--yesno", message,
        ]
    elif shutil.which("zenity"):
        command = [
            "zenity",
            "--question",
            "--title=SolarHeatingController - sicurezza",
            "--text=" + message,
            "--width=450",
        ]
    else:
        raise RuntimeError(
            "Finestra di conferma non disponibile. " +
            "Su Kubuntu installare kdialog con: sudo apt install kdialog. " +
            "Nessuna modifica eseguita."
        )

    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        raise RuntimeError(
            "Sessione grafica non disponibile. Nessuna modifica eseguita."
        )

    result = subprocess.run(command, check=False)
    if result.returncode == 0:
        return True

    print("Conferma OFF annullata. Nessuna ulteriore azione avviata.")
    return False


def physical_off_attestation(base_url, authorization, csrf):
    """Registra sul firmware la dichiarazione fisica OFF dell'operatore."""
    result = request_json(
        base_url, "/api/confirm-physical-off",
        authorization, csrf=csrf, data={"confirmed": True},
    )
    if result.get("confirmed") is not True or result.get("commanded") != "off":
        raise RuntimeError("Il firmware non ha accettato la conferma OFF.")


def print_state(state):
    """Riepilogo senza credenziali e senza token CSRF."""
    print(
        "Firmware={} | Wi-Fi={} | MQTT={} | riavvio_necessario={}".format(
            state.get("version"),
            state.get("wifi"),
            state.get("mqtt"),
            state.get("rebootNeeded"),
        )
    )
    measures = state.get("energy", {})
    for field in ("pv", "load", "battery", "voltage", "current", "soc"):
        metric = measures.get(field, {})
        print(
            "{:<9} valido={:<5} valore={:<10} eta={} ms".format(
                field,
                str(metric.get("valid")),
                str(metric.get("value")),
                metric.get("ageMs"),
            )
        )

    if "mqttSubscriptionsOk" in state:
        print(
            "SUBACK: {}/{} confermate, {} fallite; campioni accettati={}".format(
                state.get("mqttSubscriptionsOk"),
                state.get("mqttSubscriptionsRequested"),
                state.get("mqttSubscriptionsFailed"),
                state.get("mqttAccepted"),
            )
        )


def wait_for_reboot(base_url, authorization, old_uptime):
    """Attende che la configurazione venga riletta dal filesystem."""
    print("\nRiavvio richiesto. Verifica riconnessione ESP...")
    deadline = time.monotonic() + 55

    while time.monotonic() < deadline:
        time.sleep(3)
        try:
            state = request_json(
                base_url, "/api/state", authorization, timeout=5
            )
        except (OSError, URLError, ValueError, RuntimeError):
            continue

        uptime = state.get("uptime")
        if (
            state.get("rebootNeeded") is False
            and isinstance(uptime, (int, float))
            and isinstance(old_uptime, (int, float))
            and uptime < old_uptime
        ):
            return state

    raise RuntimeError(
        "Riavvio non verificato entro il limite di osservazione. " +
        "Controllare l'ESP e rieseguire lo script in modalità --check."
    )


def verify_applied_profile(config):
    """Controlla i valori salvati DOPO il riavvio, non soltanto il POST."""
    expected = create_target_config(config)

    if config.get("mqttHost") != BROKER_HOST:
        return False
    if config.get("mqttPort") != BROKER_PORT:
        return False
    if config.get("discovery") != "":
        return False
    if config.get("topics") != expected.get("topics"):
        return False
    if config.get("automationEnabled") is not False:
        return False
    if config.get("energy", {}).get("negativeCharge") is not False:
        return False
    if config.get("energy", {}).get("negativeCurrentCharge") is not False:
        return False

    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host", default="192.168.1.242",
        help="IP della NodeMCU (default 192.168.1.242)",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Verifica senza applicare modifiche e senza riavviare",
    )
    args = parser.parse_args()

    host = args.host.strip()
    if not host or "/" in host or ":" in host or "@" in host:
        parser.error("--host deve contenere un hostname/IP senza schema.")

    user = input("Utente WEB ESP8266 [admin]: ").strip()
    if not user:
        user = "admin"
    password = getpass.getpass("Password WEB ESP8266: ")

    if not password:
        print("Password vuota: operazione annullata.")
        return 1

    secret = (user + ":" + password).encode("utf-8")
    auth = "Basic " + base64.b64encode(secret).decode("ascii")
    password = ""

    base_url = "http://" + host

    try:
        state = request_json(base_url, "/api/state", auth)
        print_state(state)
        csrf = state.get("csrf")

        if not isinstance(csrf, str) or not csrf:
            raise RuntimeError("Token CSRF non disponibile da /api/state.")

        existing = request_json(base_url, "/api/config", auth)
        updated = create_target_config(existing)
        changes = compare_profile(existing, updated)

        if args.check:
            print("\nModalita' di verifica: nessuna modifica eseguita.")
            return 0

        if not changes:
            print("\nIl profilo MQTT e' gia' configurato correttamente.")
            print("Se la dashboard mostra ancora trattini, eseguire:")
            print("  python3 scripts/diagnose_mqtt.py --host " + host)
            return 0

        if not require_physical_off_popup("Salvare i topic MQTT SolarAssistant"):
            return 1

        physical_off_attestation(base_url, auth, csrf)
        response = request_json(
            base_url, "/api/config", auth, csrf=csrf, data=updated
        )
        if response.get("rebootNeeded") is not True:
            raise RuntimeError("Il firmware non ha confermato il salvataggio.")

        print("\nProfilo salvato. Credenziali Wi-Fi/MQTT/WEB preservate.")
        print("Automazione disabilitata; per applicare le sottoscrizioni serve riavviare.")

        if not require_physical_off_popup(
            "Riavviare il controller e applicare i topic MQTT"
        ):
            print("Riavvio annullato: la configurazione e' salvata.")
            print("Eseguire il riavvio dalla dashboard dopo avere verificato OFF.")
            return 1

        physical_off_attestation(base_url, auth, csrf)
        request_json(base_url, "/api/reboot", auth, csrf=csrf, data={})
        state = wait_for_reboot(base_url, auth, state.get("uptime"))

        confirmed = request_json(base_url, "/api/config", auth)
        if not verify_applied_profile(confirmed):
            raise RuntimeError(
                "Il dispositivo non riporta la configurazione attesa dopo il riavvio."
            )

        print("\n=== PROFILO MQTT SALVATO E VERIFICATO ===")
        print_state(state)
        print(
            "\nAttendere almeno qualche aggiornamento SolarAssistant. " +
            "Se le metriche restano non valide, usare:"
        )
        print("python3 scripts/diagnose_mqtt.py --host " + host)
        return 0

    except (OSError, URLError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print("\nERRORE: " + str(error))
        return 1


if __name__ == "__main__":
    sys.exit(main())
