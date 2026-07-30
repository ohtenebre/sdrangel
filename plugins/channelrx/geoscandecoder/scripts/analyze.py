import sys
from datetime import datetime


def bitrev8(x):
    x = ((x & 0x55) << 1) | ((x & 0xAA) >> 1)
    x = ((x & 0x33) << 2) | ((x & 0xCC) >> 2)
    return ((x << 4) | (x >> 4)) & 0xFF


def parse_geoscan(hex_str):
    data = bytes.fromhex(hex_str)
    print("=" * 40)
    print(" GEOSCAN TELEMETRY DECODER (Python) ")
    print("=" * 40)
    print(f"RAW PACKET: {data.hex().upper()}")
    print("-" * 40)

    # Ищем начало кадра (0x5B или 0x21 в зависимости от смещения)
    # В нашем случае мы берем уже декодированный Viterbi поток
    for i in range(len(data) - 46):
        if data[i] == 0x5B and data[i + 3] == 0x21:
            k = i
            # Парсим время (Little Endian + BitRev)
            raw_time = (
                (bitrev8(data[k + 19]) << 24)
                | (bitrev8(data[k + 18]) << 16)
                | (bitrev8(data[k + 17]) << 8)
                | bitrev8(data[k + 16])
            )
            dt = datetime.fromtimestamp(raw_time)

            # Парсим телеметрию
            volt = ((bitrev8(data[k + 37]) << 8) | bitrev8(data[k + 36])) * 10 / 1000.0
            solar = (bitrev8(data[k + 39]) << 8) | bitrev8(data[k + 38])
            temp = (
                int.from_bytes(
                    [bitrev8(data[k + 44]), bitrev8(data[k + 45])],
                    byteorder="little",
                    signed=True,
                )
                / 1.0
            )
            reboots = bitrev8(data[k + 46])

            print(f"[*] Satellite Time:  {dt.strftime('%Y-%m-%d %H:%M:%S')} UTC")
            print(f"[*] Battery Voltage: {volt:.2f} V")
            print(f"[*] Solar Current:   {solar} mA")
            print(f"[*] Chassis Temp:    {temp:+.1f} C")
            print(f"[*] System Reboots:  {reboots}")
            print("-" * 40)
            print("[+] STATUS: DECODE SUCCESSFUL")
            return

    print("[!] Error: Geoscan header not found in this data.")


if __name__ == "__main__":
    # Тестовый пакет из твоих логов (уже после Витерби)
    test_hex = "5B 1F AB 21 41 97 39 9B 20 A0 97 54 B4 DB E2 77 31 C2 4C 92 B8 6A 11 B7 01 9F BF AB 21 79 4B D2 7C FE AD 4B FC C0 7A FC 1A 45 E3 6B 71 6F 3F FC 7B 8F 5E A9 5C 94 BD 83 D2 2A 63 7C 56 8F C8 B6 6E FD 33 FC 20 7B 22 18 0A 9B 14 AF 48 A4"
    parse_geoscan(test_hex)
