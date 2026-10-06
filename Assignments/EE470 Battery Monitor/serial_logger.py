import serial
from datetime import datetime

PORT = "COM3"       # Change this to your ESP8266 COM port
BAUD = 115200

filename = "discharge_data.csv"

print(f"Opening {PORT} at {BAUD} baud...")
print(f"Saving measurements to {filename}")
print("Press Ctrl+C to stop.\n")

with serial.Serial(PORT, BAUD, timeout=1) as ser:
    with open(filename, "a", buffering=1) as file:

        while True:
            try:
                line = ser.readline().decode(
                    "utf-8",
                    errors="ignore"
                ).strip()

                if line:
                    print(line)
                    file.write(line + "\n")

            except KeyboardInterrupt:
                print("\nLogging stopped.")
                break