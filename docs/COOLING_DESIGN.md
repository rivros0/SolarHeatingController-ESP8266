# Valutazione estensione HEAT / COOL

**Stato: proposta progettuale, non ancora implementata nel firmware 0.1.0.**

Il raffrescamento è compatibile con l'architettura. MQTT, bilancio FV/carichi, priorità alla ricarica AGM, protezioni, OTA, sensore e storico sono riutilizzabili. La disponibilità del sole non autorizza da sola l'avvio: resta necessaria una richiesta termica e il rispetto delle condizioni energetiche.

## Due selezioni indipendenti

Separare:

- **Funzione climatica:** Riscaldamento HEAT / Raffrescamento COOL.
- **Gestione:** Automatico / Forza ON / Forza OFF / Diagnostica.

“Automatico” continua a significare gestione energetica del profilo climatico scelto. Non deve essere tradotto nella modalità IR AUTO del climatizzatore: quest'ultima potrebbe scegliere autonomamente caldo/freddo e rendere meno prevedibile il comportamento.

Per una prima estensione usare una scelta stagionale esplicita dalla dashboard. Il passaggio automatico HEAT↔COOL in base alla temperatura richiederebbe una fascia neutra, tempi fra cambio funzione e una politica distinta; non è necessario per il primo supporto al raffrescamento.

## Domanda termica

Esempi illustrativi, modificabili dopo il collaudo:

| Profilo | Richiesta di avvio | Arresto termico |
|---|---|---|
| HEAT | ambiente < 19 °C | ambiente >= 21 °C |
| COOL | ambiente > 26 °C | ambiente <= 25 °C |

Le condizioni devono essere coerenti con la relativa isteresi. La validazione richiede HEAT.start < HEAT.stop e COOL.start > COOL.stop, con separazione minima configurata. Ogni profilo ha setpoint IR e ventola propri; il setpoint deve consentire di raggiungere l'arresto termico del controller. Non basta invertire una scritta nella dashboard: vanno invertiti confronti, vincoli di configurazione, motivazioni e protezione termica in Forza ON.

Preferibile isolare una politica di domanda termica (`thermal_demand`) dall'autorizzazione energetica. L'energy manager riceve “richiesta attiva”, “limite termico raggiunto” e “sensore valido”, senza duplicare il bilancio FV per caldo e freddo. Il minimo di funzionamento non deve impedire un arresto per protezione batterie o limite termico.

## Comandi IR e cambio profilo

IRremoteESP8266 espone sia `stdAc::opmode_t::kHeat` sia `kCool`; il supporto sul climatizzatore concreto rimane da verificare. I protocolli nativi KELON/WHIRLPOOL restano soggetti alle stesse limitazioni toggle.

Per RAW servono catture distinte:

- HEAT ON con temperatura/ventola del profilo caldo.
- COOL ON con temperatura/ventola del profilo freddo.
- OFF verificato per HEAT e OFF verificato per COOL; uno slot condiviso è ammissibile solo se è provato che si tratta dello stesso OFF discreto in entrambi i casi.

Ogni profilo ha verifica e impronta dei propri slot. La verifica del caldo non abilita quella del freddo. Cambiare una cattura, il setpoint incorporato o la semantica revoca l'autorizzazione del relativo profilo. Nessun controllo di stima può trasformare un toggle in ON/OFF discreti.

Il cambio HEAT/COOL deve richiedere climatizzatore spento, reset delle finestre di stabilità e rispetto dell'anti-riavvio. Una prima implementazione può applicare il profilo al riavvio, come le altre configurazioni attuali: evita di sostituire codici IR durante un ciclo attivo e mantiene la ripartenza sicura in Forza OFF. L'ultima modalità gestionale salvata resta informativa.

## Potenza e apprendimento

Servono due profili di assorbimento: la potenza HEAT appresa non va assunta come potenza COOL. Conservare per ciascuno:

- potenza prevista e variazione tipica ON/OFF;
- ritardo avvio compressore;
- finestre di riferimento/osservazione e variabilità ammessa;
- esito e stato dell'apprendimento.

La procedura guidata deve indicare chiaramente il profilo che verrà acceso. Al cambio profilo, invalidare riferimento e stima precedente. La modulazione e gli altri carichi continuano a rendere incerta la stima da contatore aggregato.

La riserva di carica, le convenzioni di segno e le protezioni AGM restano comuni all'impianto. Il consumo previsto e i tempi possono essere specifici per il profilo. Non assumere che COOL consumi meno di HEAT.

## Umidità

SHT31 continua a mostrare umidità e temperatura. Per la prima estensione COOL, l'umidità rimane monitoraggio, senza richiedere né inviare automaticamente DRY. La deumidificazione è una terza funzione con logica e comandi propri, da valutare separatamente.

## Memoria e persistenza

Proposta schema v3: `climateMode` e due oggetti `profiles.heat` / `profiles.cool`, con parametri termici, IR e riconoscimento. Migrare la configurazione v2 nel profilo HEAT mantenendo soglie e credenziali; creare COOL non verificato e disabilitato. Non riutilizzare implicitamente codici, verifica o apprendimento del caldo.

Per contenere la RAM, conservare entrambi i profili su LittleFS ma caricare in RAM solo gli slot del profilo attivo. La selezione con riavvio consente di riutilizzare gli attuali due buffer ON/OFF e il buffer di cattura, senza raddoppiare l'allocazione permanente. I dati storici devono indicare il profilo degli eventi per distinguere riscaldamento e raffrescamento.

## Verifiche necessarie prima di dichiarare il supporto

1. Regressione dei 63 controlli attuali HEAT e aggiunta della matrice COOL.
2. Avvio solo sopra soglia COOL, arresto sotto soglia, isteresi e reset stabilità dopo variazioni.
3. Protezioni batterie, scadenza MQTT/sensore e durata Forza ON identiche per entrambi i profili; protezione contro sovraraffrescamento anche in Forza ON.
4. Cambio funzione solo da OFF e anti-riavvio rispettato; nessun passaggio automatico caldo/freddo.
5. Migrazione v2→v3 senza abilitare COOL né perdere credenziali; slot mancanti/corrotti o non verificati bloccano il solo profilo non sicuro.
6. Prove fisiche sul modello Hisense: COOL reale, setpoint, ventola e idempotenza ON/OFF; apprendimento separato.
7. Nuova compilazione per entrambe le schede e misura heap con il profilo COOL, ricevitore e OTA.

L'estensione è tecnicamente circoscritta, ma richiede modifiche coordinate a logica termica, schema, IR, apprendimento, dashboard e test. Il punto aperto più importante rimane la disponibilità di comandi discreti compatibili con il modello reale, per entrambe le funzioni.
