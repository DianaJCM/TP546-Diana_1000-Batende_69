/*
 * =====================================================================
 *  Sistema IoT de Monitoramento da Umidade do Solo e Irrigação Remota
 *  TP546 – Internet das Coisas e Redes Veiculares – Inatel
 *  Batende Miheto · Diana Jazmin Castro Meneces
 * ---------------------------------------------------------------------
 *  Hardware: ESP32 DevKit V1 + sensor de solo (FC-28/YL-69, saída AO)
 *            + módulo relé (ativo em LOW) + mini bomba d'água
 *  Nuvem:    EMQX Cloud (MQTT com TLS, porta 8883)
 *  Painel:   dashboard web, MQTTX e app IoT MQTT Panel no celular
 *
 *  Senhas e redes ficam em  secrets.h  (aba ao lado, fora do GitHub).
 * =====================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

// ============================ 1. PINOS ================================
#define SENSOR_SOLO  34       // AO do sensor de solo (entrada analógica)
#define RELE         5        // IN do módulo relé
#define RELE_ON      LOW      // relé ativo em nível baixo
#define RELE_OFF     HIGH

// ======================= 2. CALIBRAÇÃO DO SENSOR ======================
const int ADC_SECO    = 4084;   // sensor no AR / solo seco    -> 0 %
const int ADC_MOLHADO = 2276;   // sensor na ÁGUA / solo úmido -> 100 %

// ===================== 3. CONTROLE DA IRRIGAÇÃO =======================
const int SOLO_LIGA    = 40;                              // AUTO: liga com solo <= 40 %
const int SOLO_DESLIGA = 70;                              // AUTO: desliga com solo >= 70 %
const unsigned long MAX_BOMBA_MS = 2UL * 60UL * 1000UL;   // segurança: 2 min ligada
const unsigned long INTERVALO_MS = 2000;                  // leitura a cada 2 s

// ============================ 4. TÓPICOS ==============================
const String BASE     = TOPICO_BASE;              // inatel/batende/planta01
const String T_SOLO   = BASE + "/solo";           // publica: umidade do solo (%)
const String T_ADC    = BASE + "/adc";            // publica: leitura bruta (calibração)
const String T_ESTADO = BASE + "/bomba/estado";   // publica: ON / OFF (retain)
const String T_MODO   = BASE + "/modo";           // publica: ON / OFF / AUTO (retain)
const String T_STATUS = BASE + "/status";         // publica: online / offline (Last Will)
const String T_ALERTA = BASE + "/alerta";         // publica: avisos
const String T_CMD    = BASE + "/bomba/cmd";      // assina:  ON / OFF / AUTO

// =========================== 5. VARIÁVEIS =============================
WiFiMulti redes;
WiFiClientSecure redeSegura;
PubSubClient mqtt(redeSegura);

String modo = "AUTO";
bool bombaLigada = false;
bool sensorOk = true;
bool wifiAvisado = false;
int  soloPct = 50;
unsigned long bombaLigadaEm = 0, ultimaLeitura = 0, ultimoWiFi = 0, ultimoMQTT = 0;

// ============================ 6. FUNÇÕES ==============================
void publicar(const String& topico, const String& msg, bool reter = false) {
  if (mqtt.connected()) mqtt.publish(topico.c_str(), msg.c_str(), reter);
}

void definirBomba(bool ligar) {
  if (ligar == bombaLigada) return;               // só age quando muda
  bombaLigada = ligar;
  bombaLigadaEm = ligar ? millis() : 0;
  digitalWrite(RELE, ligar ? RELE_ON : RELE_OFF);
  Serial.println(ligar ? ">> BOMBA LIGADA" : ">> BOMBA DESLIGADA");
  publicar(T_ESTADO, ligar ? "ON" : "OFF", true);
}

void controlarBomba() {
  bool ligar = bombaLigada;
  if (modo == "ON")       ligar = true;
  else if (modo == "OFF") ligar = false;
  else {                                          // AUTO com histerese
    if (!sensorOk)                    ligar = false;   // sensor desligado: não rega
    else if (soloPct <= SOLO_LIGA)    ligar = true;
    else if (soloPct >= SOLO_DESLIGA) ligar = false;   // entre 40 e 70: mantém
  }
  // Segurança: tempo máximo de bomba ligada
  if (ligar && bombaLigada && millis() - bombaLigadaEm > MAX_BOMBA_MS) {
    Serial.println("[SEGURANCA] tempo maximo da bomba atingido");
    publicar(T_ALERTA, "Tempo maximo da bomba atingido - voltando para AUTO");
    ligar = false;
    modo = "AUTO";
    publicar(T_MODO, modo, true);
  }
  definirBomba(ligar);
}

void aoReceber(char* topico, byte* payload, unsigned int tamanho) {
  String cmd;
  for (unsigned int i = 0; i < tamanho; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();
  Serial.printf("Comando recebido: %s\n", cmd.c_str());
  if (cmd == "ON" || cmd == "OFF" || cmd == "AUTO") {
    modo = cmd;
    publicar(T_MODO, modo, true);
    controlarBomba();
  }
}

void conectarMQTT() {
  String id = "esp32-planta-" + WiFi.macAddress();    // Client ID único
  id.replace(":", "");
  Serial.print("Conectando MQTT... ");
  if (mqtt.connect(id.c_str(), MQTT_USUARIO, MQTT_SENHA,
                   T_STATUS.c_str(), 1, true, "offline")) {   // Last Will
    Serial.println("MQTT CONECTADO");
    mqtt.subscribe(T_CMD.c_str(), 1);
    publicar(T_STATUS, "online", true);
    publicar(T_ESTADO, bombaLigada ? "ON" : "OFF", true);
    publicar(T_MODO, modo, true);
  } else {
    // 4 ou 5 = usuário/senha errados | -2 = sem acesso ao broker
    Serial.printf("falhou (rc=%d)\n", mqtt.state());
  }
}

void lerSolo() {
  long soma = 0;
  for (int i = 0; i < 10; i++) { soma += analogRead(SENSOR_SOLO); delay(10); }
  int adc = soma / 10;

  // Sensor sem energia ou fio solto costuma dar ~0 fixo.
  // (Solo muito seco dá ~4095, isso é normal e NÃO é erro.)
  bool valido = adc > 50;
  if (valido != sensorOk) {
    sensorOk = valido;
    publicar(T_ALERTA, valido ? "Sensor de solo OK"
                              : "Sensor de solo invalido - confira os fios");
  }

  soloPct = constrain(map(adc, ADC_SECO, ADC_MOLHADO, 0, 100), 0, 100);
  Serial.printf("ADC=%d | Solo=%d%% | Bomba=%s | Modo=%s%s\n", adc, soloPct,
                bombaLigada ? "ON" : "OFF", modo.c_str(),
                sensorOk ? "" : " | SENSOR INVALIDO");
  publicar(T_SOLO, String(soloPct));
  publicar(T_ADC, String(adc));
}

// ============================== SETUP =================================
void setup() {
  Serial.begin(115200);
  digitalWrite(RELE, RELE_OFF);                   // relé desligado antes de virar saída
  pinMode(RELE, OUTPUT);

  pinMode(SENSOR_SOLO, INPUT);
  analogReadResolution(12);                       // 0 a 4095
  analogSetPinAttenuation(SENSOR_SOLO, ADC_11db); // lê até ~3,3 V

  WiFi.mode(WIFI_STA);
#ifdef WIFI_SSID_1
  redes.addAP(WIFI_SSID_1, WIFI_SENHA_1);
#endif
#ifdef WIFI_SSID_2
  redes.addAP(WIFI_SSID_2, WIFI_SENHA_2);
#endif
#ifdef WIFI_SSID_3
  redes.addAP(WIFI_SSID_3, WIFI_SENHA_3);
#endif

  redeSegura.setInsecure();                       // TLS ativo (sem checar certificado)
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceber);
  Serial.println("SISTEMA DE IRRIGACAO INICIADO");
}

// =============================== LOOP =================================
void loop() {
  // Wi-Fi: conecta na rede conhecida com melhor sinal
  if (WiFi.status() != WL_CONNECTED) {
    wifiAvisado = false;
    if (millis() - ultimoWiFi >= 10000) {
      ultimoWiFi = millis();
      Serial.println("Procurando Wi-Fi...");
      redes.run(5000);
    }
  } else {
    if (!wifiAvisado) {
      wifiAvisado = true;
      Serial.printf("WiFi OK: %s | IP %s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
    if (!mqtt.connected()) {
      if (millis() - ultimoMQTT >= 5000) { ultimoMQTT = millis(); conectarMQTT(); }
    } else {
      mqtt.loop();
    }
  }

  // Leitura e controle a cada 2 s (funciona mesmo sem internet)
  if (millis() - ultimaLeitura >= INTERVALO_MS) {
    ultimaLeitura = millis();
    lerSolo();
    controlarBomba();
  }
}