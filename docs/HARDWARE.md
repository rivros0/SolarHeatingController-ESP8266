# Hardware

## Piedini

| Funzione | Etichetta NodeMCU / D1 Mini | GPIO |
|---|---|---|
| Trasmettitore IR | D5 | 14 |
| Ricevitore 38 kHz opzionale | D6 | 12 |
| SHT31 SDA | D2 | 4 |
| SHT31 SCL | D1 | 5 |
| SHT31 VCC | 3V3 | — |
| Massa comune | GND | — |

Etichette D1/D2 ecc. non sono numeri GPIO. Si richiedono schede con flash reale da 4 MB; verificare i cloni prima dell'uso. GPIO di boot 0/2/15 non sono usati per le periferiche. SHT31 indirizzo predefinito 0x44, pull-up I²C a **3,3 V**, non a 5 V. Se il breakout include regolatore/pull-up, verificarne lo schema. Per sensori configurati a 0x45 modificare `sht.begin(0x44)` nel firmware.

## KY-005 e pilotaggio IR

**La piedinatura S/+/− e la presenza della resistenza variano fra moduli e cloni.** Prima del collegamento identificare LED, resistenze e pin con schema/datasheet o misure a modulo disalimentato. Non collegare un pin sconosciuto a un GPIO. La linea D5 è il segnale dati; il LED deve avere un limitatore di corrente. Non alimentare direttamente un LED ad alta corrente da GPIO14.

Schema consigliato per un LED IR nudo o un modulo passivo compatibile, dopo verifica della sua circuiteria:

```text
3V3 ── R_LED ── anodo LED IR
                 catodo ── collettore NPN (es. 2N2222)
GPIO14 ── R_BASE ── base       emettitore ── GND
                     │
                  100 kΩ
                     │
                    GND
```

Usare un transistor con guadagno, corrente e dissipazione adeguati. Collegamento attivo alto, coerente con IRsend. Se il modulo ha già un driver, seguire il suo schema invece di aggiungere un secondo circuito incompatibile. I pin di alimentazione di un modulo attivo vanno collegati alla tensione supportata dal produttore, con massa comune e ingresso compatibile 3,3 V.

Dimensionamento **esemplificativo**, non universale: con alimentazione 3,3 V, caduta LED 1,2 V, Vce sat 0,2 V e corrente impulsiva desiderata 20 mA:

```text
R_LED = (3,3 - 1,2 - 0,2) / 0,020 = 95 Ω → 100 Ω
P_R circa I²R = 0,04 W → resistenza 0,25 W
R_BASE circa (3,3 - 0,8) / 0,002 = 1250 Ω → 1,2 kΩ
```

Verificare la corrente massima del LED **anche per la durata e duty-cycle reali dei burst IR** e quella ammessa dal transistor. Se c'è già una resistenza sul modulo, considerarla nel calcolo complessivo. Non assumere che il pin S di un KY-005 sia un ingresso logico ad alta impedenza: in alcune varianti è collegato direttamente al LED.

Un condensatore di disaccoppiamento vicino al circuito IR e alimentazione USB stabile aiutano a evitare reset durante trasmissione. Posizionare il trasmettitore davanti al ricevitore del climatizzatore e verificare portata/angolo sul dispositivo reale.

## Ricevitore opzionale

VS1838B o equivalente a 38 kHz, alimentabile e con uscita compatibile a 3,3 V. Verificare l'ordine dei pin nel datasheet della variante, senza dedurlo dall'aspetto del contenitore. OUT→GPIO12, GND comune, VCC compatibile; disaccoppiamento secondo datasheet. Attivare `receiver` nella configurazione e riavviare. Disabilitato, il firmware non alloca il relativo buffer.

### Modulo a tre pin G / R / Y (ricevitore IR trovato durante il collaudo)

Il modulo fotografato dall'utente riporta le lettere **G**, **R**, **Y** sul connettore. Su questa famiglia di moduli a ricevitore IR la corrispondenza normalmente utilizzata e':

| Sigla sulla scheda | Funzione prevista | NodeMCU / D1 Mini | GPIO |
| --- | --- | --- | --- |
| `G` | Ground, massa | `GND` | — |
| `R` | Alimentazione del ricevitore | `3V3` | — |
| `Y` | Uscita digitale demodulata | `D6` | `12` |

**IMPORTANTE:** la presenza delle sole lettere non certifica il circuito interno o l'ordine fisico dei piedini. Prima di alimentare il modulo, verificare le serigrafie associate ai singoli pin, la compatibilita' con **3,3 V** e, se disponibile, lo schema del produttore o un controllo a multimetro con alimentazione scollegata. **Non collegare R a 5 V** durante il collaudo con ESP8266 e non collegare Y a un GPIO finche' la funzione dei pin non e' confermata.

Schema logico, **non** vista meccanica del connettore:

```text
Modulo IR G / R / Y                NodeMCU ESP8266
-------------------                --------------
G (massa)       -----------------> GND
R (alimentazione) ----------------> 3V3
Y (uscita dati)  -----------------> D6 / GPIO12
```

**Procedura di acquisizione con la dashboard a schede:**

1. Spegnere NodeMCU e collegare il ricevitore verificando le sigle **G, R, Y**.
2. Aprire **Configurazione** > gruppo **Infrarossi**: abilitare `Ricevitore GPIO12 abilitato`. Salvare e riavviare dopo aver confermato che il climatizzatore sia fisicamente spento.
3. Aprire **Panoramica** e scegliere **Diagnostica** come modalita' di controllo; poi aprire la scheda **Infrarossi**.
4. Puntare il telecomando originale verso il ricevitore e premere **una volta** il tasto di interesse. Nella scheda **Infrarossi**, premere **Leggi acquisizione**.
5. Verificare `protocol`, `length` e l'array `raw`. Una lunghezza nulla significa che non e' stata ottenuta una cattura valida; non salvare. Una cattura leggibile non prova ancora che il comando sia riproducibile dal trasmettitore.
6. **Prima** acquisire e collaudare separatamente `HEAT ON` e `OFF`; solo successivamente considerare il comando `DRY` come esperimento separato. Il firmware **non** implementa attualmente una terza memoria comandi DRY: i pulsanti `Salva come ON` e `Salva come OFF` sovrascrivono gli slot destinati al riscaldamento e allo spegnimento.

**Avvertenza di sicurezza:** non premere `Salva come ON` o `Salva come OFF` per il segnale DRY. Salvare una nuova cattura modifica LittleFS e revoca per sicurezza la verifica IR e l'abilitazione dell'automazione fino a un nuovo collaudo. I telecomandi dei climatizzatori possono inviare codici di stato completi e molto lunghi: la cattura puo' superare il limite attuale di 512 durate.


Le catture RAW sono limitate a 512 durate da 16 bit a 38 kHz. Overflow, sequenze troppo lunghe o non valide non vengono salvati. Un ricevitore demodulante a 38 kHz non misura la frequenza portante: per telecomandi diversi verificare separatamente la portante. Allontanare ricevitore e trasmettitore per evitare auto-ricezione.

Non intervenire su rete AC o cablaggi dell'inverter: questo progetto opera solo su bassa tensione, MQTT e IR.
