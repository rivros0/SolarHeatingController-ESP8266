# OTA e recupero

## Layout flash verificabile

`platformio.ini` imposta entrambe le schede su `eagle.flash.4m1m.ld`: flash fisica 4 MB, LittleFS da circa 1 MB, sketch massimo 1.044.464 byte. Lo spazio tra sketch e filesystem permette lo staging OTA. Il firmware riporta `ESP.getFreeSketchSpace()` in `/api/state`; la libreria Update verifica lo spazio effettivo e l'immagine. Il browser rifiuta file più grandi del margine riportato meno 4096 byte, mentre il backend usa il margine allineato a settore.

Il server usa l'Updater del core ESP8266, solo `U_FLASH`. Non accetta immagini filesystem via OTA. Non è implementato un rollback automatico A/B: ESP8266 usa il bootloader e lo staging previsti dal core. Riferimento: [documentazione ufficiale OTA ESP8266](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).

## Procedura

1. Compilare il progetto con la corretta scheda: `pio run -e d1_mini` o `nodemcuv2`.
2. Spegnere il climatizzatore con un comando verificato o il telecomando e verificarlo fisicamente. In Diagnostica scegliere “Confermo: fisicamente spento”. La sola stima di potenza non basta.
3. Aprire `/update` dalla LAN o VPN ed autenticarsi. Selezionare `.pio/build/<scheda>/firmware.bin`, **non** `littlefs.bin`.
4. Mantenere alimentazione stabile e attendere la risposta. Il server verifica autenticazione, CSRF, stato spento, scrittura e validità dell'immagine. In caso di fallimento non programma un riavvio.
5. Dopo esito positivo il dispositivo si riavvia con ritardo di 1,5 s per completare la risposta. Ricaricare la dashboard e controllare versione, motivo reset e configurazione. Una caduta di rete durante il caricamento richiede verifica prima di riprovare.
6. Dopo riavvio, stato fisico sconosciuto e Forza OFF. Confermare nuovamente prima dell'automazione.

La conservazione configurazione deriva dalla separazione fra sketch e LittleFS e richiede mantenere il medesimo layout flash. Gli eventi RAM si perdono al riavvio; il messaggio HTTP conferma la finalizzazione dell'upload, la versione al rientro conferma il firmware in esecuzione. Non viene dichiarato un esito persistente dell'OTA che il dispositivo non può dimostrare dopo un reset.

## Recupero USB

Se l'applicazione non torna raggiungibile:

1. Verificare se il climatizzatore è rimasto acceso e gestirlo con il telecomando.
2. Collegare USB, identificare porta e scheda, controllare alimentazione e log a 115200 baud (boot ROM a 74880 baud).
3. Tentare `pio run -e <scheda> -t upload`. Questo sostituisce lo sketch lasciando il filesystem, se layout e flash sono coerenti.
4. Se l'autoboot non funziona, entrare nella modalità flash secondo la scheda: GPIO0 a GND durante reset, poi rilasciare dopo l'avvio del caricamento.
5. Se LittleFS/config è corrotto, il firmware arresta le funzioni invece di formattare o sovrascrivere. Conservare un dump se necessario. Come ultimo recupero caricare `uploadfs` via USB: **questa operazione cancella configurazione e codici IR presenti nel filesystem**. Recuperare i valori dall'export redatto e reimmettere credenziali/catture. Verrà generata una nuova password iniziale sul seriale.

Non cambiare dimensione flash o layout senza backup. Non fare port forwarding. Tailscale termina su gateway/router, non sul microcontrollore. HTTP Basic su LAN non equivale a cifratura TLS.

## Aggiornamento diretto da GitHub (versione 0.2.0)

È disponibile una seconda modalità nella stessa pagina `/update`. Non sostituisce
l'OTA manuale, che continua a funzionare anche se Internet non è accessibile.

### Preparazione della release

Il workflow `.github/workflows/firmware-release.yml` esegue i test, compila
`nodemcuv2` e `d1_mini`, calcola gli SHA256 e pubblica SOLO i binari nella
release `firmware-stable`. Dopo aver caricato entrambi gli asset, aggiorna
`firmware/stable.json` sul branch `main`.

La release dedicata al case 3D, `case-v0.1`, non viene mai considerata.
I binari delle due schede hanno nomi diversi e non sono intercambiabili.
Il workflow viene eseguito quando i sorgenti cambiano su `main`, oppure
manualmente da Actions. Richiede permesso `contents:write` per il workflow.
Una modifica nella sola cartella del CAD non pubblica un nuovo firmware.

### Installazione dal dispositivo

1. Collegare l'ESP8266 alla rete con accesso a Internet e DNS correttamente
   configurato (necessario anche quando si imposta IPv4 statico).
2. Attendere la sincronizzazione NTP; senza un'ora valida TLS rifiuta il download.
3. Spegnere fisicamente il climatizzatore Hisense e confermare **OFF** nella
   dashboard Diagnostica.
4. Aprire `/update`, selezionare **Verifica aggiornamenti**.
5. Se la versione GitHub è successiva e compatibile, selezionare
   **Installa da GitHub**, confermando l'operazione.
6. Il firmware viene prima scaricato su file temporaneo LittleFS, poi viene
   controllato contro il checksum SHA256 del manifest; solo a quel punto
   viene scritto sulla partizione firmware OTA.
7. L'ESP8266 si riavvia in Forza OFF e stato fisico sconosciuto.
   Verificare versione, configurazione e stato del climatizzatore.

### Protezioni e limiti

- HTTPS usa una catena CA verificabile; non usa `setInsecure()`.
  I certificati pubblici vengono incorporati al momento della compilazione
  dall'archivio CA della macchina di build.
- Se GitHub cambia catena CA, l'aggiornamento diretto può smettere di
  funzionare finché i CA non vengono aggiornati in un firmware successivo.
  In tale caso utilizzare l'OTA manuale, non disabilitare la verifica TLS.
- Se l'orologio NTP non è sincronizzato, il download è rifiutato.
- Il filesystem LittleFS deve avere spazio libero almeno pari al firmware,
  più un margine di sicurezza. In caso contrario utilizzare l'OTA manuale.
- Dopo una perdita di rete o un checksum errato il firmware nuovo non viene
  attivato. Una perdita di alimentazione durante la fase finale di flash
  richiede comunque la procedura di recupero USB.
- Il firmware ESP8266 non dispone di un rollback automatico A/B.
- La modalità automatica non avvia mai aggiornamenti da sola: occorre
  un comando autenticato con token CSRF.
- Le credenziali restano in `config.json` su LittleFS; il firmware OTA non
  modifica `data/` né sovrascrive il filesystem.
