#pragma once
// ============================================================================
// CONFIGURAZIONE PRIVATA - SOLO SUL TUO COMPUTER
// ============================================================================
// Copiare questo file in include/private_settings.h e modificare SOLO la copia.
// Il file private_settings.h e' escluso da Git: non deve essere pubblicato.
// Valori applicati SOLO AL PRIMO AVVIO, quando LittleFS non contiene config.json.
// Dal secondo avvio prevalgono le impostazioni persistenti della dashboard.
// Per gli aggiornamenti OTA di GitHub non occorre includere queste credenziali.
//
// Requisiti:
// - Password amministratore: da 12 a 63 caratteri
// - SSID Wi-Fi: massimo 32 caratteri
// - Password Wi-Fi WPA2: 8-63 caratteri (lasciare vuota per reti aperte)
// - Non usare caratteri di controllo o password reali nel file .example.
// ============================================================================

#define SOLAR_WIFI_SSID             "INSERISCI_NOME_RETE_WIFI"
#define SOLAR_WIFI_PASSWORD         "INSERISCI_PASSWORD_WIFI"

#define SOLAR_ADMIN_USERNAME        "admin"
#define SOLAR_ADMIN_PASSWORD        "INSERISCI_PASSWORD_ADMIN_ALMENO_12_CARATTERI"

/// ===========================================================================
/// IP STATICO (facoltativo; lasciare vuota SOLAR_STATIC_IP per usare DHCP)
/// ===========================================================================
#define SOLAR_STATIC_IP             ""
#define SOLAR_STATIC_GATEWAY        ""
#define SOLAR_STATIC_SUBNET         "255.255.255.0"
#define SOLAR_STATIC_DNS1           ""
#define SOLAR_STATIC_DNS2           ""
