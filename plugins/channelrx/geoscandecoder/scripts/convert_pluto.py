#!/usr/bin/env python3
"""
Конвертер записей Adalm Pluto (CS16 бинарный IQ) → WAV для SDRangel.

Формат CS16: interleaved signed int16, I Q I Q I Q ...
Тот же формат что в test_IQ_GEOSCAN_SAT_03NOISE_NODRIFT_2MSPS_CS16.wav

Использование:
    python3 convert_pluto.py rtu.bin rtu.wav --samplerate 2000000
"""

import argparse
import numpy as np
import wave
import struct
import sys
import os

def convert(input_path, output_path, sample_rate):
    file_size = os.path.getsize(input_path)
    n_samples = file_size // 4  # 4 байта на сэмпл (int16 I + int16 Q)
    duration_sec = n_samples / sample_rate

    print(f"Входной файл:   {input_path}")
    print(f"Размер:         {file_size / 1e6:.1f} МБ")
    print(f"Сэмплов:        {n_samples:,}")
    print(f"Частота:        {sample_rate / 1e6:.2f} МГц")
    print(f"Длительность:   {duration_sec:.1f} сек ({duration_sec/60:.1f} мин)")
    print(f"Выходной файл:  {output_path}")
    print()

    # Читаем чанками чтобы не загружать всё в RAM сразу
    chunk_size = 1_000_000  # сэмплов за раз (~4 МБ)

    with open(input_path, 'rb') as f_in, \
         wave.open(output_path, 'w') as wav:

        # WAV: стерео (L=I, R=Q), int16, нужная частота дискретизации
        wav.setnchannels(2)
        wav.setsampwidth(2)       # 2 байта = int16
        wav.setframerate(sample_rate)

        processed = 0
        while True:
            raw = f_in.read(chunk_size * 4)
            if not raw:
                break

            # Читаем как int16 (interleaved I, Q, I, Q, ...)
            samples = np.frombuffer(raw, dtype=np.int16)

            # Пишем как есть — уже в формате стерео int16
            wav.writeframes(samples.tobytes())

            processed += len(samples) // 2
            pct = processed / n_samples * 100
            print(f"\r  Прогресс: {pct:.1f}% ({processed:,} сэмплов)", end='', flush=True)

    print(f"\n\nГотово! → {output_path}")
    out_size = os.path.getsize(output_path)
    print(f"Размер WAV: {out_size / 1e6:.1f} МБ")

def main():
    parser = argparse.ArgumentParser(description='Конвертер Adalm Pluto CS16 → WAV')
    parser.add_argument('input',  help='Входной .bin файл (CS16 IQ от Pluto)')
    parser.add_argument('output', help='Выходной .wav файл')
    parser.add_argument('--samplerate', type=int, default=2_000_000,
                        help='Частота дискретизации (по умолчанию 2000000)')
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Ошибка: файл {args.input} не найден")
        sys.exit(1)

    convert(args.input, args.output, args.samplerate)

if __name__ == '__main__':
    main()
