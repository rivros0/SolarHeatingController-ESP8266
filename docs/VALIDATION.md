# Verifiche software — 25 ottobre 2026

Eseguite sul codice della prima versione, senza collegamento a ESP8266 o impianto reale.

| Verifica | Risultato |
|---|---|
| NodeMCU `pio run -e nodemcuv2` | PASS |
| Wemos D1 Mini `pio run -e d1_mini` | PASS |
| LittleFS entrambe le schede `pio run -t buildfs` | PASS |
| Controlli C++17 con Address/Undefined Sanitizer | 63 controlli PASS |
| Sintassi JavaScript dashboard | PASS |
| Dashboard nel browser, viewport mobile | PASS con dati simulati |
| Cambio modalità dal frontend | PASS su backend simulato |
| Invio configurazione: riserva 700 W, password omesse | PASS su backend simulato |
| Console browser errori/warning | nessuno rilevato durante le prove |
| Collegamenti relativi documentazione | PASS |

Dimensioni al link, identiche per i due target:

- RAM statica: **42.264 / 81.920 byte (51,6%)**.
- Flash sketch: **416.761 / 1.044.464 byte (39,9%)**.
- Filesystem: immagine LittleFS da circa 1 MB, separata dallo sketch; asset inclusi `index.html`, `app.js`, `style.css`, `update.html`.

Questi numeri non misurano il massimo uso dinamico di heap, il buffer TCP durante OTA o il carico del ricevitore. `/api/state` espone heap e blocco massimo liberi per il collaudo sul dispositivo. La diagnostica e lo storico sono inviati in streaming; i codici persistono in formato binario con lunghezza e checksum. L'export della cattura IR richiede un blocco heap libero sufficiente.

I test coprono soglie e stabilità del surplus, distinzione carico acceso/spento, reset della stabilità dopo un calo, anti-riavvio, minimo di funzionamento, arresto prioritario su scarica/scadenza, protezioni V/A, convenzioni indipendenti del segno W/A, importazione, mancanza sensore, modalità forzate, limite temporale, temperatura massima, dati non finiti, wrap timer, filtro ambiente, riconoscimento ON/OFF e incertezza con rumore.

L'ambiente locale esegue processi sotto ptrace: LeakSanitizer richiede `ASAN_OPTIONS=detect_leaks=0`; AddressSanitizer e UndefinedBehaviorSanitizer sono rimasti attivi. Il tool `elf2bin.py` del core Arduino emette avvisi Python 3.14 su escape nelle espressioni regolari, senza impedire la creazione dei binari. I sorgenti del progetto compilano senza warning C++ rilevati. La CI usa Python 3.12.

**Non eseguiti:** prove fisiche SHT31/IR/Wi-Fi/MQTT dell'impianto, identificazione del modello Hisense, misura heap prolungata, perdita alimentazione durante scritture, upload OTA su ESP8266 e recupero USB. Vedere [COMMISSIONING.md](COMMISSIONING.md). I dati dell'anteprima browser sono sintetici e non sono inclusi nel firmware distribuito.
