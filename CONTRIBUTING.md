# Contributing

Aprire issue con modello scheda, firmware, versione inverter/SolarAssistant, modello climatizzatore e passi riproducibili. Non condividere password, token, dump completi della flash o dati di rete sensibili.

Per le modifiche alla logica aggiungere casi che riproducono il problema in `test/native/test_control.cpp`; eseguire `sh scripts/test.sh`, compilazione e buildfs per entrambe le schede. Non aggirare la distinzione fra stato comandato e stimato o il blocco toggle. Documentare i test fisici realmente eseguiti e tenere separati quelli simulati.
