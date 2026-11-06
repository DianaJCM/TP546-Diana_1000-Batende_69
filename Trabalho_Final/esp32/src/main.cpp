/*
 * ESP32 — Monitoramento e irrigação de plantas via MQTT (HiveMQ Cloud, TLS 8883)
 *
 * Sensores: DHT22 (GPIO 33), BH1750 (I2C 21/22), umidade do solo (ADC GPIO 35)
 * Atuador:  relé da bomba (GPIO 32)
 * Mesma pinagem e lógica do artigo de referência (Shteiwi & Okay, SIU 2026).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include "secrets.h"

// ------------------------------------------------------------- Pinos
#define PINO_DHT    33
#define PINO_SOLO   35
#define PINO_RELE   32
#define RELE_LIGADO HIGH   // troque para LOW se o seu relé for ativo em nível baixo

// Calibração do sensor de solo: leia o ADC no ar (seco) e na água (molhado)
const int ADC_SECO = 3200;
const int ADC_MOLHADO = 1300;

// --------------------------------------------------- Controle (modo AUTO)
const float SOLO_LIGA_ABAIXO = 30.0;
const float SOLO_DESLIGA_ACIMA = 70.0;
const unsigned long MAX_BOMBA_MS = 10UL * 60UL * 1000UL;  // 10 min
const unsigned long INTERVALO_MS = 5000;

// ------------------------------------------------------------ Tópicos
const String BASE = TOPICO_BASE;
const String T_TEMP   = BASE + "/temperatura";
const String T_UMID   = BASE + "/umidade";
const String T_LUX    = BASE + "/luz";
const String T_SOLO   = BASE + "/solo";
const String T_ESTADO = BASE + "/bomba/estado";
const String T_CMD    = BASE + "/bomba/cmd";
const String T_STATUS = BASE + "/status";

DHT dht(PINO_DHT, DHT22);
BH1750 luxSensor;
WiFiClientSecure redeSegura;
PubSubClient mqtt(redeSegura);

String modo = "AUTO";
bool bombaLigada = false;
unsigned long bombaLigadaEm = 0;
unsigned long ultimoEnvio = 0;
float ultimoSolo = 50;

// --------------------------------------------------------------------
void definirBomba(bool ligar) {
  if (ligar == bombaLigada) return;
  bombaLigada = ligar;
  bombaLigadaEm = ligar ? millis() : 0;
  digitalWrite(PINO_RELE, ligar ? RELE_LIGADO : !RELE_LIGADO);
  mqtt.publish(T_ESTADO.c_str(), ligar ? "ON" : "OFF", true);
  Serial.printf("[BOMBA] %s (modo %s)\n", ligar ? "LIGADA" : "DESLIGADA", modo.c_str());
}

void controlarBomba(float solo, bool leituraValida) {
  bool ligar = bombaLigada;
  if (modo == "ON") ligar = true;
  else if (modo == "OFF") ligar = false;
  else {  // AUTO, com histerese
    if (!leituraValida) ligar = false;          // segurança: sensor inválido
    else if (solo <= SOLO_LIGA_ABAIXO) ligar = true;
    else if (solo >= SOLO_DESLIGA_ACIMA) ligar = false;
  }
  if (ligar && bombaLigada && millis() - bombaLigadaEm > MAX_BOMBA_MS) {
    Serial.println("[SEGURANCA] Tempo maximo da bomba atingido");
    ligar = false;
    modo = "AUTO";
  }
  definirBomba(ligar);
}

void aoReceber(char* topico, byte* payload, unsigned int tamanho) {
  String cmd;
  for (unsigned int i = 0; i < tamanho; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();
  Serial.printf("[CMD] %s -> %s\n", topico, cmd.c_str());
  if (cmd == "ON" || cmd == "OFF" || cmd == "AUTO") {
    modo = cmd;
    controlarBomba(ultimoSolo, true);
  }
}

void conectarWiFi() {
  Serial.printf("Conectando ao Wi-Fi %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nWi-Fi OK, IP: %s\n", WiFi.localIP().toString().c_str());
}

void sincronizarHora() {
  // A hora certa ajuda o handshake TLS (o artigo também usa NTP)
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Sincronizando NTP");
  while (time(nullptr) < 1700000000) { delay(500); Serial.print("."); }
  Serial.println(" OK");
}

void conectarMQTT() {
  // Client ID único: usa o MAC da placa
  String clientId = "esp32-planta-" + WiFi.macAddress();
  clientId.replace(":", "");
  while (!mqtt.connected()) {
    Serial.printf("Conectando ao broker como %s ... ", clientId.c_str());
    // Last Will: o broker publica "offline" se a placa cair
    if (mqtt.connect(clientId.c_str(), MQTT_USUARIO, MQTT_SENHA,
                     T_STATUS.c_str(), 1, true, "offline")) {
      Serial.println("OK");
      mqtt.publish(T_STATUS.c_str(), "online", true);
      mqtt.subscribe(T_CMD.c_str(), 1);
    } else {
      Serial.printf("falhou (rc=%d), tentando em 5 s\n", mqtt.state());
      delay(5000);
    }
  }
}

float lerSolo() {
  long soma = 0;
  for (int i = 0; i < 10; i++) { soma += analogRead(PINO_SOLO); delay(5); }
  int adc = soma / 10;
  float pct = map(adc, ADC_SECO, ADC_MOLHADO, 0, 100);
  return constrain(pct, 0, 100);
}

// --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, !RELE_LIGADO);

  dht.begin();
  Wire.begin(21, 22);
  luxSensor.begin();

  conectarWiFi();
  sincronizarHora();

  // TLS: criptografa a conexão. setInsecure() não verifica o certificado do
  // servidor — suficiente para o protótipo. Para produção, use setCACert()
  // com o certificado raiz ISRG Root X1 (Let's Encrypt).
  redeSegura.setInsecure();

  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceber);
  mqtt.setBufferSize(512);
  conectarMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (millis() - ultimoEnvio < INTERVALO_MS) return;
  ultimoEnvio = millis();

  float t = dht.readTemperature();
  float u = dht.readHumidity();
  float lux = luxSensor.readLightLevel();
  float solo = lerSolo();
  bool dhtOk = !isnan(t) && !isnan(u);
  ultimoSolo = solo;

  char buf[16];
  if (dhtOk) {
    snprintf(buf, sizeof(buf), "%.1f", t); mqtt.publish(T_TEMP.c_str(), buf);
    snprintf(buf, sizeof(buf), "%.1f", u); mqtt.publish(T_UMID.c_str(), buf);
  } else {
    Serial.println("[ERRO] Falha na leitura do DHT22");
  }
  if (lux >= 0) { snprintf(buf, sizeof(buf), "%.0f", lux); mqtt.publish(T_LUX.c_str(), buf); }
  snprintf(buf, sizeof(buf), "%.0f", solo); mqtt.publish(T_SOLO.c_str(), buf);

  controlarBomba(solo, true);

  Serial.printf("T=%.1fC U=%.1f%% Luz=%.0flx Solo=%.0f%% Bomba=%s (%s)\n",
                t, u, lux, solo, bombaLigada ? "ON" : "OFF", modo.c_str());
}
