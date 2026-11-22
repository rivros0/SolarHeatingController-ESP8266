#!/usr/bin/env python3
"""
Diagnostica MQTT SolarHeatingController senza scrittura sulla memoria ESP8266.

Interroga esclusivamente:
    GET /api/config
    GET /api/state
    GET /api/diagnostics

Le credenziali dell'interfaccia web sono richieste interattivamente,
non vengono salvate né stampate. Non modifica broker, topic, IR o clima.

Esempio:
    python3 scripts/diagnose_mqtt.py --host 192.168.1.242
"""

import argparse
import base64
import getpass
import json
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


# ---------------------------------------------------------------------------
# Topic verificati sul broker SolarAssistant domestico.
# Le corrispondenze sono ESATTE: l'ESP8266 non usa wildcard per le metriche.
# ---------------------------------------------------------------------------
EXPECTED_HOST = "192.168.1.240"
EXPECTED_TOPICS = {
    "pv": "solar_assistant/inverter_1/pv_power/state",
    "load": "solar_assistant/inverter_1/load_power/state",
    "battery": "solar_assistant/total/battery_power/state",
    "voltage": "solar_assistant/battery_1/voltage/state",
    "current": "solar_assistant/battery_1/current/state",
    "soc": "solar_assistant/total/battery_state_of_charge/state",
    "inverter": "solar_assistant/inverter_1/device_mode/state",
    "import": "",
    "export": "",
}


def request_json(base_url, endpoint, authorization, timeout):
    """
    Esegue una lettura HTTP in sola lettura, mostrando il percorso richiesto.

    Se la connessione va in timeout, la stampa precedente permette di
    distinguere il blocco del webserver da un problema dei topic MQTT.
    """
    request = Request(base_url + endpoint, method="GET")
    request.add_header("Authorization", authorization)
    request.add_header("Accept", "application/json")
    request.add_header("Cache-Control", "no-store")

    print("GET {} ...".format(endpoint), flush=True)
    started = time.monotonic()

    try:
        with urlopen(request, timeout=timeout) as response:
            payload = response.read(131072)
            if len(payload) >= 131072:
                raise ValueError("Risposta JSON troppo grande.")

            data = json.loads(payload.decode("utf-8"))
            elapsed_seconds = time.monotonic() - started
            print(
                "OK  {}: {} byte in {:.1f} s".format(
                    endpoint, len(payload), elapsed_seconds
                ),
                flush=True,
            )
            return data
    except HTTPError as error:
        print(
            "HTTP {} su {}. Verificare l'utente web e la password WEB.".format(
                error.code, endpoint
            ),
            flush=True,
        )
        raise
    except (TimeoutError, URLError, OSError) as error:
        print(
            "ERRORE CONNESSIONE su {}: {}".format(endpoint, str(error)),
            flush=True,
        )
        raise


def human_flag(value):
    if value:
        return "SI"
    return "NO"


def get_metric(state, name):
    energy = state.get("energy", {})
    return energy.get(name, {})


def show_snapshot(state, label):
    print("\n=== " + label + " ===")
    print("Firmware:", state.get("version", "n/d"))
    print("Wi-Fi:", human_flag(state.get("wifi")))
    print("MQTT:", human_flag(state.get("mqtt")))
    print("Configurazione in attesa di riavvio:",
          human_flag(state.get("rebootNeeded")))
    print("Messaggi MQTT rifiutati:", state.get("mqttRejected", "n/d"))
    print("Messaggi retained ignorati:", state.get("retainedIgnored", "n/d"))
    print("Tempo dall'ultimo topic riconosciuto (ms):",
          state.get("lastMqttAgeMs", "n/d"))

    for name in ("pv", "load", "battery", "voltage", "current", "soc"):
        measurement = get_metric(state, name)
        print(
            "{:<9} valido={:<3} valore={:<9} età={} ms".format(
                name,
                human_flag(measurement.get("valid")),
                str(measurement.get("value")),
                measurement.get("ageMs", "n/d"),
            )
        )


def show_configuration(config):
    print("\n=== CONFIGURAZIONE PERSISTENTE ===")
    print("Broker:", config.get("mqttHost", "n/d"))
    print("Porta:", config.get("mqttPort", "n/d"))
    print("Filtro diagnostico:", config.get("discovery", "n/d"))
    print("Automazione abilitata:", human_flag(config.get("automationEnabled")))
    energy = config.get("energy", {})
    print("Dati scaduti dopo (ms):", energy.get("staleMs", "n/d"))
    print("Potenza negativa = carica:",
          human_flag(energy.get("negativeCharge")))
    print("Corrente negativa = carica:",
          human_flag(energy.get("negativeCurrentCharge")))

    mismatches = []
    topics = config.get("topics", {})
    for name, expected in EXPECTED_TOPICS.items():
        actual = topics.get(name, "")
        status = "OK"
        if actual != expected:
            status = "DIVERSO"
            mismatches.append(name)
        print("{:<9} {:<8} {}".format(name, status, actual))

    return mismatches


def show_seen_topics(diagnostics):
    print("\n=== ULTIMI TOPIC VISIBILI NELLA DIAGNOSTICA ===")
    seen = diagnostics.get("topics", [])
    if not seen:
        print("Nessun topic nel buffer diagnostico.")
        return

    # Il firmware conserva soltanto 12 topic. L'assenza di una metrica
    # NON prova da sola che non siano arrivati messaggi.
    for row in seen:
        topic = row.get("topic", "")
        if not topic.startswith("solar_assistant/"):
            continue
        print(
            "{} = {}  retained={}  eta={} ms".format(
                topic,
                row.get("value", ""),
                human_flag(row.get("retained")),
                row.get("ageMs", "n/d"),
            )
        )


def number_or_none(value):
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return value
    return None


def explain(config, first, second, mismatches):
    print("\n=== VALUTAZIONE ===")
    if config.get("mqttHost") != EXPECTED_HOST:
        print("ATTENZIONE: il broker configurato non coincide con quello testato.")
    if mismatches:
        print("CONFIG: topic differenti da quelli verificati:",
              ", ".join(mismatches))
        print("Applicare il profilo SolarAssistant e riavviare l'ESP.")
    if second.get("rebootNeeded"):
        print("CONFIG: alcune impostazioni non sono ancora attive: riavvio richiesto.")

    if not second.get("wifi"):
        print("RETE: ESP scollegato dal Wi-Fi.")
    elif not second.get("mqtt"):
        print("MQTT: ESP non collegato al broker. Verificare host, porta,")
        print("credenziali e raggiungibilita' del broker dalla sua rete.")
    else:
        print("MQTT: connessione TCP/MQTT al broker riuscita.")
        print("NOTA: 'connesso' non dimostra che le SUBSCRIBE siano confermate.")

    first_reject = number_or_none(first.get("mqttRejected"))
    second_reject = number_or_none(second.get("mqttRejected"))
    first_retained = number_or_none(first.get("retainedIgnored"))
    second_retained = number_or_none(second.get("retainedIgnored"))

    if first_reject is not None and second_reject is not None:
        delta = second_reject - first_reject
        print("Messaggi rifiutati durante la prova:", delta)
        if delta > 0:
            print("Analizzare il formato dei payload e i limiti del parser.")

    if first_retained is not None and second_retained is not None:
        print("Retained ignorati durante la prova:",
              second_retained - first_retained)

    valid_metrics = []
    for name in ("pv", "load", "battery", "voltage", "current", "soc"):
        if get_metric(second, name).get("valid"):
            valid_metrics.append(name)

    if valid_metrics:
        print("METRICHE VALIDE:", ", ".join(valid_metrics))
        if len(valid_metrics) == 6:
            print("Le sei metriche principali sono disponibili via API.")
            print("Se la dashboard resta vuota, verificare JavaScript/cache.")
    elif second.get("mqtt") and not mismatches and not second.get("rebootNeeded"):
        print("ANOMALIA: MQTT connesso e topic configurati, ma nessuna metrica valida.")
        print("Controllare la ricezione e le conferme SUBACK nel firmware.")
        print("Potrebbe servire strumentare onSubscribe/onMessage per")
        print("distinguere sottoscrizione negata, payload ignorato e scadenza.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.1.242",
                        help="IP o hostname dell'ESP8266 (default 192.168.1.242)")
    parser.add_argument("--seconds", type=int, default=12,
                        help="Secondi tra le due letture (default 12)")
    parser.add_argument("--timeout", type=int, default=25,
                        help="Timeout HTTP per ciascuna richiesta (default 25 s)")
    parser.add_argument(
        "--quick", action="store_true",
        help="Legge soltanto /api/state per testare il webserver in pochi secondi"
    )
    args = parser.parse_args()

    if args.seconds < 1 or args.seconds > 120:
        parser.error("--seconds deve essere compreso tra 1 e 120")
    if args.timeout < 2 or args.timeout > 90:
        parser.error("--timeout deve essere compreso tra 2 e 90")

    host = args.host.strip()
    if not host or "/" in host or "@" in host or ":" in host:
        parser.error("--host deve contenere soltanto IP o nome host")

    base_url = "http://" + host
    username = input("Utente web ESP8266: ").strip()
    password = getpass.getpass("Password web ESP8266: ")
    if not username or not password:
        parser.error("Credenziali web obbligatorie")

    secret = (username + ":" + password).encode("utf-8")
    authorization = "Basic " + base64.b64encode(secret).decode("ascii")
    password = ""

    try:
        # La verifica rapida esclude volutamente la lettura di /api/config:
        # consente di diagnosticare un eventuale blocco di quella rotta.
        if args.quick:
            state = request_json(
                base_url, "/api/state", authorization, args.timeout
            )
            show_snapshot(state, "CONTROLLO RAPIDO")
            return 0

        config = request_json(
            base_url, "/api/config", authorization, args.timeout
        )
        first = request_json(
            base_url, "/api/state", authorization, args.timeout
        )
        show_configuration(config)
        show_snapshot(first, "PRIMA LETTURA")

        print("\nOsservazione live per {} secondi...".format(args.seconds))
        time.sleep(args.seconds)

        second = request_json(
            base_url, "/api/state", authorization, args.timeout
        )
        show_snapshot(second, "SECONDA LETTURA")
        try:
            diagnostics = request_json(
                base_url, "/api/diagnostics", authorization, args.timeout
            )
            show_seen_topics(diagnostics)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print("Diagnostica topic non disponibile:", str(error))

        mismatches = []
        for name, expected in EXPECTED_TOPICS.items():
            if config.get("topics", {}).get(name, "") != expected:
                mismatches.append(name)
        explain(config, first, second, mismatches)

    except HTTPError as error:
        if error.code == 401:
            print(
                "ERRORE 401: l'ESP ha risposto, ma le credenziali WEB "
                "non sono state accettate. "
                "Verificare l'utente configurato (non necessariamente admin)."
            )
        else:
            print(
                "ERRORE HTTP {}: verificare risposta del dispositivo.".format(
                    error.code
                )
            )
        return 1
    except (URLError, OSError, ValueError, json.JSONDecodeError) as error:
        print("ERRORE DI LETTURA:", str(error))
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
