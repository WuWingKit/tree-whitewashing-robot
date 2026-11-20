"""Exercise the real receiver over local HTTP, without a board/network change."""
import csv
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('receiver', root / 'pc/receive.py')
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)
posts = []
state = dict(stm32_online=True, tof=[dict(id=0, valid=True, mm=42, status=0, seq=1, age_ms=2),
                                   dict(id=1, valid=False, mm=None, status=256, seq=1, age_ms=4)])


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        if self.path.startswith('/camera.jpg'):
            self.send_header('X-Frame-Seq', '1')
            body = b'\xff\xd8fake-test-frame\xff\xd9'
        else:
            body = json.dumps(state).encode()
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        posts.append(self.path)
        self.send_response(202)
        self.end_headers()
        self.wfile.write(b'queued')

    def log_message(self, *args):
        pass


server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory() as folder:
        args = SimpleNamespace(host=f'http://127.0.0.1:{server.server_port}', camera=0, format=0,
                               image_port=server.server_port, output=Path(folder), interval=0.1)
        with patch.object(receiver.time, 'sleep', side_effect=[None, KeyboardInterrupt]):
            receiver.run(args)
        rows = list(csv.DictReader((Path(folder) / 'measurements.csv').open(encoding='utf-8-sig')))
        assert len(rows) == 4 and rows[0]['mm'] == '42' and rows[1]['mm'] == ''
        assert len(list(Path(folder).glob('*.jpg'))) == 1, 'same frame sequence must not be saved twice'
        assert posts == ['/api/camera/select?id=0&format=0', '/api/camera/stop']
        assert json.loads((Path(folder) / 'status.json').read_text())['stm32_online']
    print('PASS: HTTP telemetry, valid/invalid CSV, JPEG, dedup, start/stop')
finally:
    server.shutdown()
    server.server_close()
