# Configurazione e API locali

Tutti gli endpoint, inclusi gli asset, richiedono HTTP Basic. Le mutazioni POST richiedono anche `X-CSRF-Token`, ottenuto da `GET /api/state` dopo autenticazione. Non includere credenziali in URL, log o repository. Il token cambia al boot. Non vi è API di lettura delle password.

| Metodo | Percorso | Uso |
|---|---|---|
| GET | `/` | Dashboard LittleFS |
| GET | `/api/state` | Misure, validità/età, stato, versione, heap, token CSRF |
| GET / POST | `/api/config` | Configurazione redatta / modifica versionata |
| POST | `/api/mode` | `{"mode":"auto"}`; alternative `force_on`, `force_off`, `diagnostic` |
| GET | `/api/history` | Fino a 120 campioni RAM, JSON inviato in streaming |
| GET | `/api/diagnostics` | Fino a 16 eventi e 12 topic osservati |
| GET / POST | `/api/ir` | Cattura corrente e slot / prove e salvataggio |
| POST | `/api/learn` | Apprendimento: `baseline`, `on`, `off`, `save`, `cancel` |
| POST | `/api/reboot` | `{}`; richiede stato fisico confermato OFF |
| GET / POST | `/update` | Pagina OTA / multipart firmware `.bin` |
| POST | `/api/confirm-physical-off` | `{"confirmed":true}`; conferma OFF generale da popup in qualsiasi scheda, senza inviare IR né cambiare modalità |
| POST | `/api/update-confirm-off` | `{"confirmed":true}`; registra la conferma fisica OFF prima di OTA, porta in Forza OFF senza inviare IR |
| GET / POST | `/api/github-update` | Controlla e aggiorna separatamente firmware e asset web statici verificati SHA256, dopo conferma OFF |

Risposte: 400 richiesta/config non valida; 401 autenticazione; 403 token; 409 stato/procedura incompatibile; 413 corpo eccessivo; 503 configurazione/memoria non disponibile; 507 scrittura configurazione fallita.

## Profilo MQTT preconfigurato (v0.3.5)

`data/app.js` offre il pulsante **Applica profilo MQTT SolarAssistant**.
Il profilo usa il broker `192.168.1.240:1883`, i sette
topic osservati nel log (PV `inverter_1/pv_power`, carichi
`inverter_1/load_power`, batteria `total/battery_power`, tensione
e corrente `battery_1`, SOC `total/battery_state_of_charge`
e stato inverter `inverter_1/device_mode`).

Il pulsante prepara una copia della configurazione attuale,
aggiorna soltanto broker, porta, filtro diagnostico, mappatura topic,
convenzione segni batteria, e disattiva l'automazione. Con conferma
fisica OFF, invia l'ordinaria `POST /api/config` con token CSRF.
Poiche' il JSON letto da `/api/config` non contiene le password, il
firmware conserva le password salvate quando quei campi sono omessi.
Il riavvio e' necessario per rinnovare connessione e sottoscrizioni.

I valori iniziali del firmware in `src/config.cpp` riportano lo
stesso profilo, ma sono applicati **soltanto se non esiste**
`/config.json` o `/config.bak` valido. Aggiornare il firmware
da solo **non altera** i topic gia' salvati; usare il pulsante
per applicare il profilo a una configurazione esistente.

La potenza e corrente batteria negative sono scarica: entrambe
le opzioni `negativeCharge` e `negativeCurrentCharge` restano
`false`. Import/export non vengono mappati automaticamente dalla
sola `grid_power` senza convalida della convenzione del segno.
Se il broker richiede credenziali, devono essere gia' presenti
e valide nella configurazione locale. Non inserirle nel repository.

## Conferma OFF contestuale, senza cambio scheda (v0.3.4)

Quando l'utente preme un pulsante che richiede stato fisico OFF,
il frontend propone immediatamente un popup contestuale.
Dopo consenso esplicito invia
`POST /api/confirm-physical-off` con
`{"confirmed":true}`, HTTP Basic e header CSRF come per tutte
le mutazioni. La risposta e' `{"commanded":"off","confirmed":true}`.
Il server registra che l'operatore ha osservato lo stato fisico OFF;
**non effettua misure indipendenti e non invia IR**.
L'endpoint non e' disponibile durante un aggiornamento OTA in corso.

La cancellazione del popup non genera POST e non avvia l'operazione.
Gli endpoint di reboot, configurazione, salvataggio RAW e modalita'
conservano ciascuno le proprie verifiche lato server: una dichiarazione
OFF non rende automaticamente leciti comandi con IR non verificato,
modalita' non consentite o dati energetici non sicuri. La conferma
OTA preesistente continua a usare `/api/update-confirm-off`, che
passa anche in ForceOff.

## Conferma fisica prima dell'aggiornamento OTA

Nella pagina `/update`, sia il pulsante **Installa da GitHub** sia il
caricamento manuale `.bin` mostrano un popup che chiede all'operatore
di verificare **personalmente** che il climatizzatore sia spento.
Solo dopo conferma si invia `POST /api/update-confirm-off` con
`{"confirmed":true}`, autenticazione HTTP Basic e token CSRF.

La conferma **non** invia comandi IR. Il controller registra lo stato
fisico dichiarato come OFF e passa a `ForceOff` per impedire avvii durante
la manutenzione. Annullare il popup non registra alcuna modifica e non
avvia l'installazione. Rimangono anche i controlli firmware indipendenti
nelle rotte di aggiornamento (`/api/github-update` e `/update`).
La conferma vale solo finche' il controller rimane in esecuzione;
dopo ogni riavvio lo stato fisico torna sconosciuto.

**Distribuzione:** il sorgente del popup e' in `data/update.html`
e dalla versione 0.3.1 la pagina `/update` e' servita direttamente
dal firmware (PROGMEM generato nello step pre-build). Il flash USB
aggiorna la pagina OTA senza modificare LittleFS. Dalla **0.3.2**
la dashboard principale resta su LittleFS, ma e' aggiornabile
**anche dalla funzione OTA GitHub** conservando dati privati.

## Aggiornamento OTA incrementale (firmware 0.3.2)

`GET /api/github-update` restituisce `checked`, `version`,
`installed`, `board`, `size` e i nuovi campi:

| Campo | Significato |
| --- | --- |
| `available` | Nuovo firmware **oppure** nuovi asset web disponibili |
| `firmwareAvailable` | Versione firmware della release superiore a quella locale |
| `webSupported` | Manifest con metadati web completi |
| `webAvailable` | Almeno uno dei tre file web differisce per dimensioni o SHA256 |
| `webFilesToUpdate` | Numero di file da scaricare (0–3) |

Dopo autenticazione e conferma OFF, `POST /api/github-update` esegue
solo le operazioni necessarie e restituisce `installed`, `version`,
`firmwareInstalled`, `webInstalled`, `rebooting`.

L'OTA web modifica **esclusivamente** `/index.html`, `/app.js` e
`/style.css`, con nomi fissati nel firmware. Il manifest contiene
SHA256 e dimensioni, ma non impone percorsi locali arbitrari.
L'installazione passa per file temporanei verificati prima della
sostituzione: non formatta LittleFS e conserva credenziali e codici IR.
Una perdita di alimentazione durante la promozione puo' richiedere di
ripetere il controllo OTA per completare gli asset rimanenti.
Una modifica della sola dashboard non obbliga a riavviare l'ESP8266.

Il caricamento manuale `POST /update` resta limitato al firmware
`.bin` e non aggiorna i file web. Per il passaggio da 0.3.1 a 0.3.2 e' sufficiente anche l'OTA
GitHub gia' presente nella 0.3.1: prima si installa il firmware nuovo,
poi si ripete la verifica per i file web (non riconosciuti dalla 0.3.1).
Se la rete e' instabile, resta preferibile il flash USB.

## Configurazione

Richiesta JSON con `version: 2` (obbligatorio), campi omessi mantenuti dal valore esistente. Password assenti vengono conservate; `mqttPassword:""` le cancella. Il frontend conserva password lasciate vuote ed espone una casella per cancellare quella MQTT. Modifiche salvate in modo coerente e applicate soltanto al riavvio. Prima di modificare occorre spegnere il climatizzatore se era comandato ON. Il dispositivo continua con i parametri precedenti fino al riavvio, ma blocca nuovi avvii dopo il salvataggio.

Campi principali:

| Campo | Default | Vincolo |
|---|---|---|
| `ssid`, `wifiPassword` | vuoti | max 32/63 caratteri |
| `staticIp`, `staticGateway`, `staticSubnet` | vuoti = DHCP | IPv4 validi e gateway nella sottorete |
| `staticDns1`, `staticDns2` | vuoti | IPv4 validi; DNS2 richiede DNS1 |
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
| `humidityAssist` in `energy` | false | consenso esplicito all'assistenza UR in HEAT |
| `humidityOn` in `energy` | 60 % | richiesta UR elevata: 3 punti sopra humidityOff, max 85 |
| `humidityOff` in `energy` | 55 % | rientro: 30–70 % |
| `historyMs` | 30000 ms | 10000–3600000 ms |
| `forceOnMs` | 3600000 ms | 60000–86400000 ms |

**UR e storico:** `GET /api/state` include `humidityAssist`,
`humidityDemand` e `humidityOnlyRun`. I nuovi campi in `energy`
sono opzionali per compatibilita' con i file di configurazione v2
esistenti. `GET /api/history` restituisce `t` (°C), `h` (% UR),
`on` e `known` (stato comandato/dichiarato, non misura indipendente).
Il nuovo grafico e' parte dei file LittleFS, che vanno aggiornati
separatamente e con conseguente cancellazione della configurazione
presente sul dispositivo. La versione HEAT non implementa DRY.

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

## Credenziali locali e configurazione IPv4 statica

Il file `include/private_settings.example.h` è un **modello**, non deve
contenere password reali. Copiarlo in `include/private_settings.h` e inserire
SSID, password Wi-Fi e amministratore. `private_settings.h` è escluso da Git.

Nel file è possibile specificare anche:

```cpp
#define SOLAR_STATIC_IP             "192.168.1.90"
#define SOLAR_STATIC_GATEWAY        "192.168.1.1"
#define SOLAR_STATIC_SUBNET         "255.255.255.0"
#define SOLAR_STATIC_DNS1           "192.168.1.1"
#define SOLAR_STATIC_DNS2           "1.1.1.1"
```

Tali indirizzi sono **solo esempi**; scegliere un IP riservato al dispositivo
o escluso dal pool DHCP del router, compatibile con la propria rete.

Lasciare `SOLAR_STATIC_IP` vuoto, e gli altri parametri vuoti a eccezione
della subnet predefinita, per utilizzare DHCP.

Le impostazioni nel file privato vengono utilizzate **solo durante il primo
avvio su LittleFS senza configurazione**. Se esiste `/config.json` o
`/config.bak`, continuano a prevalere i valori della dashboard, anche dopo
gli aggiornamenti OTA. Per cambiare l'indirizzo di un dispositivo già
configurato, modificare i campi in dashboard, salvare e riavviare.
L'IP risultante è visibile in `GET /api/state`.

La configurazione IPv4 statica viene convalidata prima del salvataggio e
applicata con `WiFi.config()` prima di `WiFi.begin()`. Un indirizzo statico
sbagliato ma formalmente valido può rendere inaccessibile il dispositivo:
in tal caso ripristinare la configurazione via USB o usare la rete di recupero
quando disponibile. Accedere via HTTP solo da LAN affidabile o VPN.
