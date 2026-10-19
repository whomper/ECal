#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's disk access, counted from Hatari's GEMDOS trace (the
guidelines, 6.5): every Fopen, Fcreate, Fclose, Fsfirst/Fsnext,
Fdelete/Frename, Fseek, Fread and Fwrite on ECal's own files (ECAL.INF
in the root and everything in ECal's folder), job by job:

    tests/hatari/acc_files.py WORK [--machine ST-1:mono|Falcon|...] [--run quiet|sync|falcon]
                              [--bin DIR] [--roots]

Two CalDAV calendars of about 500 events between them (Radicale) and a
subscription (a feed this computer serves), all copied to the Atari
first by cal-cli, as an earlier refresh left them.

  quiet   1 boot until the accessory is idle, its window never opened;
          2 five minutes of idle, nothing changing; 3 the window opened
          (with SYNC_OPEN=0: only its look at the files)
  sync    4 refreshes with the window closed (SYNC_CLOSED=1, REFRESH=2):
          one with nothing new on the server, then one with 10 events
          changed there and the feed changed (its .NEW)
  falcon  5 Falcon mode (no gateway): the first TLS connection's files;
          --roots without ROOTS.DAT, so CACERT.PEM is read (tls_read_at)

Hatari writes the trace to a pipe this script reads as it comes, so each
call is put in the job going on at that moment. --bin takes the programs
from another folder (a release's, from common/tools/getrelease.py).
"""
import argparse
import base64
import datetime as dt
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
import rig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(ROOT, "build", "cal-cli")
AUTH = "Basic " + base64.b64encode(b"alice:wonderland").decode()
SCHEME = "http"
RAD = GW = FEED = 0

CALLS = {"3D": "Fopen", "3C": "Fcreate", "3E": "Fclose", "4E": "Fsfirst", "4F": "Fsnext", "41": "Fdelete",
         "56": "Frename", "42": "Fseek", "3F": "Fread", "40": "Fwrite"}


def http(method, path, body=None, ctype=None):
    import ssl as _ssl
    rq = urllib.request.Request("%s://127.0.0.1:%d%s" % (SCHEME, RAD, path), data=body, method=method)
    rq.add_header("Authorization", AUTH)
    if ctype:
        rq.add_header("Content-Type", ctype)
    ctx = _ssl._create_unverified_context() if SCHEME == "https" else None
    with urllib.request.urlopen(rq, timeout=20, context=ctx) as r:
        return r.read().decode("utf-8", "replace")


def split_ics(text):
    head, rest = text.split("BEGIN:VEVENT", 1)
    vtz = head[head.index("BEGIN:VTIMEZONE"):] if "BEGIN:VTIMEZONE" in head else ""
    by_uid = {}
    for ev in ("BEGIN:VEVENT" + rest).split("BEGIN:VEVENT")[1:]:
        ev = "BEGIN:VEVENT" + ev.split("END:VCALENDAR")[0]
        uid = re.search(r"UID:([^\r\n]+)", ev).group(1)
        by_uid.setdefault(uid, []).append(ev)
    return {uid: "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\n" + vtz + "".join(evs) + "END:VCALENDAR\r\n"
            for uid, evs in by_uid.items()}


class Trace:
    """reads Hatari's trace from the pipe; each call of ECal's files goes
    to the job named at the time (self.job)"""

    def __init__(self, path):
        self.path, self.job, self.jobs, self.last = path, "boot", {}, 0.0
        self.handles = {}           # GEMDOS handle -> file name, for ECal's files
        self.pending = None
        self.t0 = time.time()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def ours(self, name):
        n = name.upper().replace("/", "\\")
        # ECAL.INF and its safe save's ECAL.IN$ in the root; ECal's folder
        return re.search(r"(^|\\)ECAL\.IN[F$]$", n) is not None or "\\ECAL\\" in n

    def add(self, call, name, nbytes=0):
        j = self.jobs.setdefault(self.job, {"calls": {}, "bytes": {}, "files": {}, "first": None, "last": None})
        j["calls"][call] = j["calls"].get(call, 0) + 1
        if nbytes:
            j["bytes"][call] = j["bytes"].get(call, 0) + nbytes
        f = j["files"].setdefault(os.path.basename(name.replace("\\", "/")).upper() or name, {})
        f[call] = f.get(call, 0) + 1
        if call in ("Fopen", "Fcreate", "Fsfirst", "Fdelete", "Frename"):
            j.setdefault("log", []).append((time.time() - self.t0, call, name))
        if call == "Fread":
            f["bytes"] = f.get("bytes", 0) + nbytes
        now = time.time() - self.t0
        j["first"] = now if j["first"] is None else j["first"]
        j["last"] = now
        self.last = time.time()

    def run(self):
        with open(self.path, encoding="latin-1") as f:
            for line in f:
                m = re.match(r'GEMDOS 0x(3D|3C|4E|41|56)\s+\w+\("([^"]*)"(?:,\s*"([^"]*)")?', line)
                if m:
                    code, name = m.group(1), m.group(2)
                    self.pending = None
                    if self.ours(name):
                        self.add(CALLS[code], name)
                        if code in ("3D", "3C"):
                            self.pending = name
                    continue
                m = re.match(r"-> FD (\d+)", line)
                if m and self.pending:
                    self.handles[int(m.group(1))] = self.pending
                    self.pending = None
                    continue
                m = re.match(r"GEMDOS 0x(3E|42|3F|40|4F)\s+\w+\((\d+)?(?:,\s*(-?\d+))?", line)
                if m:
                    code = m.group(1)
                    if code == "4F":
                        continue            # Fsnext: counted with its Fsfirst, below
                    h = int(m.group(2)) if m.group(2) else -1
                    if code == "42":        # Fseek(offset, handle, mode)
                        mm = re.match(r"GEMDOS 0x42\s+Fseek\((-?\d+),\s*(\d+)", line)
                        h = int(mm.group(2)) if mm else -1
                    name = self.handles.get(h)
                    if name:
                        n = int(m.group(3)) if code in ("3F", "40") and m.group(3) else 0
                        self.add(CALLS[code], name, n)
                        if code == "3E":
                            self.handles.pop(h, None)

    def quiet_for(self, secs, limit, at_least=0.0):
        """wait until ECal's files have been left alone for secs (after at
        least at_least seconds), at most limit seconds"""
        t0 = time.time()
        while time.time() - t0 < limit:
            if time.time() - t0 >= at_least and time.time() - max(self.last, t0) >= secs:
                return True
            time.sleep(1)
        return False


def budget(j, piece):
    """6.5's budget for a job: an open per file it touches (one per piece
    of what it reads), one more when it writes it (itself, or through its
    safe save's other half, ECAL.IN$ or CAL00003.NEW); a look per file,
    plus the files' look (ECAL.INF, ECAL.KEY, the CACHE listing)"""
    opens = looks = 0
    halves = [n for n, f in j["files"].items()
              if n.endswith(("$", ".TMP", ".NEW")) and (f.get("Fcreate") or f.get("Fwrite"))]

    def half_of(name, half):
        if half.endswith("$"):
            return name.startswith(half[:-1]) and len(name) == len(half)
        return name.rsplit(".", 1)[0] == half.rsplit(".", 1)[0]

    for name, f in j["files"].items():
        if name.endswith(("$", ".TMP", ".NEW")) or name == "*.*":
            continue            # a safe save's or a download's other half: with its file
        reads = -(-f.get("bytes", 0) // piece) if f.get("Fread") else 0
        written = f.get("Fcreate") or f.get("Fwrite") or any(half_of(name, h) for h in halves)
        opens += max(reads, 1 if f.get("Fopen") else 0) + (1 if written else 0)
        looks += 1
    return opens, looks + 3


def report(tr, label, piece, timeline=False):
    print("\n== %s" % label)
    over = []
    for job, j in tr.jobs.items():
        c = j["calls"]
        span = (j["last"] - j["first"]) if j["first"] is not None else 0
        print("  [%s] %s; read %d B, written %d B; ECal's files busy over %.0f s" % (
            job, ", ".join("%s %d" % (k, c[k]) for k in ("Fopen", "Fcreate", "Fclose", "Fsfirst", "Fdelete",
                                                          "Frename", "Fseek", "Fread", "Fwrite") if k in c) or "nothing",
            j["bytes"].get("Fread", 0), j["bytes"].get("Fwrite", 0), span))
        for name, f in sorted(j["files"].items()):
            print("      %-14s %s" % (name, ", ".join("%s %d" % kv for kv in sorted(f.items()) if kv[0] != "bytes")))
        if timeline:
            for t, call, name in j.get("log", []):
                print("        %7.1f s  %-8s %s" % (t, call, name))
        bo, bl = budget(j, piece)
        o, l = c.get("Fopen", 0) + c.get("Fcreate", 0), c.get("Fsfirst", 0)
        print("      budget (6.5): opens %d of %d, looks %d of %d%s" % (o, bo, l, bl,
              "" if o <= bo and l <= bl else "   OVER"))
        if o > bo or l > bl:
            over.append(job)
    return over


def main():
    global RAD, GW, FEED, SCHEME
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    rig.add_machine(ap, "ST-1:mono")
    ap.add_argument("--run", default="quiet", choices=("quiet", "sync", "falcon"))
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"))
    ap.add_argument("--roots", action="store_true", help="falcon: without ROOTS.DAT (CACERT.PEM read)")
    ap.add_argument("--display", default=":98")
    ap.add_argument("--timeline", action="store_true", help="each job's opens, looks, deletes and renames in order")
    a = ap.parse_args()
    os.environ["TZ"] = "Asia/Jerusalem"
    time.tzset()
    base = 21000 + os.getpid() % 3000 * 3
    RAD, GW, FEED = base, base + 1, base + 2
    falcon = a.run == "falcon"
    if falcon and rig.is_st(a.machine):
        a.machine = "Falcon"
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECAL")
    os.makedirs(os.path.join(hd, "AUTO"))
    os.makedirs(folder)
    srv = os.path.join(work, "srv")
    os.makedirs(os.path.join(srv, "feed"))
    open(os.path.join(srv, "users"), "w").write("alice:wonderland\n")
    procs, r = [], None
    ssl = []
    if falcon:
        cert, key = os.path.join(srv, "cert.pem"), os.path.join(srv, "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=DNS:127.0.0.1",
                        "-keyout", key, "-out", cert], check=True, capture_output=True)
        ssl = ["--server-ssl", "--server-certificate", cert, "--server-key", key]
        SCHEME = "https"
    try:
        procs.append(subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % RAD] +
                                      ssl + ["--storage-filesystem-folder", os.path.join(srv, "store"),
                                             "--auth-type", "htpasswd", "--auth-htpasswd-filename",
                                             os.path.join(srv, "users"), "--auth-htpasswd-encryption", "plain"],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "ecalgate.py"), "--port",
                                       str(GW), "--bind", "127.0.0.1", "--allow-local", "-v"],
                                      stderr=open(os.path.join(work, "ecalgate.log"), "w")))
        procs.append(subprocess.Popen([sys.executable, "-m", "http.server", str(FEED), "--bind", "127.0.0.1",
                                       "--directory", os.path.join(srv, "feed")],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        for _ in range(50):
            try:
                http("GET", "/")
                break
            except Exception:
                time.sleep(0.2)
        ics = os.path.join(work, "ics")
        subprocess.run([sys.executable, os.path.join(ROOT, "tests", "host", "bigcal.py"), ics],
                       check=True, stdout=subprocess.DEVNULL)
        shutil.copy(os.path.join(ics, "SUBS.ICS"), os.path.join(srv, "feed", "holidays.ics"))
        events = {}
        for cal, files in (("work", ["WORK.ICS"]), ("home", ["HOME.ICS", "FAMILY.ICS"])):
            http("MKCALENDAR", "/alice/%s/" % cal, ('<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" '
                 'xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>%s'
                 '</D:displayname></D:prop></D:set></C:mkcalendar>' % cal.title()).encode(), "application/xml")
            res = {}
            for fn in files:
                res.update(split_ics(open(os.path.join(ics, fn), newline="").read()))
            for i, uid in enumerate(sorted(res)):
                http("PUT", "/alice/%s/e%03d.ics" % (cal, i), res[uid].encode(), "text/calendar")
            events[cal] = (sorted(res), res)
        print("the server: %d events in two calendars, and a feed of %d"
              % (sum(len(v[0]) for v in events.values()),
                 open(os.path.join(srv, "feed", "holidays.ics")).read().count("BEGIN:VEVENT")))

        # the Atari's copy, as an earlier refresh left it
        seed = os.path.join(work, "seed")
        env = dict(os.environ, CACERT=cert) if falcon else None
        subprocess.run([CLI, seed, "zone", "Asia/Jerusalem"], check=True, stdout=subprocess.DEVNULL)
        if falcon:
            subprocess.run([CLI, seed, "falcon", "1"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([CLI, seed, "caldav", "Server", "%s://127.0.0.1:%d/" % (SCHEME, RAD), "alice", "wonderland"],
                       check=True, stdout=subprocess.DEVNULL, env=env)
        subprocess.run([CLI, seed, "subscribe", "http://127.0.0.1:%d/holidays.ics" % FEED, "Holidays"],
                       check=True, stdout=subprocess.DEVNULL, env=env)
        shutil.copy(os.path.join(seed, "ECAL.KEY"), os.path.join(folder, "ECAL.KEY"))
        os.makedirs(os.path.join(folder, "CACHE"))
        for f in os.listdir(os.path.join(seed, "CACHE")):
            shutil.copy(os.path.join(seed, "CACHE", f), os.path.join(folder, "CACHE", f.upper()))
        with open(os.path.join(seed, "ECAL.INF"), newline="") as f:
            lines = [l for l in f.read().split("\r\n")
                     if not l.upper().startswith(("START=", "SYNC_OPEN=", "SYNC_CLOSED=", "LOG=", "GATE=",
                                                  "GATEPORT=", "FALCON=", "DSP=", "REFRESH="))]
        i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
        lines[i] = "FOLDER=C:\\ECAL"
        sync = a.run in ("sync", "falcon")
        conn = ["FALCON=1", "DSP=1"] if falcon else ["GATE=127.0.0.1", "GATEPORT=%d" % GW]
        lines[i + 1:i + 1] = ["START=20", "SYNC_OPEN=0", "SYNC_CLOSED=%d" % sync, "REFRESH=%d" % (2 if sync else 0),
                              "LOG=0"] + conn
        with open(os.path.join(hd, "ECAL.INF"), "w", newline="") as f:
            f.write("\r\n".join(lines))
        if falcon:
            with open(os.path.join(folder, "CACERT.PEM"), "wb") as f:
                f.write(open(os.path.join(a.bin, "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
            if not a.roots:
                subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(folder, "CACERT.PEM"),
                                os.path.join(folder, "ROOTS.DAT")], check=True, capture_output=True)
        acc = "ECALST.ACC" if rig.is_st(a.machine) else "ECAL.ACC"
        shutil.copy(os.path.join(a.bin, acc), os.path.join(hd, "ECAL.ACC"))

        pipe = os.path.join(work, "trace.pipe")
        os.mkfifo(pipe)
        tr = Trace(pipe)
        r = rig.Rig(work, a.machine, network=True, auto=None, display=a.display,
                    extra=("--trace", "gemdos,aes", "--trace-file", pipe))
        label = "%s, %s, %s" % (a.machine, os.path.basename(a.bin.rstrip("/")), a.run + (" without ROOTS.DAT" if a.roots else ""))
        if a.run == "quiet":
            tr.job = "1 boot until idle"
            tr.quiet_for(40, 900, at_least=45)
            tr.job = "2 five minutes idle"
            time.sleep(300)
            r.key(0x01)                     # Esc: a reminder's alert out of the way
            time.sleep(2)
            tr.job = "3 the window opened"
            r.click(18, 8)
            time.sleep(1.5)
            r.click(60, 8 + 3 * 16 + 8)
            time.sleep(5)
            tr.quiet_for(20, 300)
            r.shot("opened")
        elif a.run == "sync":
            tr.job = "boot, reading and the first refresh"
            tr.quiet_for(45, 1200, at_least=60)
            tr.job = "4a a refresh, nothing new"
            time.sleep(150)                 # REFRESH=2: one more meanwhile
            tr.job = "between"
            work_i, (uids, res) = 0, events["work"]
            kept = []
            for f in os.listdir(os.path.join(folder, "CACHE")):
                if f.endswith(".DAT"):
                    kept += [int(k) for k in re.findall(rb"^R [^\t]*\t/alice/work/e(\d+)\.ics\t",
                                                        open(os.path.join(folder, "CACHE", f), "rb").read(), re.M)]
            for k in sorted(kept)[:10]:
                http("PUT", "/alice/work/e%03d.ics" % k,
                     res[uids[k]].replace("SUMMARY:", "SUMMARY:Changed ", 1).encode(), "text/calendar")
            feed = os.path.join(srv, "feed", "holidays.ics")
            text = open(feed, newline="").read().replace("SUMMARY:Holiday 1\r\n", "SUMMARY:Holiday one\r\n", 1)
            open(feed, "w", newline="").write(text)
            tr.job = "4b a refresh, 10 changed and the feed"
            time.sleep(150)
        else:
            tr.job = "5 boot, reading and the first refresh (TLS)"
            tr.quiet_for(60, 1500, at_least=90)
        time.sleep(3)
    finally:
        if r:
            r.stop()
        for p in procs:
            p.terminate()
    time.sleep(2)
    over = report(tr, label, 16384 if rig.is_st(a.machine) else 32768, a.timeline)
    log = open(os.path.join(work, "ecalgate.log"), errors="replace").read()
    print("  (the gateway: %d requests)" % len(re.findall(r" -> \d+", log)))
    print("acc_files:", "within budget" if not over else "over budget: " + "; ".join(over))
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
