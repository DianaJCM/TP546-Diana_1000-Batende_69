"""
Simulador de um sistema IoT de monitoramento e irrigação de plantas.

Simula o ESP32 com sensores DHT22 (temperatura/umidade do ar), BH1750 (luz)
e sensor de umidade do solo, e uma bomba d'água controlada por relé.
Publica os dados num broker MQTT na nuvem (HiveMQ Cloud, TLS porta 8883)
e assina o tópico de comando da bomba (ON / OFF / AUTO).

Uso:
    pip install paho-mqtt
    copie config_exemplo.py para config.py e preencha seus dados
    python simulador.py
"""

import json
import random
import ssl
import time

import paho.mqtt.client as mqtt

import config

# ---------------------------------------------------------------- Tópicos
BASE = config.TOPICO_BASE  # ex.: "inatel/batende/planta01"
T_TEMP = f"{BASE}/temperatura"
T_UMID = f"{BASE}/umidade"
T_LUX = f"{BASE}/luz"
T_SOLO = f"{BASE}/solo"
T_BOMBA_ESTADO = f"{BASE}/bomba/estado"
T_BOMBA_CMD = f"{BASE}/bomba/cmd"
T_STATUS = f"{BASE}/status"
T_JSON = f"{BASE}/json"

# ---------------------------------------------------- Parâmetros de controle
SOLO_LIGA_ABAIXO = 30.0     # % — liga a bomba no modo AUTO
SOLO_DESLIGA_ACIMA = 70.0   # % — desliga a bomba no modo AUTO (histerese)
MAX_BOMBA_LIGADA_S = 60     # segurança: tempo máximo com a bomba ligada
INTERVALO_S = 5             # período de publicação

# ------------------------------------------------------- Estado simulado
estado = {
    "temperatura": 24.0,   # °C  (DHT22: -40 a 80)
    "umidade": 55.0,       # %   (DHT22: 0 a 100)
    "luz": 300.0,          # lux (BH1750: 0 a 65535)
    "solo": 45.0,          # %
    "modo": "AUTO",        # AUTO, ON ou OFF
    "bomba": False,
    "bomba_ligada_em": None,
}


def limitar(valor, minimo, maximo):
    return max(minimo, min(maximo, valor))


def atualizar_sensores():
    """Passeio aleatório: cada leitura varia um pouco em relação à anterior."""
    e = estado
    e["temperatura"] = limitar(e["temperatura"] + random.uniform(-0.3, 0.3), 10, 45)
    e["umidade"] = limitar(e["umidade"] + random.uniform(-1.0, 1.0), 20, 95)
    e["luz"] = limitar(e["luz"] + random.uniform(-40, 40), 0, 2000)

    # O solo seca devagar (mais rápido com calor) e molha rápido com a bomba.
    secagem = 0.8 + 0.05 * (e["temperatura"] - 20)
    if e["bomba"]:
        e["solo"] += random.uniform(4.0, 6.0)
    else:
        e["solo"] -= max(0.3, secagem) + random.uniform(0, 0.4)
    e["solo"] = limitar(e["solo"], 0, 100)


def controlar_bomba(cliente):
    """Lógica igual à do artigo: modo AUTO com histerese + tempo máximo."""
    e = estado
    ligar = e["bomba"]

    if e["modo"] == "ON":
        ligar = True
    elif e["modo"] == "OFF":
        ligar = False
    else:  # AUTO
        if e["solo"] <= SOLO_LIGA_ABAIXO:
            ligar = True
        elif e["solo"] >= SOLO_DESLIGA_ACIMA:
            ligar = False

    # Segurança: não deixa a bomba ligada além do tempo máximo
    if ligar and e["bomba_ligada_em"] is not None:
        if time.time() - e["bomba_ligada_em"] > MAX_BOMBA_LIGADA_S:
            print("[SEGURANÇA] Tempo máximo da bomba atingido — desligando.")
            ligar = False
            e["modo"] = "AUTO"

    if ligar != e["bomba"]:
        e["bomba"] = ligar
        e["bomba_ligada_em"] = time.time() if ligar else None
        cliente.publish(T_BOMBA_ESTADO, "ON" if ligar else "OFF", qos=1, retain=True)
        print(f"[BOMBA] {'LIGADA' if ligar else 'DESLIGADA'} (modo {e['modo']})")


# ---------------------------------------------------------- Callbacks MQTT
def ao_conectar(cliente, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("Conectado ao broker!")
        cliente.publish(T_STATUS, "online", qos=1, retain=True)
        cliente.subscribe(T_BOMBA_CMD, qos=1)
        print(f"Assinando: {T_BOMBA_CMD}")
    else:
        print(f"Falha na conexão: {reason_code}")


def ao_receber(cliente, userdata, msg):
    cmd = msg.payload.decode().strip().upper()
    print(f"[CMD] {msg.topic} -> {cmd}")
    if cmd in ("ON", "OFF", "AUTO"):
        estado["modo"] = cmd
        controlar_bomba(cliente)
    else:
        print("Comando inválido. Use ON, OFF ou AUTO.")


def ao_desconectar(cliente, userdata, flags, reason_code, properties):
    print(f"Desconectado ({reason_code}). Tentando reconectar...")


# ------------------------------------------------------------------- Main
def main():
    cliente = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=config.CLIENT_ID,  # precisa ser ÚNICO no broker
    )
    cliente.username_pw_set(config.MQTT_USUARIO, config.MQTT_SENHA)
    cliente.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)  # TLS na porta 8883

    # Last Will: se o simulador cair, o broker publica "offline"
    cliente.will_set(T_STATUS, "offline", qos=1, retain=True)

    cliente.on_connect = ao_conectar
    cliente.on_message = ao_receber
    cliente.on_disconnect = ao_desconectar
    cliente.reconnect_delay_set(min_delay=1, max_delay=30)

    print(f"Conectando em {config.MQTT_HOST}:{config.MQTT_PORTA} ...")
    cliente.connect(config.MQTT_HOST, config.MQTT_PORTA, keepalive=60)
    cliente.loop_start()

    try:
        while True:
            atualizar_sensores()
            controlar_bomba(cliente)

            e = estado
            leituras = {
                T_TEMP: f"{e['temperatura']:.1f}",
                T_UMID: f"{e['umidade']:.1f}",
                T_LUX: f"{e['luz']:.0f}",
                T_SOLO: f"{e['solo']:.0f}",
            }
            for topico, valor in leituras.items():
                cliente.publish(topico, valor, qos=0)

            # Mensagem única em JSON (útil para banco de dados / dashboards)
            cliente.publish(T_JSON, json.dumps({
                "temperatura": round(e["temperatura"], 1),
                "umidade": round(e["umidade"], 1),
                "luz": round(e["luz"]),
                "solo": round(e["solo"]),
                "bomba": "ON" if e["bomba"] else "OFF",
                "modo": e["modo"],
                "ts": int(time.time()),
            }), qos=0)

            print(f"T={leituras[T_TEMP]}°C  U={leituras[T_UMID]}%  "
                  f"Luz={leituras[T_LUX]}lx  Solo={leituras[T_SOLO]}%  "
                  f"Bomba={'ON' if e['bomba'] else 'OFF'} ({e['modo']})")
            time.sleep(INTERVALO_S)
    except KeyboardInterrupt:
        print("\nEncerrando...")
    finally:
        cliente.publish(T_STATUS, "offline", qos=1, retain=True).wait_for_publish(2)
        cliente.loop_stop()
        cliente.disconnect()


if __name__ == "__main__":
    main()
