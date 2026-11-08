# Contenitore 3D per Solar Heating Controller (ESP8266)

Modello parametrico per **NodeMCU ESP8266**, modulo IR KY-005 e sensore SHT31. Versione preliminare: verificare le quote reali prima di stampare.

## Contenuto generato

- `SolarHeatingCase_BASE.step` — base CAD per SolidWorks
- `SolarHeatingCase_LID.step` — coperchio CAD per SolidWorks
- `SolarHeatingCase_ASSEMBLY.step` — assemblaggio chiuso
- `SolarHeatingCase_BASE.stl` — base pronta per slicer
- `SolarHeatingCase_LID.stl` — coperchio pronto per slicer
- `create_case.py` — modello parametrico CadQuery
- `converti_STEP_in_SLDPRT.vbs` — conversione locale in SolidWorks

## Generare i modelli

Da questa cartella, dopo l'installazione di Python 3.11 e CadQuery 2.x:

```bash
python -m pip install "cadquery==2.6.1"
python create_case.py
```

Un workflow GitHub Actions dedicato esegue lo stesso comando e prepara automaticamente un ZIP da scaricare tra gli **Artifacts** del workflow "Contenitore 3D - STEP e STL". Non è necessario installare CadQuery per scaricare l'archivio generato da GitHub.

## SolidWorks

I file STEP sono solidi CAD e possono essere importati in SolidWorks e salvati in formato `.SLDPRT`. In alternativa, su Windows con SolidWorks installato, tenere lo script VBS nella stessa cartella dei file STEP ed eseguire:

```cmd
cscript //nologo converti_STEP_in_SLDPRT.vbs
```

**Nota:** i file `.SLDPRT` non sono inclusi direttamente: la conversione nativa richiede SolidWorks. Dopo importazione le parti sono modificabili, ma la cronologia parametrica non viene ricostruita automaticamente.

## Quote iniziali e fissaggi

| Specifica | Valore |
|---|---|
| Ingombro del corpo | 110 × 78 mm |
| Altezza assemblata | 29,2 mm |
| Pareti | 2,4 mm |
| Fondo | 2,8 mm |
| Chiusura | 4 viti M3 |
| Staffe | 2 fori M4 |
| Apertura USB | Laterale |
| Trasmettitore IR | Foro frontale Ø 6,8 mm |
| SHT31 | Vano ventilato |

Materiale consigliato: PETG, layer 0,20 mm e almeno tre perimetri. La posizione della USB, delle fascette e del vano SHT31 va verificata sul materiale reale. Evitare che il calore dell'ESP8266 falsi la misura ambientale e non inserire conduttori a tensione di rete nel case.

L'archivio generato dal workflow comprende i file STEP/STL e i sorgenti, ma **non** l'anteprima PNG presente nel pacchetto originale.
