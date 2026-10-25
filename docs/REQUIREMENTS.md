# Copertura requisiti ricevuti

L'allegato di progetto termina alla sezione 10 con “Gestire versionamento e mig”. È stata implementata una struttura versionata con migrazione v1→v2; nessun requisito successivo non presente viene presunto come ricevuto.

| Area | Implementazione | Condizione / limite |
|---|---|---|
| ESP8266 Arduino | PlatformIO NodeMCU e D1 Mini | 4 MB reali |
| Trasmettitore IR D5 | IRremoteESP8266 | compatibilità e cablaggio da verificare |
| Hisense e selezione protocollo | RAW, KELON, WHIRLPOOL_AC; identificazione KELON168 | nativi toggle in diagnostica, automazione con RAW discreto |
| HEAT / temperatura / ventola | sintesi nativa nei protocolli di test; RAW catturato | RAW incorpora parametri e non è riscritto |
| Ricevitore opzionale D6 | decoder, cattura e salvataggio ON/OFF | 512 durate max, 38 kHz |
| SHT31 D2/D1 | temperatura/umidità, EMA, errori e recupero | 0x44; automazione bloccata su errore |
| SolarAssistant MQTT | broker/porta/auth/topic configurabili, scoperta limitata | payload scalari W/V/A/% |
| Retained/scadenza/anomalie | flag retained rifiutato; scadenza per campione e invalidazione | origine deve pubblicare valori attuali |
| Bilancio e priorità carica | surplus differenziato acceso/spento, veto carica/import | conservativo, richiede corretta aggregazione AC/FV |
| Stabilità/isteresi/minimi | timer configurabili, protezione prioritaria | soglie iniziali da validare |
| AGM | potenza + V/A opzionali, SOC informativo | valori V/A specifici del banco da configurare |
| Stato comandato/stimato/unknown | separati, confidenza limitata | contatore aggregato non identifica con certezza |
| Apprendimento | riferimento → ON → osservazione → OFF → salvataggio | richiede supervisione e carichi stabili |
| Modalità | Automatico / Forza ON / Forza OFF / Diagnostica | riavvio sicuro; toggle solo test confermati |
| Dashboard e configurazione | responsive, grafici canvas, campi tutte le soglie | no CDN né framework |
| Storico e log | RAM circolare, export e logger CSV esterno | persistenza su host LAN, non flash periodica |
| OTA obbligatorio | `/update` autenticato e CSRF, U_FLASH, esito/errori | no A/B; USB per recovery/asset |
| Configurazione persistente | schema v2, backup, salvataggio esplicito | password in chiaro flash, mai negli export |
| Open source GitHub | MIT, CI, codice e documentazione | dipendenze con licenze proprie |

L'assenza dell'hardware impedisce di dichiarare collaudati sensore, portata IR, protocollo del modello reale, topic specifici dell'inverter, prestazioni di heap sotto carico o upload OTA sul dispositivo. Sono forniti test software e una procedura di messa in servizio per queste verifiche.
