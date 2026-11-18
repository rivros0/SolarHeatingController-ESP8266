# SolarHeatingController-ESP8266

Controller locale per usare il surplus fotovoltaico nel riscaldamento di un climatizzatore Hisense, mantenendo priorità ai carichi domestici e alla ricarica batterie. Firmware Arduino per **NodeMCU ESP8266 e Wemos D1 Mini da 4 MB**, dashboard HTML/CSS/JS su LittleFS, SolarAssistant via MQTT, SHT31 e IRremoteESP8266. Nessun servizio cloud necessario.

**Versione 0.3.1 — da collaudare sull'impianto reale.** Compilazione e test software non certificano compatibilità IR, correttezza dei topic o protezione delle batterie. Il climatizzatore e' un Hisense CA50XS1AG con telecomando DG11L1-12; protocollo IR da acquisire. La tensione nominale del banco AGM deve essere verificata: occorre configurarli e verificare il comportamento prima di abilitare l'automazione. Il controller non sostituisce le protezioni dell'inverter.

## Funzioni

- Automazione con surplus stabile, riserva ricarica, isteresi termica e temporale, tempi minimi e arresti prioritari.
- Automatico, Forza ON con scadenza, Forza OFF e Diagnostica. Le protezioni su telemetria, temperatura e batteria rimangono attive anche in Forza ON.
- Topic configurabili per FV, carichi AC, batteria W/V/A, SOC, import/export e stato inverter. Segno di potenza e corrente batteria configurabili separatamente.
- Rifiuto dei messaggi retained nelle decisioni; scadenza di ogni misura, invalidazione alla disconnessione e attesa di nuovi messaggi live.
- IR KELON/WHIRLPOOL_AC per prove confermate; acquisizione RAW opzionale a 38 kHz. Automazione esclusivamente con due codici ON/OFF discreti verificati sul dispositivo.
- Temperatura/umidità SHT31 con filtro EMA, segnalazione errori e tentativi di recupero.
- **Assistenza UR in HEAT opzionale** (disabilitata di default): richiesta quando UR >=60%, rientro <=55%, soltanto se T < tempOff-0,5 °C e restano rispettate le protezioni energetiche/IR e i tempi. HEAT non rimuove acqua dall'aria; per vera deumidificazione occorre una modalita' DRY verificata separatamente.
- Stato comandato separato da stima, confidenza e stato sconosciuto; apprendimento guidato e riconoscimento variazioni di carico con riferimento e attesa compressore.
- Dashboard responsive **a cinque schede**, grafici senza dipendenze esterne, diagnostica, esportazione configurazione senza credenziali, 120 campioni e 16 eventi in RAM.
- OTA firmware `.bin` autenticato su `/update`, controllo errori, riavvio differito e conservazione LittleFS.
- Aggiornamento da GitHub (release firmware-stable) con controllo versione, board, HTTPS verificato e SHA256 prima di installare.
- Credenziali iniziali Wi-Fi/admin in file locale escluso da Git e configurazione IPv4 statico (gateway, subnet, DNS).
- Configurazione versionata, migrazione v1→v2, backup e sostituzione tramite file temporaneo. Nessuna formattazione automatica.

## Avvio rapido

1. Cabla i componenti secondo [HARDWARE.md](docs/HARDWARE.md). Verifica il modulo IR prima di alimentarlo. Facoltativamente copia `include/private_settings.example.h` in `include/private_settings.h` e inserisci **solo nella copia locale** credenziali Wi-Fi/admin e, se necessario, IPv4 statico. Questa configurazione vale al primo avvio, non sovrascrive le impostazioni gia\u0027 salvate in LittleFS.
2. Installa Python e PlatformIO: `python -m pip install platformio==6.2.0`.
3. Compila e carica tramite USB (scegli la scheda):

   ```sh
   pio run -e nodemcuv2
   pio run -e nodemcuv2 -t uploadfs
   pio run -e nodemcuv2 -t upload
   pio device monitor -b 115200
   ```

   Per Wemos sostituisci `nodemcuv2` con `d1_mini`. L'immagine filesystem iniziale è obbligatoria. `uploadfs` sostituisce l'intero filesystem: non usarlo come aggiornamento ordinario dopo la configurazione.
4. Al primo avvio, il seriale mostra una password casuale unica da 24 caratteri. Collegati alla rete WPA `SolarHeat-<chipid>` con questa password; apri `http://192.168.4.1`, utente `admin`, stessa password. Conservala: viene mostrata solo durante la prima inizializzazione.
5. Configura Wi-Fi, broker e topic. Salva; in Diagnostica conferma che il climatizzatore sia fisicamente spento, poi riavvia. La conferma fisica non invia IR. La rete AP viene disattivata quando la station è connessa; torna disponibile se la station è assente dopo 30 s dall'avvio.
6. Trova l'indirizzo locale nel DHCP del router. Verifica i valori MQTT, le unità, la distinzione fra carico totale e altre potenze e le convenzioni del segno. Se i messaggi arrivano solo come retained, l'automazione resta bloccata: configurare pubblicazioni periodiche live sul broker.
7. Configura il sensore e le soglie AGM adatte al tuo banco. Non copiare soglie di tensione da impianti con tensione nominale diversa.
8. Esegui [COMMISSIONING.md](docs/COMMISSIONING.md), verifica i comandi IR e abilita l'automazione soltanto dopo le prove.
9. Per verificare nuovi firmware direttamente da GitHub apri `/update`, consulta il canale firmware-stable e scegli l'installazione. Vedere [OTA.md](docs/OTA.md).

Al riavvio il controller entra sempre in **Forza OFF con stato fisico sconosciuto**. Non invia un toggle e non presume che il climatizzatore sia spento. Se dispone già di codici discreti verificati, Forza OFF o una protezione possono inviare un unico OFF preventivo anche da stato sconosciuto. L'ultima modalità richiesta è salvata a fini diagnostici; la ripartenza automatica dopo un reset richiede una nuova conferma fisica o un comando OFF discreto. Questa scelta evita avvii e inversioni inattesi dopo OTA o interruzioni di alimentazione.

## Aggiornamento OTA: conferma OFF nella pagina (v0.3.1)

La pagina **Aggiorna firmware** apre un popup prima di **Installa da
GitHub** e prima dell'**upload manuale .bin**. L'operatore deve aver
verificato personalmente che il climatizzatore sia spento. Dopo OK la
pagina registra tramite API autenticata lo stato fisico dichiarato OFF
e imposta **Forza OFF**; nessun comando infrarosso viene inviato per
questa operazione. Con **Annulla** l'installazione non inizia.

I controlli di sicurezza nel firmware restano attivi indipendentemente
dal popup; la conferma non bypassa la validazione della release, del file
BIN, del CSRF o della scheda. Il comportamento precedente della
telemetria, dei sensori, dell'IR e delle automazioni non cambia.

Il nuovo popup viene letto da `data/update.html` **durante la
compilazione** e incorporato nel firmware come risorsa PROGMEM. Di
conseguenza basta il **solo upload USB del firmware 0.3.1** per aggiornare
anche la pagina `/update`. Non serve `uploadfs`: le impostazioni Wi-Fi,
MQTT e gli eventuali codici RAW su LittleFS rimangono intatti. La dashboard
a schede continua invece a essere servita da LittleFS e richiede la
relativa procedura separata.

## Ricevitore IR G / R / Y (GND / 3V3 / D6)

E' stato individuato un ricevitore IR a tre pin marcati **G**, **R**, **Y**.
Sul modulo di questo tipo il cablaggio previsto e':
**G → GND**, **R → 3V3**, **Y → D6 (GPIO12)**.
La corrispondenza della sigla all'effettivo piedino va **verificata sul modulo
disalimentato**: forma e serigrafia non garantiscono da sole una piedinatura
universale. Verificare alimentazione a 3,3 V prima di collegare la NodeMCU.

La scheda **Infrarossi** contiene ora una guida espandibile ai collegamenti e
un riepilogo dell'ultima cattura RAW (`protocol`, `length`, lunghezze degli
slot HEAT ON e OFF). Il JSON completo e' ancora consultabile senza perdere
i dati tecnici. Per usare il ricevitore, abilitare **Ricevitore GPIO12** in
**Configurazione → Infrarossi**, salvare e riavviare, poi leggere la cattura
con il tasto **Leggi acquisizione**.

Per la modalita' **DRY** si puo' eseguire una lettura diagnostica, **ma non
salvarla negli slot HEAT ON/OFF**: il firmware non dispone ancora di uno slot
DRY e un salvataggio sostituirebbe un segnale precedentemente verificato.

Le modifiche di questa sezione riguardano documentazione e dashboard, non
richiedono una nuova versione del firmware. Per visualizzarle sul dispositivo
e' necessario aggiornare i file in LittleFS. **Non eseguire `uploadfs` senza
prima aver salvato configurazioni e codici IR**: l'operazione sovrascrive
l'intero filesystem, comprese password e catture. Un export della dashboard
non contiene le password ne' i file RAW.

Per la procedura elettrica e di collaudo, vedere
[docs/HARDWARE.md](docs/HARDWARE.md#modulo-a-tre-pin-g--r--y-ricevitore-ir-trovato-durante-il-collaudo).

## Dashboard a schede (aggiornamento interfaccia 0.3.0)

La stessa dashboard locale e' suddivisa in cinque schede, senza alterare
le API, le modalita' di controllo e la persistenza della configurazione:

- **Panoramica**: valori FV, carichi, batterie, surplus, temperatura/UR,
  modalita' Auto/Forza ON/Forza OFF/Diagnostica e stato del clima.
- **Grafici**: energia e storico ambiente con due assi °C/UR e fascia
  ON/OFF/ignoto; i grafici vengono ridisegnati all'apertura della scheda.
- **Configurazione**: impostazioni Wi-Fi, MQTT, mappatura topic, soglie
  energetiche/ambientali e IR, in gruppi espandibili; salva, riavvia,
  esportazione redatta e cancellazione password MQTT invariati.
- **Infrarossi**: conferma fisica, singoli test, ricezione/salvataggio RAW
  e apprendimento consumi.
- **Diagnostica**: eventi, topic MQTT, stato della rete e risorse.

La navigazione utilizza un hash locale (`#overview`, `#charts`,
`#settings`, `#infrared`, `#diagnostics`) ed e' utilizzabile con
tastiera e su smartphone. Gli ID dei 37 controlli originali sono protetti
da un test automatico: `node scripts/test_web_ui.mjs`.

**Installazione su dispositivo gia' configurato:** le modifiche riguardano
esclusivamente i file web in `data/`. Per applicarle serve aggiornare
**LittleFS** con `pio run -e nodemcuv2 -t uploadfs --upload-port /dev/ttyUSB0`.
**Importante:** questo comando sostituisce l'intero filesystem e cancella
`config.json`, le credenziali Wi-Fi/MQTT e i codici IR RAW persistenti.
Prima salvare l'esportazione redatta, annotare le password separatamente e
mettere al sicuro le catture IR, se presenti; dopo il caricamento reinserire
le impostazioni. Il normale upload del solo firmware non aggiorna i file web.
Non e' stato modificato lo schema di configurazione ne' il firmware in questa
revisione della sola interfaccia.

## Diagnostica seriale USB (versione 0.2.1)

La porta seriale del firmware utilizza **115200 baud**. Con la NodeMCU collegata via USB:

```sh
pio device monitor --port /dev/ttyUSB0 --baud 115200 --rts 0 --dtr 0 --filter time
```

- **A ogni avvio** il firmware stampa un banner `[BOOT]` con versione, motivo e dettaglio del reset, memoria flash, dimensione dello sketch e RAM libera. I messaggi `[INIT]`, `[FS]`, `[CONFIG]`, `[SENSOR]` e `[WIFI]` mostrano l'avanzamento dell'inizializzazione anche quando esiste gia' `/config.json`.
- Gli eventi registrati nella dashboard sono riportati anche sulla USB con prefisso `[EVENT +Ns]`: questo include le variazioni di modalita', gli invii IR, gli eventi MQTT e l'esito OTA.
- Ogni **30 secondi** appare un riepilogo `[STATUS]`, `[WIFI]`, `[MQTT]`, `[SENSOR]`, `[ENERGY]`, `[IR]`, con uptime, RAM libera, stato rete, IP/RSSI se connesso, temperatura/umidita' e misure energetiche valide. I campioni assenti o scaduti sono indicati come `n/d`. La potenza `batteria_raw` mantiene il segno originale ricevuto via MQTT.
- Le transizioni Wi-Fi e MQTT producono un messaggio immediato, controllato a intervalli di un secondo. Il riepilogo continua anche quando la configurazione non e' disponibile.
- **Non** vengono ripetute le credenziali nei riepiloghi. Solo il **primo avvio con password generata** stampa la password iniziale, come gia' previsto dalla procedura di configurazione: conservare il log seriale in modo riservato.

**Attenzione:** il bootloader ROM dell'ESP8266 trasmette brevemente a **74880 baud**; sono quindi normali alcuni caratteri incomprensibili *prima* del banner `[BOOT]` leggibile a 115200 baud. Se il banner ricompare continuamente, confrontare il campo `Reset` e verificare alimentazione, RST e collegamenti esterni.

Per aggiornare il firmware dalla USB dopo la prima installazione e' sufficiente `pio run -e nodemcuv2 -t upload --upload-port /dev/ttyUSB0`. **Non ripetere `uploadfs`**: sostituisce il filesystem e cancella credenziali, configurazioni e codici IR acquisiti. La diagnostica seriale non modifica le decisioni energetiche o la sicurezza delle trasmissioni IR.

## Controllo comfort igrometrico (v0.3.0)

La scheda **Configurazione → Ambiente** permette di attivare l'opzione
`energy.humidityAssist`, impostando `energy.humidityOn` (60% predefinito)
e `energy.humidityOff` (55%). Le soglie devono rispettare
`30 <= humidityOff <= 70` e `humidityOff + 3 <= humidityOn <= 85`.
L'opzione e' **inizialmente disabilitata** anche quando si aggiorna
una configurazione v2 gia' esistente.

In Automatico, con telemetria live, codici discreti verificati e riserva
energetica sufficiente, un'umidita' elevata puo' richiedere riscaldamento
solo sotto `tempOff-0,5 °C`. Non scavalca mai il limite `tempOff`.
Una richiesta termica (`T < tempOn`) prevale e conserva la normale
isteresi termica. Se il ciclo era stato avviato esclusivamente per UR,
l'arresto avviene dopo rientro sotto `humidityOff` e rispetto del tempo
minimo di funzionamento. Sensore UR assente o dati scaduti non provocano
nuovi avvii per umidita'; il controllo termico preesistente continua.

**Limite fisico importante:** riscaldare riduce l'umidita' *relativa*,
ma non sottrae vapore all'ambiente. Per correggere umidita' persistente
serve ventilazione o una modalita' DRY/deumidificatore dedicato, con
controlli IR specifici non implementati in questa versione.

Il grafico **Temperatura, umidita' e stato climatizzatore** separa
gli assi °C e % UR e visualizza una fascia ON/OFF/ignoto del comando
climatizzatore. Non e' una misura fisica indipendente. Lo storico tiene
120 campioni in RAM (circa 60 minuti a 30 secondi) e si azzera al
riavvio. I campioni senza lettura producono intervalli vuoti.

**Aggiornamento dalla v0.2.1:** il nuovo grafico e i nuovi controlli web
richiedono anche il caricamento del nuovo LittleFS. Prima di `uploadfs`
annotare/esportare le impostazioni e conservare le credenziali e i
codici RAW IR: `uploadfs` cancella la configurazione del dispositivo.
Non usarlo per un aggiornamento esclusivamente del firmware.

## Logica energetica

```text
surplus_dopo_riserva = produzione_FV - carico_AC_totale - riserva_carica

Spento: surplus_dopo_riserva >= potenza_prevista_clima
Acceso: surplus_dopo_riserva >= 0  (il clima è già nel carico AC)

In entrambi i casi: carica_batteria_normalizzata >= riserva_carica
Importazione > 50 W: veto all'avvio / arresto dopo il filtro temporale
```

La potenza batteria non è sottratta nuovamente dal bilancio: costituisce una verifica indipendente della ricarica effettiva. L'export, quando associato, deve essere aggiornato, ma non viene sommato al surplus già derivato da FV e carichi: sarebbe un doppio conteggio. Questa stima è deliberatamente conservativa e non sfrutta energia FV non misurata per limitazione dell'inverter.

Esempio: FV 4000 W, carico 1000 W, riserva 500 W → 2500 W disponibili prima di avviare un carico da 1200 W. Dopo l'avvio, carico 2200 W → 1300 W di margine dopo la riserva. La ricarica reale deve restare sufficiente.

La scarica oltre soglia, la tensione sotto limite, la corrente di scarica eccessiva, la perdita di dati/sensore e la temperatura massima provocano OFF senza attendere il minimo di funzionamento. Un calo non critico attende sia la verifica temporale sia il minimo di funzionamento. SOC è informativo e non autorizza da solo l'accensione.

Valori iniziali: 19/21 °C, riserva 500 W, carico clima 1200 W, stabilità 10 min, verifica calo 5 min, anti-riavvio 10 min, funzionamento minimo 5 min. Scarica critica 100 W; protezioni V/A disattivate finché non impostate consapevolmente. L'isteresi è `tempOff - tempOn`.

## Documentazione e verifiche

- [Collegamenti e dimensionamento IR](docs/HARDWARE.md)
- [Configurazione, API e limiti delle risorse](docs/API.md)
- [Messa in servizio e prove sull'impianto](docs/COMMISSIONING.md)
- [Aggiornamento OTA e recupero USB](docs/OTA.md)
- [Architettura e limiti](docs/ARCHITECTURE.md)
- [Requisiti e corrispondenza implementazione](docs/REQUIREMENTS.md)
- [Valutazione estensione raffrescamento HEAT/COOL](docs/COOLING_DESIGN.md) (proposta, non attiva in 0.1.0)
- [Esito verifiche software](docs/VALIDATION.md)

```sh
sh scripts/test.sh              # C++17, controlli con Address/Undefined Sanitizer
pio run                        # entrambe le schede
pio run -t buildfs              # entrambe le immagini LittleFS
node --check data/app.js        # verifica sintassi dashboard
```

Nel sandbox Codex, LeakSanitizer non può operare sotto ptrace: usare `ASAN_OPTIONS=detect_leaks=0 sh scripts/test.sh`; AddressSanitizer e UndefinedBehaviorSanitizer restano attivi. CI GitHub esegue i test e produce firmware/LittleFS per entrambe le schede.

## Storico persistente senza usura flash

Il dispositivo mantiene solo il breve termine in RAM, con intervallo configurabile. Per archiviazione persistente usa un computer della LAN/VPN, con durata e frequenza configurabili:

```sh
python scripts/history_logger.py http://192.168.1.50 --interval 30 --hours 24 --output solar-history.csv
```

La password viene richiesta interattivamente. Non è salvata nel CSV. Non modificare la configurazione per ogni misura: il salvataggio flash è riservato alle operazioni esplicite dell'utente. L'esportazione configurazione esclude tutte le password (inclusa quella Wi-Fi) e deve essere completata manualmente con le credenziali su un nuovo dispositivo.

## Sicurezza di rete

HTTP Basic e MQTT in questa versione non sono cifrati. Usare una LAN fidata o una VPN terminata su router/gateway (ad esempio Tailscale sul gateway), con TLS/autenticazione aggiuntiva se necessario. Non installare Tailscale direttamente sull'ESP8266 e non fare port forwarding dell'OTA o della dashboard. Tutte le API richiedono autenticazione; le mutazioni richiedono inoltre un token CSRF generato a ogni avvio.

Credenziali conservate in chiaro nella flash, mai incluse negli export. L'accesso fisico può consentirne la lettura. I log dei topic sono solo dati ricevuti dal broker: usare un filtro ristretto a SolarAssistant per evitare di registrare messaggi sensibili di altri dispositivi. La conferma dei codici IR richiede verifica fisica; la stima dei consumi non vale come conferma.

## Contributi e licenza

Contributi tramite issue e pull request. Per nuovi climatizzatori includere modello esatto, telecomando, protocollo, natura toggle/discreta e catture riproducibili senza credenziali. Firmware pubblicato con licenza MIT; le librerie dipendenti mantengono le rispettive licenze (fra cui LGPL di IRremoteESP8266). Prima di distribuire binari, rispettare anche gli obblighi delle librerie collegate.
