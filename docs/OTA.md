# Aggiornamento OTA e recupero USB

## Principio di funzionamento (firmware 0.3.2)

Il controller consente due modalità di manutenzione:

| Procedura | Firmware | Interfaccia web | Configurazioni / credenziali / IR RAW |
| --- | --- | --- | --- |
| **Aggiorna da GitHub** (`/update`) | Se più recente | Se differente, anche a firmware invariato | **Conservati** |
| **Carica firmware .bin** (`/update`) | Sostituito | Invariata | **Conservati** |
| **Upload USB firmware** (`pio run -t upload`) | Sostituito | Invariata | **Conservati**, se layout invariato |
| **Upload USB LittleFS** (`pio run -t uploadfs`) | Invariato | Sostituita integralmente | **Cancellati** |

Il canale **firmware-stable** di GitHub contiene il firmware per ciascuna
scheda e **tre file web pubblici**:

- `web-index.html` → `/index.html`
- `web-app.js` → `/app.js`
- `web-style.css` → `/style.css`

**Non sono pubblicati né scaricati** `config.json`, `config.bak`,
`/mode.txt`, password, credenziali MQTT e `/ir-on.raw`/`/ir-off.raw`.

Il manifest `firmware/stable.json` riporta per ogni asset dimensione e
SHA-256. Il controller confronta anche il contenuto **effettivo** dei tre
file già presenti in LittleFS: non si affida a un numero di versione web
salvato nel filesystem. Questo permette di individuare una pagina obsoleta
dopo una reinstallazione o un download interrotto.

## Ripristino del controllo manifest (versione 0.3.3)

Se la 0.3.2 segnala **"Manifest firmware JSON non valido"** prima
di abilitare il pulsante GitHub, il problema e' nella precedente
lettura HTTP del JSON. Il file pubblicato puo' essere valido e il
trasferimento parziale o il framing HTTP non interpretato correttamente.

La 0.3.3 riceve l'intero corpo usando HTTPClient, controlla il numero
di byte e riporta una diagnostica distinta per errori HTTP o parser.
Anche i binari vengono letti tramite HTTPClient e verificati contro
SHA256 dei file temporanei realmente scritti su LittleFS.

**Procedura di recupero:** programmare la 0.3.3 via USB o upload
manuale `.bin` sulla pagina `/update`. Questa operazione non
modifica LittleFS. Dopo il riavvio selezionare **Verifica aggiornamenti**
e **Installa aggiornamenti da GitHub** per aggiornare selettivamente
`index.html`, `app.js` e `style.css`. Una connessione Wi-Fi scarsa
puo' ancora causare timeout o trasferimenti incompleti, che ora
vengono rifiutati senza installare file parziali.

## Prima attivazione del nuovo sistema OTA (da 0.3.1 a 0.3.2)

La versione 0.3.1 sa gia' aggiornare il firmware dal canale
`firmware-stable`, ma non conosce ancora gli asset web. Il primo
aggiornamento richiede quindi **due passaggi** se si utilizza la rete:

1. sulla 0.3.1, verificare e installare il firmware **0.3.2** da GitHub;
2. dopo il riavvio, riaprire `/update`, verificare nuovamente e
   aggiornare i file della dashboard mediante la nuova funzione.

In alternativa, caricare la 0.3.2 via USB o con upload manuale `.bin`,
poi usare GitHub OTA per la dashboard. Nessuna delle due procedure
richiede `uploadfs`. Con Wi-Fi instabile, preferire USB per il firmware.

## Aggiornamento completo da GitHub

1. Assicurarsi che climatizzatore e impianto siano in condizioni sicure.
   Aprire `http://<ip-esp>/update` da LAN fidata.
2. Premere **Verifica aggiornamenti**: si vedranno separatamente lo stato
   del **firmware** e dell'**interfaccia web**, compreso il numero di file da
   aggiornare. Il controllo può rilevare un'interfaccia nuova anche quando
   il firmware è già aggiornato.
3. Premere **Installa aggiornamenti da GitHub**.
4. Il popup richiede di verificare **fisicamente** che il climatizzatore
   sia spento. Il pulsante Annulla interrompe l'operazione. Con OK,
   un'API autenticata registra lo stato OFF e passa a **Forza OFF**,
   senza inviare IR.
5. Il controller scarica **soltanto i file web differenti** in file
   temporanei sulla propria LittleFS. Ogni file deve superare il controllo
   di dimensione e SHA-256. Solo dopo aver verificato tutti i file, li
   sostituisce individualmente con rename.
6. Se esiste un firmware più recente, il controller lo scarica, ne verifica
   SHA-256 e formato, lo installa nello spazio OTA del core ESP8266 e
   programma un riavvio. Se cambia soltanto l'interfaccia web, non
   riavvia il dispositivo.
7. Dopo il completamento, ricaricare la dashboard con **Ctrl+Shift+R**
   per evitare la cache del browser. Verificare Wi-Fi/MQTT, valori,
   configurazione, comandi IR e versione effettivamente avviata.

**In caso di Wi-Fi debole:** il download può fallire per timeout, HTTP o
checksum. Le risorse correnti rimangono disponibili se l'errore avviene
durante lo staging. Se si perde alimentazione durante la sostituzione,
alcuni asset potrebbero essere già nuovi e altri ancora vecchi: il
successivo controllo SHA-256 individua i file da completare. Non viene
cancellato il filesystem. Il controller non esegue mai aggiornamenti
automaticamente senza comando esplicito.

## Pubblicazione GitHub

Il workflow `.github/workflows/firmware-release.yml`:

1. esegue i test e compila `nodemcuv2` e `d1_mini`;
2. copia `data/index.html`, `data/app.js`, `data/style.css` come
   asset `web-*`;
3. calcola SHA-256 e dimensioni per firmware e web;
4. pubblica tutti gli asset nella release `firmware-stable`;
5. **solo dopo** aggiorna `firmware/stable.json` sul ramo `main`.

La release del contenitore CAD resta indipendente. Gli asset pubblicati
sono esclusivamente file statici e binari, senza informazioni riservate.

## Sicurezza e limiti

- TLS HTTPS con verifica dei certificati CA e verifica hash dei file.
  Non viene utilizzato `setInsecure()`.
- È necessaria un'ora NTP valida per verificare i certificati.
- Il manifest indica i file consentiti e le loro dimensioni, ma
  **non può scegliere percorsi arbitrari** nel filesystem.
- File temporanei e verifica preventiva limitano i rischi di download
  incompleti. Un'interruzione proprio durante la sostituzione può
  richiedere di ripetere l'operazione.
- L'OTA firmware usa soltanto `U_FLASH`, mai `U_FS`, e mantiene il
  layout `eagle.flash.4m1m.ld`. Non c'è rollback automatico A/B.
- Richiesti HTTP Basic, token CSRF e conferma fisica OFF.
- Non esporre la dashboard o l'OTA direttamente a Internet tramite
  port forwarding. Utilizzare LAN fidata o VPN terminata su gateway.
- Se lo spazio libero LittleFS non è sufficiente per lo staging, il
  controller rifiuta l'aggiornamento con un messaggio esplicito.

## Recupero USB

1. Verificare lo stato fisico del climatizzatore; gestirlo con il suo
   telecomando se il controller non è raggiungibile.
2. Collegare USB e identificare la porta CH340 con `pio device list`
   (per esempio `/dev/ttyUSB1`, non il numero di dispositivo USB).
3. Ricaricare il firmware: `pio run -e nodemcuv2 -t upload --upload-port /dev/ttyUSB1`.
   Questo comando conserva normalmente LittleFS.
4. Consultare i log seriali a 115200 baud; il bootloader ROM stampa a
   74880 baud e può produrre caratteri iniziali incomprensibili.
5. **Solo come ultima risorsa**, `pio run -e nodemcuv2 -t uploadfs --upload-port /dev/ttyUSB1`.
   Questo comando sovrascrive l'intero LittleFS e cancella configurazioni,
   credenziali e codici IR. Salvare prima i dati se recuperabili.

Non cambiare dimensione flash o layout senza backup.
