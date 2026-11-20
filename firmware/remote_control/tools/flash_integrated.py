"""USB-install integrated firmware, explicitly selecting ota_0 on next boot.
Only run at the user's flashing step; builds never call this script.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import json

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--profile',choices=['legacy','pcb_v1','pcb_v2'],required=True)
    p.add_argument('--esptool', type=Path)
    p.add_argument('--baud', type=int, default=460800)
    args = p.parse_args()
    out = Path(__file__).resolve().parents[1] / 'esp32/build/integrated'
    if args.profile!='legacy':out=out/args.profile
    profile_file=out/'wiring_profile.json'
    if not profile_file.is_file() or json.loads(profile_file.read_text(encoding='utf-8')).get('profile')!=args.profile:
        p.error('Missing or mismatched wiring profile manifest: '+str(profile_file))
    for file in ['bootloader.bin', 'partitions.bin', 'app.bin']:
        if not (out/file).is_file():
            p.error('Missing firmware: '+str(out/file))
    blank = out/'reset_otadata.bin'
    blank.write_bytes(b'\xff'*8192)
    cmd = [str(args.esptool)] if args.esptool else [sys.executable, '-m', 'esptool']
    cmd += ['--chip', 'esp32s3', '--port', args.port, '--baud', str(args.baud), 'write_flash', '-z',
            '0x0', str(out/'bootloader.bin'), '0x8000', str(out/'partitions.bin'),
            '0xe000', str(blank), '0x10000', str(out/'app.bin')]
    subprocess.run(cmd, check=True)
