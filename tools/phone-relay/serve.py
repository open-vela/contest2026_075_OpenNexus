"""Serve only the three static gateway assets on loopback, with a bounded TTL.

This is not an API proxy. It has no credentials, microphone endpoints, file
browser or upload handler. Publishing an HTTPS tunnel is a separate step.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parent
ASSETS={
    '/':('index.html','text/html; charset=utf-8'),
    '/index.html':('index.html','text/html; charset=utf-8'),
    '/gateway.js':('gateway.js','text/javascript; charset=utf-8'),
    '/protocol.js':('protocol.js','text/javascript; charset=utf-8'),
}
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        item=ASSETS.get(urlsplit(self.path).path)
        if not item:self.send_error(404);return
        data=(ROOT/item[0]).read_bytes()
        self.send_response(200)
        self.send_header('Content-Type',item[1]);self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store');self.send_header('Referrer-Policy','no-referrer')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Permissions-Policy','bluetooth=(self), microphone=(), camera=()')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src https://httpbin.org; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
        self.end_headers();self.wfile.write(data)
    def log_message(self,fmt,*args):
        print(fmt%args,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',type=int,default=8852)
    p.add_argument('--seconds',type=int,default=900)
    args=p.parse_args()
    if not 30<=args.seconds<=3600:p.error('seconds must be between 30 and 3600')
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    timer=threading.Timer(args.seconds,server.shutdown);timer.daemon=True;timer.start()
    print(f'Gateway: http://127.0.0.1:{args.port}; expires after {args.seconds} seconds',flush=True)
    try:server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:pass
    finally:timer.cancel();server.server_close()
