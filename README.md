# SolarHeatingController-ESP8266

Controller locale per usare il surplus fotovoltaico nel riscaldamento di un climatizzatore Hisense, mantenendo priorità ai carichi domestici e alla ricarica batterie. Firmware Arduino per **NodeMCU ESP8266 e Wemos D1 Mini da 4 MB**, dashboard HTML/CSS/JS su LittleFS, SolarAssistant via MQTT, SHT31 e IRremoteESP8266. Nessun servizio cloud necessario.

**Versione 0.1.0 — da collaudare sull'impianto reale.** Compilazione e test software non certificano compatibilità IR, correttezza dei topic o protezione delle batterie. Il modello del climatizzatore e la tensione nominale del banco AGM non sono ancora noti: occorre configurarli e verificare il comportamento prima di abilitare l'automazione. Il controller non sostituisce le protezioni dell'inverter.

## Funzioni

- Automazione con surplus stabile, riserva ricarica, isteresi termica e temporale, tempi minimi e arresti prioritari.
- Automatico, Forza ON con scadenza, Forza OFF e Diagnostica. Le protezioni su telemetria, temperatura e batteria rimangono attive anche in Forza ON.
- Topic configurabili per FV, carichi AC, batteria W/V/A, SOC, import/export e stato inverter. Segno di potenza e corrente batteria configurabili separatamente.
- Rifiuto dei messaggi retained nelle decisioni; scadenza di ogni misura, invalidazione alla disconnessione e attesa di nuovi messaggi live.
- IR KELON/WHIRLPOOL_AC per prove confermate; acquisizione RAW opzionale a 38 kHz. Automazione esclusivamente con due codici ON/OFF discreti verificati sul dispositivo.
- Temperatura/umidità SHT31 con filtro EMA, segnalazione errori e tentativi di recupero.
- Stato comandato separato da stima, confidenza e stato sconosciuto; apprendimento guidato e riconoscimento variazioni di carico con riferimento e attesa compressore.
- Dashboard responsive, grafici senza dipendenze esterne, diagnostica, esportazione configurazione senza credenziali, 60 campioni e 16 eventi in RAM.
- OTA firmware `.bin` autenticato su `/update`, controllo errori, riavvio differito e conservazione LittleFS.
- Configurazione versionata, migrazione v1→v2, backup e sostituzione tramite file temporaneo. Nessuna formattazione automatica.

## Avvio rapido

1. Cabla i componenti secondo [HARDWARE.md](docs/HARDWARE.md). Verifica il modulo IR prima di alimentarlo.
2. Installa Python e PlatformIO: `python -m pip install platformio==6.2.0`.
3. Compila e carica tramite USB (scegli la scheda):

   ```sh
   pio run -e nodemcuv2
   pio run -e nodemcuv2 -t upload
   pio run -e nodemcuv2 -t uploadfs
   pio device monitor -b 115200
   ```

   Per Wemos sostituisci `nodemcuv2` con `d1_mini`. L'immagine filesystem iniziale è obbligatoria. `uploadfs` sostituisce l'intero filesystem: non usarlo come aggiornamento ordinario dopo la configurazione.
4. Al primo avvio, il seriale mostra una password casuale unica da 24 caratteri. Collegati alla rete WPA `SolarHeat-<chipid>` con questa password; apri `http://192.168.4.1`, utente `admin`, stessa password. Conservala: viene mostrata solo durante la prima inizializzazione.
5. Configura Wi-Fi, broker e topic. Salva; in Diagnostica conferma che il climatizzatore sia fisicamente spento, poi riavvia. La conferma fisica non invia IR. La rete AP viene disattivata quando la station è connessa; torna disponibile se la station è assente dopo 30 s dall'avvio.
6. Trova l'indirizzo locale nel DHCP del router. Verifica i valori MQTT, le unità, la distinzione fra carico totale e altre potenze e le convenzioni del segno. Se i messaggi arrivano solo come retained, l'automazione resta bloccata: configurare pubblicazioni periodiche live sul broker.
7. Configura il sensore e le soglie AGM adatte al tuo banco. Non copiare soglie di tensione da impianti con tensione nominale diversa.
8. Esegui [COMMISSIONING.md](docs/COMMISSIONING.md), verifica i comandi IR e abilita l'automazione soltanto dopo le prove.

Al riavvio il controller entra sempre in **Forza OFF con stato fisico sconosciuto**. Non invia un toggle e non presume che il climatizzatore sia spento. Se dispone già di codici discreti verificati, Forza OFF o una protezione possono inviare un unico OFF preventivo anche da stato sconosciuto. L'ultima modalità richiesta è salvata a fini diagnostici; la ripartenza automatica dopo un reset richiede una nuova conferma fisica o un comando OFF discreto. Questa scelta evita avvii e inversioni inattesi dopo OTA o interruzioni di alimentazione.

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
