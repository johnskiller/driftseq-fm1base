#!/usr/bin/env python3
"""no-cache static file server for the driftseq web emu (build/emu)."""
import http.server, socketserver, sys, os

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'build', 'emu')
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8933

class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=os.path.abspath(ROOT), **kw)
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()
    def log_message(self, format, *args):   # never log: a full stdout pipe hangs the server
        pass

socketserver.TCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(('0.0.0.0', PORT), H) as s:
    s.serve_forever()
