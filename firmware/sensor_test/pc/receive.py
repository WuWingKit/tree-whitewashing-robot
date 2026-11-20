"""Python 3 standard-library sensor receiver; no OpenCV needed.

Connect PC Wi-Fi to TreeRobotSensors / TreeTest2026 first.
python receive.py --camera 0 --format 0 --output captures
Browser http://192.168.4.1 displays live images and sensor health.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from urllib.error import URLError, HTTPError
from urllib.request import urlopen, Request
from urllib.parse import urlsplit, urlunsplit


def get_json(url):
    with urlopen(url, timeout=3) as response:
        return json.load(response)


def run(args):
    base = args.host.rstrip('/')
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    state = get_json(base + '/api/status')
    print(json.dumps(state, ensure_ascii=False, indent=2))
    if args.camera is not None:
        req = Request(base + f'/api/camera/select?id={args.camera}&format={args.format}', method='POST', data=b'')
        with urlopen(req, timeout=3) as response:
            print(response.read().decode())
    parsed = urlsplit(base)
    image_base = urlunsplit((parsed.scheme, parsed.hostname + ':' + str(args.image_port), '', '', ''))
    last_seq = None
    with (out / 'measurements.csv').open('a', newline='', encoding='utf-8-sig') as log:
        writer = csv.writer(log)
        if log.tell() == 0:
            writer.writerow(['time_utc', 'id', 'valid', 'mm', 'status', 'seq', 'age_ms'])
        try:
            while True:
                stamp = datetime.now(timezone.utc).isoformat()
                try:
                    state = get_json(base + '/api/status')
                    (out / 'status.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
                    for sensor in state['tof']:
                        writer.writerow([stamp, sensor['id'], sensor['valid'], sensor['mm'] if sensor['valid'] else '',
                                         sensor['status'], sensor['seq'], sensor['age_ms']])
                    log.flush()
                    print('STM32:', state.get('stm32_online', state.get('online', False)), ' | ',
                          ' '.join(f"ToF{s['id']}={s['mm'] if s['valid'] else 'invalid'}" for s in state['tof']))
                    if args.camera is not None:
                        with urlopen(image_base + f'/camera.jpg?id={args.camera}', timeout=3) as response:
                            seq = response.headers.get('X-Frame-Seq')
                            data = response.read(256*1024+1)
                        if len(data) > 256*1024 or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                            raise ValueError('Invalid/oversize JPEG')
                        if seq != last_seq:
                            path = out / f'camera{args.camera}_{time.time_ns()}.jpg'
                            path.write_bytes(data)
                            last_seq = seq
                except (URLError, HTTPError, TimeoutError, OSError, ValueError, KeyError) as error:
                    print('Waiting for fresh data:', error)
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass
        finally:
            if args.camera is not None:
                try:
                    with urlopen(Request(base + '/api/camera/stop', method='POST', data=b''), timeout=3):
                        pass
                except OSError:
                    pass


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='http://192.168.4.1')
    p.add_argument('--camera', type=int, choices=[0, 1])
    p.add_argument('--image-port', type=int, default=81)
    p.add_argument('--format', type=int, default=0, help='Index shown in browser /api/status; prefer 640x480 or lower first')
    p.add_argument('--output', type=Path, default=Path('captures'))
    p.add_argument('--interval', type=float, default=0.5)
    args = p.parse_args()
    if args.interval < 0.1:
        p.error('interval must be >=0.1 seconds')
    run(args)
