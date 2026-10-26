#!/usr/bin/env python3
"""tools/serve-https.py — the emulator over HTTPS, so a phone on the LAN can run it.

Why this exists: the wasm build renders inside an AudioWorklet, and
AudioContext.audioWorklet only EXISTS in a secure context. http:// on a LAN
address is not one, so an iPhone opening http://192.168.x.x:8932 gets

    Could not start: undefined is not an object (evaluating 'ac.audioWorklet.addModule')

and nothing else. https:// (even with a self-signed certificate) is a secure
context, so the worklet appears. The certificate is self-signed, so the phone
shows a warning once: Safari > "Show Details" > "visit this website".

    python3 tools/serve-https.py [port]        # default 8933

Serves build/emu. Generate the certificate first:

    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \\
      -keyout .certs/key.pem -out .certs/cert.pem \\
      -subj "/CN=<lan-ip>" -addext "subjectAltName=IP:<lan-ip>,IP:127.0.0.1,DNS:localhost"
"""
import http.server
import os
import ssl
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE / "build" / "emu"
CERT = HERE / ".certs" / "cert.pem"
KEY = HERE / ".certs" / "key.pem"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8933


class Handler(http.server.SimpleHTTPRequestHandler):
    # python's table has no .wasm entry, and a wrong MIME makes the fetch a
    # network error rather than a compile error
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
    }

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(ROOT), **kw)

    def end_headers(self):
        # the page and the worklet are same-origin; these just make the intent
        # explicit and keep the door open if the build ever wants SharedArrayBuffer
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, format, *args):        # noqa: A002 - the base class's name
        sys.stderr.write("%s %s\n" % (self.address_string(), format % args))


def main():
    if not CERT.exists() or not KEY.exists():
        raise SystemExit(f"no certificate: {CERT} / {KEY}")
    if not (ROOT / "index.html").exists():
        raise SystemExit(f"nothing to serve: {ROOT}/index.html is missing (build with --web)")
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=str(CERT), keyfile=str(KEY))
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"https://<this machine>:{PORT}/   serving {ROOT}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
