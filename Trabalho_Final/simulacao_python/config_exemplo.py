# Copie este arquivo para config.py e preencha com os dados do SEU cluster.
# O config.py está no .gitignore — não suba a senha para o GitHub.

MQTT_HOST = "a1b2c3d4.ala.us-east-1.emqxsl.com"# URL do cluster no HiveMQ Cloud
MQTT_PORTA = 8883                                   # porta TLS
MQTT_USUARIO = "planta01"
MQTT_SENHA = "a_senha_que_voce_criou_ali"

# Cada dispositivo precisa de um Client ID diferente (o MQTTX usa outro).
CLIENT_ID = "simulador-planta-01"

# Tópico base específico, para não misturar com outros usuários.
TOPICO_BASE = "inatel/batende/planta01"

