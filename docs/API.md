# Configurazione e API locali

Tutti gli endpoint, inclusi gli asset, richiedono HTTP Basic. Le mutazioni POST richiedono anche `X-CSRF-Token`, ottenuto da `GET /api/state` dopo autenticazione. Non includere credenziali in URL, log o repository. Il token cambia al boot. Non vi è API di lettura delle password.

| Metodo | Percorso | Uso |
|---|---|---|
| GET | `/` | Dashboard LittleFS |
| GET | `/api/state` | Misure, validità/età, stato, versione, heap, token CSRF |
| GET / POST | `/api/config` | Configurazione redatta / modifica versionata |
| POST | `/api/mode` | `{"mode":"auto"}`; alternative `force_on`, `force_off`, `diagnostic` |
| GET | `/api/history` | Fino a 60 campioni RAM, JSON inviato in streaming |
| GET | `/api/diagnostics` | Fino a 16 eventi e 12 topic osservati |
| GET / POST | `/api/ir` | Cattura corrente e slot / prove e salvataggio |
| POST | `/api/learn` | Apprendimento: `baseline`, `on`, `off`, `save`, `cancel` |
| POST | `/api/reboot` | `{}`; richiede stato fisico confermato OFF |
| GET / POST | `/update` | Pagina OTA / multipart firmware `.bin` |

Risposte: 400 richiesta/config non valida; 401 autenticazione; 403 token; 409 stato/procedura incompatibile; 413 corpo eccessivo; 503 configurazione/memoria non disponibile; 507 scrittura configurazione fallita.

## Configurazione

Richiesta JSON con `version: 2` (obbligatorio), campi omessi mantenuti dal valore esistente. Password assenti vengono conservate; `mqttPassword:""` le cancella. Il frontend conserva password lasciate vuote ed espone una casella per cancellare quella MQTT. Modifiche salvate in modo coerente e applicate soltanto al riavvio. Prima di modificare occorre spegnere il climatizzatore se era comandato ON. Il dispositivo continua con i parametri precedenti fino al riavvio, ma blocca nuovi avvii dopo il salvataggio.

Campi principali:

| Campo | Default | Vincolo |
|---|---|---|
| `ssid`, `wifiPassword` | vuoti | max 32/63 caratteri |
| `adminUser` | admin | 1–32 |
| `adminPassword` | casuale al primo avvio | 12–63 |
| `mqttHost` | vuoto | max 128 |
| `mqttPort`, `mqttKeepalive` | 1883, 20 s | 1–65535; 5–300 s |
| `mqttUser`, `mqttPassword` | vuoti | max 64/128 |
| `discovery` | solar_assistant/# | filtro max 128, vuoto disabilita |
| `topics` | nove stringhe vuote | chiavi pv/load/battery/voltage/current/soc/import/export/inverter; topic esatti distinti, max 128 |
| `automationEnabled` | false | consenso esplicito; richiede pv/load/battery associati |
| `protocol` | RAW | RAW, KELON, WHIRLPOOL_AC |
| `powerKind` | unverified | unverified, discrete, toggle; protocolli nativi solo toggle |
| `irVerified` | false | true solo con RAW discrete e due slot distinti |
| `receiver` | false | modifica applicata al riavvio |
| `model`, `fan` | 1, auto | id modello 1/2 della libreria; auto/low/medium/high |
| `setpoint` | 22 °C | 16–30 e almeno tempOff; RAW richiede cattura coerente |
| `sampleMs`, `filterAlpha` | 5000 ms, 0.25 | 1000–30000 ms; 0.01–1 |
| `historyMs` | 30000 ms | 10000–3600000 ms |
| `forceOnMs` | 3600000 ms | 60000–86400000 ms |

`energy`:

- `tempOn:19` (5–29 °C), `tempOff:21` (almeno tempOn+0.5, max 35 e <= setpoint).
- `reserveW:500` (0–20000), `climateW:1200` (100–10000), `dischargeW:100` (0–5000).
- `minVoltage:0` (0–200 V), `maxDischargeA:0` (0–500 A): 0 disabilita il criterio; >0 richiede il topic corrispondente valido.
- `negativeCharge:false`: negativo = carica se true, riferito alla potenza batteria.
- `negativeCurrentCharge:false`: la convenzione del segno della corrente è indipendente.
- `stableMs:600000` (10000–7200000), `dropMs:300000` (10000–3600000).
- `restartMs:600000` (60000–7200000), `minRunMs:300000` (0–7200000).
- `staleMs:30000` (10000–300000 e almeno due periodi sensore).

`detector`: `baselineMs:60000` (10000–600000), `startupMs:180000` (0–900000), `observationMs:120000` (10000–600000), `deltaW:400` (50–10000), `maxNoiseW:200` (1–5000).

`irFingerprint` è un campo interno derivato dagli slot al salvataggio della verifica IR; non è una firma crittografica e non va modificato manualmente.

`modeOnBoot:"force_off"` è un'informazione fissa e non modifica la politica di avvio sicuro. L'ultima modalità esplicitamente richiesta è in `/mode.txt` e nello stato `lastRequestedMode`; non viene ripresa automaticamente. Non ci sono scritture flash periodiche dello stato.

## Esempi operativi

Esempio parziale di configurazione, **sostituire i topic in base al proprio inverter**:

```json
{
  "version": 2,
  "mqttHost": "192.168.1.20",
  "topics": {
    "pv": "solar_assistant/inverter_1/pv_power/state",
    "load": "solar_assistant/inverter_1/load_power/state",
    "battery": "solar_assistant/battery_1/power/state"
  },
  "energy": { "negativeCharge": false },
  "automationEnabled": false
}
```

Questi topic sono esempi di struttura: non garantiscono l'esistenza né la corretta aggregazione dei dati SolarAssistant. Ricezione solo scalare, massimo 127 byte, messaggi frammentati rifiutati. FV/carichi/import/export 0–100000 W, batteria ±100000 W, tensione 0–200 V, corrente ±1000 A, SOC 0–100. NaN/Inf, stringhe non numeriche e suffissi di unità invalidano il dato. Le soglie di plausibilità non sostituiscono la verifica fisica del mapping.

Il broker MQTT può pubblicare senza dipendere da Internet. Il flag retained dei messaggi ricevuti è disponibile in AsyncMqttClient, motivo della scelta rispetto a callback che non lo espongono. Un retained viene mostrato in diagnostica ma non aggiorna alcuna metrica decisionale. All'arrivo di nuovi messaggi live sul topic, la relativa misura diventa valida con timestamp locale. Non c'è inferenza sull'età del campione a monte: la fonte deve pubblicare misure correnti; un publisher che ritrasmette vecchi numeri come live non è rilevabile dal solo payload scalare.

Diagnostica, conferma stato osservato (nessun IR):

```json
{"action":"confirm_state","on":false,"confirmed":true}
```

Prova singola da stato fisico osservato:

```json
{"action":"on","actualOn":false,"confirmed":true}
```

Per RAW discrete è possibile ripetere esplicitamente un test dallo stesso stato (`actualOn:true` con `action:on`) per verificarne l'idempotenza. Per toggle occorre lo stato opposto. Le trasmissioni hanno un intervallo minimo di 2 s. Nessun test viene ripetuto automaticamente e una stima incerta non attiva comandi.

Salvataggio cattura: `{"action":"capture_on","confirmed":true}` oppure `capture_off`. Richiede ricevitore attivo e ultima cattura valida. Non salvare lo slot con il clima comandato ON. Un nuovo codice disabilita verifica e automazione; verificare e salvare nuovamente i parametri. La portante è 38 kHz, senza autodetezione. RAW riproduce HEAT/setpoint/ventola incorporati: modificare i campi dashboard non riscrive i bit di un segnale RAW. Occorre una nuova cattura coerente.

Apprendimento: `{"action":"baseline"}`, poi `{"action":"on","confirmed":true}`, `off` con conferma, infine `save`. L'utente deve verificare fisicamente lo stato prima di proseguire. `cancel` annulla il processo. Il salvataggio richiede incremento e riduzione >=100 W, rumore <= metà del minore dei due; aggiorna potenza prevista, soglia delta e rumore, e ritardo avvio se rilevato. La stima rimane probabilistica.

## Export e accesso

`GET /api/config` esclude wifiPassword, mqttPassword e adminPassword. Non è un backup completo delle credenziali. `GET /api/diagnostics` non legge né serializza credenziali: valori/topic sono quelli inviati dal publisher, quindi limitare il filtro diagnostico a dati energetici. Lo script di storico interroga soltanto history; l'utente sceglie dove conservarlo. L'HTTP sulla LAN richiede protezione della rete o un gateway con TLS.
