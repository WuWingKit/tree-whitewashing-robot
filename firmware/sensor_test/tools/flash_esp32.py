"""Explicit USB install for sensor test only; never called by build scripts.
python flash_esp32.py --port COM10 --esptool E:/path/to/esptool.exe
The new factory partition replaces the old OTA layout. See README first.
"""
import argparse
from pathlib import Path
import subprocess
import sys


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True, help='ESP32 COM/CH343 bridge port, not STM32 port')
    p.add_argument('--esptool', type=Path, help='Existing esptool.exe; if omitted use python -m esptool')
    p.add_argument('--baud', type=int, default=460800)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1] / 'build/esp32'
    files = ['bootloader.bin', 'partitions.bin', 'app.bin']
    for file in files:
        if not (root / file).is_file():
            p.error('Missing built firmware: ' + str(root / file))
    cmd = [str(args.esptool)] if args.esptool else [sys.executable, '-m', 'esptool']
    cmd += ['--chip', 'esp32s3', '--port', args.port, '--baud', str(args.baud),
            'write_flash', '-z', '0x0', str(root / files[0]), '0x8000', str(root / files[1]),
            '0x10000', str(root / files[2])]
    subprocess.run(cmd, check=True)
