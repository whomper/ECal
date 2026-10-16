#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A proxy in front of Radicale that answers the way iCloud does: HTTP/1.1
with connections kept open, and answers compressed with gzip when the
client asks for it; and like iCloud it keeps one event per UID in an
account: a new event whose UID is already in another of the account's
calendars is refused, 412. Radicale itself speaks HTTP/1.0, never
compresses and allows the same UID in two calendars, so without this the
tests would never see any of these.

    gzproxy.py LISTEN_PORT UPSTREAM_PORT [--tls CERT KEY]
"""
import gzip
import http.client
import re
import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UP = None
HOP = {"connection", "keep-alive", "transfer-encoding", "content-length", "content-encoding"}


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("gzproxy: %s %s\n" % (self.command, self.path))

    def up(self, method, path, body=None, hdrs=None):
        up = http.client.HTTPConnection("127.0.0.1", UP, timeout=60)
        up.request(method, path, body=body, headers=hdrs or {})
        r = up.getresponse()
        data = r.read()
        up.close()
        return r, data

    def uid_elsewhere(self, body, hdrs):
        """iCloud's rule: is this new event's UID in another calendar of the account?"""
        m = re.search(rb"^UID:(.*?)\r?$", body or b"", re.M)
        parts = self.path.split("/")
        if not m or len(parts) < 4:
            return False
        uid, coll = m.group(1).strip(), "/".join(parts[:-1]) + "/"
        auth = {k: v for k, v in hdrs.items() if k.lower() == "authorization"}
        _, listing = self.up("PROPFIND", "/".join(parts[:-2]) + "/", None, dict(auth, Depth="1"))
        for href in re.findall(rb"<[^>]*href>([^<]+)</", listing):
            h = href.decode()
            if h.endswith("/") and h != coll and h.count("/") == coll.count("/"):
                _, ics = self.up("GET", h, None, auth)
                if re.search(rb"^UID:" + re.escape(uid) + rb"\r?$", ics, re.M):
                    return True
        return False

    def relay(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else None
        hdrs = {k: v for k, v in self.headers.items() if k.lower() not in HOP and k.lower() != "accept-encoding"}
        if self.command == "PUT" and self.headers.get("If-None-Match") == "*" and self.uid_elsewhere(body, hdrs):
            msg = b"UID already in use in another calendar of this account\n"
            self.send_response(412, "Precondition Failed")
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)
            return
        r, data = self.up(self.command, self.path, body, hdrs)
        packed = "gzip" in (self.headers.get("Accept-Encoding") or "") and len(data) > 64
        if packed:
            data = gzip.compress(data, 6)
        self.send_response(r.status, r.reason)
        for k, v in r.getheaders():
            if k.lower() not in HOP:
                self.send_header(k, v)
        if packed:
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    do_GET = do_PUT = do_DELETE = do_POST = do_HEAD = do_OPTIONS = relay
    do_PROPFIND = do_REPORT = do_MKCALENDAR = do_PROPPATCH = do_MOVE = do_COPY = relay


def main():
    global UP
    port, UP = int(sys.argv[1]), int(sys.argv[2])
    srv = ThreadingHTTPServer(("127.0.0.1", port), Proxy)
    if "--tls" in sys.argv:
        i = sys.argv.index("--tls")
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(sys.argv[i + 1], sys.argv[i + 2])
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
