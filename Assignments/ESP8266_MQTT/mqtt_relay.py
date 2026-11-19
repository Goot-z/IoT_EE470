import json
import os
import uuid

import paho.mqtt.client as mqtt
import requests

BROKER = "broker.hivemq.com"
BROKER_PORT = 1883
SENSOR_TOPIC = "testtopic/temp/outTopic/gootz"

INGEST_URL = (
    "https://mediumspringgreen-meerkat-888583.hostingersite.com/insert_mqtt.php"
)

API_KEY = os.environ.get("MQTT_API_KEY")
if not API_KEY:
    raise SystemExit("Set the MQTT_API_KEY environment variable before running.")

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        print(f"MQTT connection failed: {reason_code}")
        return

    print(f"Connected to {BROKER}")
    client.subscribe(SENSOR_TOPIC)
    print(f"Subscribed to {SENSOR_TOPIC}")

def on_message(client, userdata, message):
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        print(f"Ignoring invalid JSON: {error}")
        return

    print(f"MQTT received: {payload}")

    try:
        response = requests.post(
            INGEST_URL,
            json=payload,
            headers={"X-API-Key": API_KEY},
            timeout=10,
        )

        if response.ok:
            print(f"Database response: {response.text}")
        else:
            print(
                f"Database request failed: HTTP {response.status_code} "
                f"{response.text}"
            )

    except requests.RequestException as error:
        print(f"Could not reach the database endpoint: {error}")

client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id=f"ee470-relay-{uuid.uuid4().hex[:8]}",
)
client.on_connect = on_connect
client.on_message = on_message

print(f"Connecting to {BROKER}...")
client.connect(BROKER, BROKER_PORT, keepalive=60)

try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\nStopping relay.")
    client.disconnect()
