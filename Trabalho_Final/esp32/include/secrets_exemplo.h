// Copie este arquivo para secrets.h (mesma pasta) e preencha.
// O secrets.h fica no .gitignore - nao suba senhas para o GitHub.
#pragma once

// Redes Wi-Fi que o ESP32 conhece (2,4 GHz, so senha, com internet).
// Ele conecta na que estiver ao alcance. Apague as que nao usar.
#define WIFI_SSID_1    "POCO F4 GT JHAZ"
#define WIFI_SENHA_1   "s19051698"

#define WIFI_SSID_2    "wifi_de_casa"
#define WIFI_SENHA_2   "senha_2"

#define WIFI_SSID_3    "Ivan_5Ghz"
#define WIFI_SENHA_3   "salima13"




#define MQTT_HOST      "xxxxxxxx.emqxsl.com"
#define MQTT_PORTA     8883
#define MQTT_USUARIO   "Batende_Diana"
#define MQTT_SENHA     "Batende_Diana"

#define TOPICO_BASE    "inatel/batende/planta01"
