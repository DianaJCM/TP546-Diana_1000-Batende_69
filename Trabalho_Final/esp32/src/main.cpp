/*
 * ESP32 — Irrigação automática via MQTT (EMQX Cloud, TLS 8883)
 * Trabalho final TP546 — Batende Miheto e Diana Jasmin
 *
 * Hardware: sensor de umidade do solo (ADC GPIO ) + módulo relé (GPIO 0) + bomba
 * Wi-Fi e credenciais MQTT ficam em include/secrets.h (fora do GitHub).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

// ------------------------------------------------------------- Pinos
#define SENSOR_SOLO 7
#define RELE        0
#define RELE_ON     LOW    // relé ativo em nível baixo
#define RELE_OFF    HIGH

// Calibração: leia o ADC no Serial Monitor com o sensor no ar (seco) e na água
const int ADC_SECO    = 3500;
const int ADC_MOLHADO = 1500;

// ------------------------------------------- Controle automático (histerese)
const int SOLO_LIGA    = 30;   // liga a bomba com solo <= 30 %
const int SOLO_DESLIGA = 70;   // desliga a bomba com solo >= 70 %
const unsigned long MAX_BOMBA_MS = 2UL * 60UL * 1000UL;  // segurança: 2 min ligada no máximo
const unsigned long INTERVALO_MS = 2000;

// ------------------------------------------------------------ Tópicos
const String BASE     = TOPICO_BASE;
const String T_SOLO   = BASE + "/solo";
const String T_CMD    = BASE + "/bomba/cmd";
const String T_ESTADO = BASE + "/bomba/estado";
const String T_MODO   = BASE + "/modo";
const String T_STATUS = BASE + "/status";

WiFiMulti redes;               // várias redes Wi-Fi: conecta na que estiver ao alcance
bool wifiOk = false;
WiFiClientSecure redeSegura;
PubSubClient mqtt(redeSegura);

String modo = "AUTO";          // AUTO, ON ou OFF
bool bombaLigada = false;
unsigned long bombaLigadaEm = 0;
unsigned long ultimaLeitura = 0, ultimoWiFi = 0, ultimoMQTT = 0;
int ultimoSolo = 50;

// --------------------------------------------------------------------
void publicarModo() {
  if (mqtt.connected()) mqtt.publish(T_MODO.c_str(), modo.c_str(), true);
}

void definirBomba(bool ligar) {
  if (ligar == bombaLigada) return;          // só age (e publica) quando muda
  bombaLigada = ligar;
  bombaLigadaEm = ligar ? millis() : 0;
  digitalWrite(RELE, ligar ? RELE_ON : RELE_OFF);
  Serial.println(ligar ? "BOMBA LIGADA" : "BOMBA DESLIGADA");
  if (mqtt.connected()) mqtt.publish(T_ESTADO.c_str(), ligar ? "ON" : "OFF", true);
}

void controlarBomba() {
  bool ligar = bombaLigada;
  if (modo == "ON") ligar = true;
  else if (modo == "OFF") ligar = false;
  else {
    if (ultimoSolo <= SOLO_LIGA) ligar = true;
    else if (ultimoSolo >= SOLO_DESLIGA) ligar = false;   // entre 30 e 70: mantém
  }
  if (ligar && bombaLigada && millis() - bombaLigadaEm > MAX_BOMBA_MS) {
    Serial.println("[SEGURANCA] Tempo maximo da bomba atingido");
    ligar = false;
    modo = "AUTO";
    publicarModo();
  }
  definirBomba(ligar);
}

void aoReceber(char* topico, byte* payload, unsigned int tamanho) {
  String cmd;
  for (unsigned int i = 0; i < tamanho; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();
  Serial.printf("Comando MQTT: %s\n", cmd.c_str());
  if (cmd == "ON" || cmd == "OFF" || cmd == "AUTO") {
    modo = cmd;
    publicarModo();
    controlarBomba();
  }
}

void conectarMQTT() {
  String clientId = "esp32-planta-" + WiFi.macAddress();   // ID único (MAC)
  clientId.replace(":", "");
  Serial.println("Conectando MQTT...");
  // Last Will: o broker publica "offline" se a placa cair
  if (mqtt.connect(clientId.c_str(), MQTT_USUARIO, MQTT_SENHA,
                   T_STATUS.c_str(), 1, true, "offline")) {
    Serial.println("MQTT CONECTADO");
    mqtt.subscribe(T_CMD.c_str(), 1);
    mqtt.publish(T_STATUS.c_str(), "online", true);
    mqtt.publish(T_ESTADO.c_str(), bombaLigada ? "ON" : "OFF", true);
    publicarModo();
  } else {
    Serial.printf("Erro MQTT: %d\n", mqtt.state());
  }
}

// --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  digitalWrite(RELE, RELE_OFF);
  pinMode(RELE, OUTPUT);
  analogReadResolution(12);

  WiFi.mode(WIFI_STA);
  // Cadastra todas as redes definidas no secrets.h (WIFI_SSID_1 ... WIFI_SSID_3)
#ifdef WIFI_SSID_1
  redes.addAP(WIFI_SSID_1, WIFI_SENHA_1);
#endif
#ifdef WIFI_SSID_2
  redes.addAP(WIFI_SSID_2, WIFI_SENHA_2);
#endif
#ifdef WIFI_SSID_3
  redes.addAP(WIFI_SSID_3, WIFI_SENHA_3);
#endif
#ifdef WIFI_SSID          // compatível com o secrets.h antigo (uma rede só)
  redes.addAP(WIFI_SSID, WIFI_SENHA);
#endif
  Serial.println("Procurando redes Wi-Fi conhecidas...");

  // TLS: criptografa a conexão (não verifica o certificado — ok para o protótipo)
  redeSegura.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceber);
  Serial.println("SISTEMA DE IRRIGACAO INICIADO");
}

void loop() {
  // Wi-Fi e MQTT sem travar o loop
  if (WiFi.status() != WL_CONNECTED) {
    wifiOk = false;
    if (millis() - ultimoWiFi >= 10000) {
      ultimoWiFi = millis();
      Serial.println("Procurando redes Wi-Fi conhecidas...");
      redes.run(5000);   // tenta a rede conhecida com sinal mais forte
    }
  } else if (!wifiOk) {
    wifiOk = true;
    Serial.printf("WiFi OK: %s | IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else if (!mqtt.connected()) {
    if (millis() - ultimoMQTT >= 5000) { ultimoMQTT = millis(); conectarMQTT(); }
  } else {
    mqtt.loop();
  }

  if (millis() - ultimaLeitura < INTERVALO_MS) return;
  ultimaLeitura = millis();

  long soma = 0;
  for (int i = 0; i < 10; i++) { soma += analogRead(SENSOR_SOLO); delay(2); }
  int adc = soma / 10;
  ultimoSolo = constrain(map(adc, ADC_SECO, ADC_MOLHADO, 0, 100), 0, 100);

  Serial.printf("ADC: %d | Solo: %d%% | Bomba: %s | Modo: %s\n",
                adc, ultimoSolo, bombaLigada ? "ON" : "OFF", modo.c_str());

  if (mqtt.connected()) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", ultimoSolo);
    mqtt.publish(T_SOLO.c_str(), buf);
  }
  controlarBomba();
}
