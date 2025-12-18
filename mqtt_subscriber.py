# mqtt_subscriber.py

import paho.mqtt.client as mqtt
import os
# --- Puxa as variáveis de configuração do arquivo config.py ---
from config import *

# -----------------------------------------------------------

def on_message(client, userdata, msg):
    """
    Processa a mensagem recebida e imprime o tópico e o payload (valor).
    """
    try:
        payload_str = msg.payload.decode()
        
        print("-" * 30)
        print(f"[{msg.topic}] RECEBIDO:")
        if msg.topic == MQTT_TOPIC_TEMP:
            # Note: A conversão para float é boa prática para garantir que é um número
            temp_value = float(payload_str)
            print(f"Temperatura: {temp_value:.2f} °C")
        elif msg.topic == MQTT_TOPIC_HUM:
            hum_value = float(payload_str)
            print(f"Umidade: {hum_value:.2f} %")
        elif msg.topic == MQTT_TOPIC_IP:
            ip_value = str(payload_str)
            print(f"IP do ESP: {ip_value}")
        else:
            print(f"Mensagem: {payload_str}")
        print("-" * 30)
            
    except ValueError:
        print(f"Erro: Payload inválido (não é um número) no tópico {msg.topic}: {payload_str}")
    except Exception as e:
        print(f"Erro ao processar mensagem: {e}")

def on_connect(client, userdata, flags, rc):
    """
    Verifica o status da conexão e se inscreve nos tópicos se a conexão for bem-sucedida.
    """
    if rc == 0:
        os.system("clear")
        print(f"Conectado ao broker MQTT com sucesso: {MQTT_BROKER}")
        
        # Inscrição nos tópicos
        # Lista de tuplas (tópico, QoS)
        topics_to_subscribe = [
            (MQTT_TOPIC_TEMP, 0),
            (MQTT_TOPIC_HUM, 0),
            (MQTT_TOPIC_IP, 0)
        ]
        client.subscribe(topics_to_subscribe)
        
        print(f"Subscrito nos tópicos: {MQTT_TOPIC_TEMP} e {MQTT_TOPIC_HUM}")
        
    else:
        print(f"Falha na conexão, código de retorno: {rc}")

# -----------------------------------------------------------
# Início do programa
# -----------------------------------------------------------

client = mqtt.Client(client_id=CLIENT_ID)
if 'MQTT_USER' in locals() and 'MQTT_PASSWORD' in locals():
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
client.on_connect = on_connect
client.on_message = on_message

try:
    print(f"Tentando conectar a {MQTT_BROKER}:{MQTT_PORT}...")
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    
    # Bloqueia e processa o tráfego MQTT (loop infinito)
    client.loop_forever()

except KeyboardInterrupt:
    print("\nPrograma encerrado pelo usuário (KeyboardInterrupt).")
    client.disconnect()
except Exception as e:
    print(f"Ocorreu um erro: {e}")
finally:
    # Apenas garante que a conexão será fechada se loop_forever() for interrompido
    if client.is_connected():
        client.disconnect()