# mqtt_publisher.py

import paho.mqtt.client as mqtt
import time
import random

from config import *

# -----------------------------------------------------------

# Valores iniciais para simular um ambiente
BASE_TEMP = 25.0  # Temperatura base em °C
BASE_HUM = 60.0   # Umidade base em %

def simulate_reading(base_value, fluctuation_range=0.5):
    """
    Gera um valor simulado com uma pequena flutuação aleatória.
    """
    fluctuation = random.uniform(-fluctuation_range, fluctuation_range)
    return base_value + fluctuation

def on_connect(client, userdata, flags, rc):
    """
    Verifica o status da conexão.
    """
    if rc == 0:
        print(f"Conectado ao broker MQTT com sucesso: {MQTT_BROKER}")
    else:
        print(f"Falha na conexão, código de retorno: {rc}")
        
# -----------------------------------------------------------
# Início do programa
# -----------------------------------------------------------

client = mqtt.Client(client_id=CLIENT_ID_PUB)
if 'MQTT_USER' in locals() and 'MQTT_PASSWORD' in locals():
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
client.on_connect = on_connect

try:
    print(f"Tentando conectar a {MQTT_BROKER}:{MQTT_PORT}...")
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start() # Inicia o loop de rede em segundo plano

    print("Simulador DHT11 iniciado. Pressione Ctrl+C para parar.")
    
    while True:
        # 1. Simula as leituras
        current_temp = simulate_reading(BASE_TEMP, fluctuation_range=0.3)
        current_hum = simulate_reading(BASE_HUM, fluctuation_range=0.5)
        
        # 2. Converte para string (MQTT só publica strings/bytes)
        temp_payload = f"{current_temp:.2f}"
        hum_payload = f"{current_hum:.2f}"
        
        # 3. Publica a Temperatura
        # qos=0 (entrega "no máximo uma vez"), retain=False
        result_temp = client.publish(MQTT_TOPIC_TEMP, temp_payload, qos=0, retain=False)
        
        # 4. Publica a Umidade
        result_hum = client.publish(MQTT_TOPIC_HUM, hum_payload, qos=0, retain=False)
        
        print("-" * 45)
        print(f"[{time.strftime('%H:%M:%S')}] Publicado:")
        print(f"  {MQTT_TOPIC_TEMP}: {temp_payload} °C (Resultado: {result_temp.rc})")
        print(f"  {MQTT_TOPIC_HUM}: {hum_payload} % (Resultado: {result_hum.rc})")
        print("-" * 45)
        
        # 5. Espera antes de enviar o próximo dado
        time.sleep(5) # Publica a cada 5 segundos

except KeyboardInterrupt:
    print("\nPrograma encerrado pelo usuário (KeyboardInterrupt).")
except Exception as e:
    print(f"Ocorreu um erro: {e}")
finally:
    client.loop_stop() # Para o loop de rede em segundo plano
    client.disconnect()
    print("Conexão MQTT fechada.")