# Architettura e limiti

## Moduli

- `lib/ControlCore/energy_manager.h`: funzione decisionale con stato dei timer, senza dipendenze Arduino. Restituisce azione, ragione, margine e criticità; non trasmette IR.
- `environment_manager.h`: validazione e filtro EMA dei due canali SHT31. Un errore invalida subito entrambi; la prima lettura valida reinizializza il filtro. Altri sensori possono alimentare la stessa interfaccia di campione.
- `climate_power_detector.h`: riferimento e varianza su finestre, attesa avvio e osservazione dopo un comando. La stima ha confidenza massima 0.65, non è una probabilità calibrata né una misura dedicata. Direzione e variabilità incompatibili producono unknown.
- `src/config.cpp`: schema v2, validazione, migrazione v1 con automazione disattivata, scrittura temporanea e backup. Le password non entrano nella serializzazione pubblica. Credenziali protette dall'accesso HTTP, non cifrate nella flash.
- `src/ir_manager.cpp`: selezione protocollo, test confermati, capture RAW con limiti, slot persistenti e deduplicazione comandi. La verifica è legata all’impronta dei due slot; viene revocata e salvata prima di sostituire una cattura. Un backup di configurazione precedente non può autorizzare slot differenti. Ricezione di un telecomando esterno invalida lo stato comandato noto: non si presume che il frame sia stato recepito dal climatizzatore.
- `src/main.cpp`: MQTT asincrono con metadati retain, scheduler tramite millis, API autenticate, OTA U_FLASH, apprendimento e buffer circolari.
- `data/`: frontend senza CDN/framework, canvas per grafici, configurazione e diagnostica. Dati non validi rappresentati come assenti; nessun valore arbitrario sostituisce una misura mancante.

## Stato e interruzioni

Da stato sconosciuto, Forza OFF e le protezioni possono inviare OFF soltanto con codici discreti già verificati. La trasmissione rende noto lo stato comandato OFF senza confermare quello fisico. In Diagnostica gli stati sconosciuti richiedono intervento dell’operatore.

Il comando ON indica una **trasmissione effettuata**, non la conferma del ricevitore del climatizzatore. Il rilevatore aggregato mantiene uno stato indipendente. Altri elettrodomestici e modulazione compressore possono generare falsi indizi; nessun indizio viene usato per ritrasmettere un toggle.

La perdita di comunicazione invalida l'intero set e il riferimento del rilevatore. Dopo riconnessione occorrono campioni live e un nuovo intervallo di stabilità. I campioni usano timestamp locali di ricezione monotoni a 32 bit; i timer operativi gestiscono il wrap di millis tramite sottrazione unsigned. L'uptime e le ascisse grafiche si riavvolgono circa ogni 49,7 giorni; per timestamp assoluti lo storico esterno registra il momento di acquisizione del computer. Non è richiesto NTP né Internet.

In avvio la configurazione si carica da primary, altrimenti backup. File più grandi del limite, JSON non validi o versioni non supportate non vengono applicati. Se nessun file esistente è recuperabile l'automazione e il web non partono; il seriale indica recupero USB. Nessun filesystem viene formattato implicitamente. La migrazione non può inventare credenziali o semantiche da versioni di terze parti.

La modalità preferita è salvata solo su azione esplicita dell'utente, in un file separato. È informazione diagnostica; l'avvio rimane Forza OFF/unknown. Non si invia OFF al boot se la semantica non è verificata, e non si riprende automaticamente un riscaldamento dopo un reset.

## Risorse

- Firmware e librerie sono limitati ai protocolli IR selezionati dai flag PlatformIO (KELON, WHIRLPOOL, decoder KELON168, RAW/hash). KELON168 è identificabile e riproducibile tramite RAW, non sintetizzato da IRac 2.8.6.
- Tre buffer RAW da 512 uint16 (3072 byte); ricevitore opzionale da 1024 entry allocato solo se abilitato. Non vengono accettate catture in overflow.
- Storico 60 campioni (~1,7 KB), eventi 16×160 caratteri massimi, 12 topic diagnostici di massimo 128+80 caratteri.
- JSON configurazione su heap fino a 6144 byte di capacità; export cattura IR fino a 9216 byte, subordinato a un blocco libero di almeno 11000 byte. Storico e diagnostica vengono inviati una riga alla volta, non costruiti interamente su heap. Gli slot IR sono binari con lunghezza e checksum, senza una seconda rappresentazione JSON allocata in RAM. Gli asset LittleFS vengono letti in streaming.
- Nessuna scrittura flash a ogni misura, evento, messaggio MQTT o iterazione. Configurazione, modalità e codici sono salvati soltanto in seguito a richieste esplicite.
- Monitorare heap libero e blocco massimo in `/api/state`, soprattutto con ricevitore e apprendimento. La misura di RAM al link è statica e non comprende heap allocato durante l'esecuzione.

## Limiti intenzionali e lavoro futuro

Compatibilità fisica con un climatizzatore ancora non identificato non garantibile. L'automazione toggle richiederebbe un riscontro affidabile dedicato e una politica distinta: non viene simulata con stime dei consumi. Per RAW occorre acquisire nuovamente se cambia setpoint/ventola.

Storico persistente delegato a un host locale con script incluso; ESP8266 mantiene solo RAM configurabile. Il rilevatore non attribuisce consumi continuativamente né identifica carichi multipli: la variazione riferita all'ultimo comando è un indizio. L'apprendimento misura il ritardo al primo superamento soglia, con risoluzione di circa 1 s e dipendenza dalla frequenza MQTT, non una misura di laboratorio del compressore.

OTA autenticato con verifica immagine del core; niente firma applicativa configurata, rollback A/B o aggiornamento remoto degli asset. Mantenere layout e compatibilità API negli aggiornamenti firmware; aggiornare gli asset via USB con backup e procedura di recupero se serve. Non sono presenti TLS MQTT/HTTPS sull'ESP8266: la protezione del trasporto è affidata alla rete/gateway.
