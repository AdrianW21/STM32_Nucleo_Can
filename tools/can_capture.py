#!/usr/bin/env python3
"""Capture les trames CAN envoyées par la Nucleo (liaison série) dans un fichier CSV.

Utilisation :
    pip install pyserial
    python tools/can_capture.py --port COM4 --baud 921600 --out captures/premier_test.csv

Le bouton utilisateur de la Nucleo arrête/reprend la capture ; chaque reprise
crée un nouveau "segment" dans la colonne segment du CSV.
Fermer le Serial Monitor de CLion (ou PuTTY) avant de lancer ce script :
un seul programme peut ouvrir le port à la fois.
"""
import argparse
import csv
import re
import sys

import serial  # pip install pyserial

SEG_RE = re.compile(r"seg=(\d+)")


def main() -> int:
    ap = argparse.ArgumentParser(description="Capture CAN (Nucleo) vers CSV")
    ap.add_argument("--port", default="COM4", help="port série (ex. COM4)")
    ap.add_argument("--baud", type=int, default=921600, help="débit de la liaison série")
    ap.add_argument("--out", default="capture.csv", help="fichier CSV de sortie")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as exc:
        print(f"Impossible d'ouvrir {args.port} : {exc}")
        return 1

    segment = 0
    written = 0
    bad = 0

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["segment", "t_s", "id", "ext", "dlc"] + [f"d{i}" for i in range(8)])
        print(f"Capture sur {args.port} @ {args.baud} -> {args.out} (Ctrl+C pour quitter)")

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("ascii", errors="replace").strip()
                if not line:
                    continue

                if line.startswith("F,"):
                    parts = line.split(",")
                    if len(parts) != 6:
                        bad += 1
                        continue
                    _, t_s, cid, ext, dlc, data = parts
                    try:
                        n = int(dlc)
                    except ValueError:
                        bad += 1
                        continue
                    if len(data) != 2 * n or n > 8:
                        bad += 1          # ligne tronquée ou corrompue
                        continue
                    data_bytes = [data[i:i + 2] for i in range(0, len(data), 2)]
                    data_bytes += [""] * (8 - len(data_bytes))
                    writer.writerow([segment, t_s, "0x" + cid, ext, n] + data_bytes)
                    written += 1
                else:
                    print(line)
                    m = SEG_RE.search(line)
                    if m:
                        segment = int(m.group(1))
                    if line.startswith("# STOP"):
                        f.flush()
                        print(f"-> arret : {written} trames ecrites dans {args.out} "
                              f"({bad} lignes ignorees)")
        except KeyboardInterrupt:
            pass

    print(f"Termine : {written} trames, {bad} lignes ignorees.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
