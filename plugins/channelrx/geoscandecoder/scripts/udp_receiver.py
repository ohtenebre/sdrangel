#!/usr/bin/env python3
import socket
import json
import sys

HOST = "127.0.0.1"
PORT = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))
print(f"Listening on {HOST}:{PORT}")

while True:
    data, addr = sock.recvfrom(4096)
    try:
        text = data.decode("utf-8")
        obj = json.loads(text)
        print(f"\n[{obj.get('dateTime', '?')}] {obj.get('sourceCallsign', '?')} → {obj.get('destinationCallsign', '?')}")
        print(f"  Battery: {obj.get('voltageBattSumMv')} mV  Solar: {obj.get('currentSolarMa')} mA  Temp: {obj.get('tempBatt1')}°C")
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(f"\n[Binary] {len(data)} bytes from {addr}")
        print(f"  {data.hex()}")
