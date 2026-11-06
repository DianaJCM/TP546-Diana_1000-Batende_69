# Monitoramento e Irrigação de Plantas com IoT (ESP32 + MQTT na nuvem)

Trabalho final de IoT — Inatel. Sistema que mede temperatura, umidade do ar,
luz e umidade do solo, publica os dados num broker MQTT na nuvem (HiveMQ Cloud,
TLS) e controla uma bomba d'água remotamente (ON / OFF / AUTO).

Pode rodar de duas formas:
- **`simulacao_python/`** — simulação com a biblioteca Paho (sem hardware)
- **`esp32/`** — implementação real com ESP32 (PlatformIO)

## Arquitetura

```
[DHT22, BH1750, Solo] -> [ESP32 ou simulador Python] --MQTT/TLS 8883--> [HiveMQ Cloud] <--> [MQTTX / Blynk]
                                 ^                                                              |
                                 +------------------- bomba/cmd (ON/OFF/AUTO) -----------------+
```

## Tópicos MQTT

| Tópico (base: `inatel/batende/planta01`) | Sentido | Conteúdo |
|---|---|---|
| `.../temperatura` | publica | °C |
| `.../umidade` | publica | % |
| `.../luz` | publica | lux |
| `.../solo` | publica | % |
| `.../json` | publica | todas as leituras em JSON (só no simulador) |
| `.../bomba/estado` | publica (retain) | ON / OFF |
| `.../status` | publica (retain + Last Will) | online / offline |
| `.../bomba/cmd` | **assina** | ON / OFF / AUTO |

No modo AUTO a bomba liga com solo ≤ 30 % e desliga com solo ≥ 70 % (histerese),
com um tempo máximo de funcionamento por segurança.

---

## Passo 1 — Criar o broker no HiveMQ Cloud

1. Acesse https://console.hivemq.cloud e crie uma conta.
2. Crie um cluster gratuito (*Serverless / Free*).
3. Copie a **URL do cluster** (algo como `abc123.s1.eu.hivemq.cloud`). A porta é **8883** (TLS).
4. Em **Access Management**, crie um usuário e senha (credenciais MQTT).
5. Teste em **Web Client** dentro do console: conecte e assine `inatel/batende/#`.

> O professor avisou que o plano gratuito do HiveMQ Cloud pode ser desativado no fim do ano.
> Para o prazo do trabalho serve; se der problema, o EMQX Cloud Serverless funciona igual
> (só trocar host, porta 8883, usuário e senha).

## Passo 2 — Conectar o MQTTX (dashboard)

1. Instale o MQTTX: https://mqttx.app
2. **New Connection**:
   - Host: `mqtts://` + URL do cluster
   - Port: `8883`
   - Client ID: um diferente do simulador/ESP32 (ex.: `mqttx-batende`)
   - Username / Password: os do Passo 1
   - SSL/TLS: **ligado**, *CA signed server certificate*
3. **Connect** → **New Subscription** → `inatel/batende/planta01/#`
4. Para controlar a bomba: publique `ON`, `OFF` ou `AUTO` no tópico
   `inatel/batende/planta01/bomba/cmd`.

## Passo 3a — Rodar a simulação em Python

```bash
cd simulacao_python
pip install paho-mqtt
copy config_exemplo.py config.py   # no Windows (Linux/Mac: cp)
# edite config.py com host, usuário e senha
python simulador.py
```

## Passo 3b — Rodar no ESP32 (opcional)

1. VS Code + extensão **PlatformIO**. Abra a pasta `esp32/`.
2. Copie `include/secrets_exemplo.h` para `include/secrets.h` e preencha Wi-Fi e MQTT.
3. **Build** e **Upload**; abra o **Serial Monitor** (115200).
4. Calibre `ADC_SECO` e `ADC_MOLHADO` no `main.cpp` (sensor no ar e na água).

Ligações: DHT22 → GPIO 33 · BH1750 SDA/SCL → GPIO 21/22 · Solo AO → GPIO 35 · Relé IN → GPIO 32.

## Segurança

- Conexão criptografada (TLS, porta 8883) e autenticada (usuário/senha).
- Tópico base específico; Client ID único (MAC da placa no ESP32).
- Credenciais fora do Git (`config.py` e `secrets.h` estão no `.gitignore`).

## Referência

A. N. A. Shteiwi e F. Y. Okay, "Plant Monitoring and Irrigation System with ESP32,
MQTT, and HomeKit Integration via Homebridge", SIU 2026, IEEE.
DOI: 10.1109/SIU71813.2026.11637064
