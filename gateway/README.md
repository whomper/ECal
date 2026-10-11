# The Raspberry Pi gateway

Calendar servers (iCloud, Fastmail, Google's published calendars...) only
accept encrypted connections, and an 8 MHz ST can't do modern TLS. So
ECal sends its requests to a Raspberry Pi on your home network in plain
HTTP, and the Pi makes the HTTPS connections. The Pi runs `ecalgate.py`, a
small program in standard Python 3, set up as a service by `install.sh`.

```
Atari (ECALST.ACC) --plain HTTP, home LAN--> Pi :8180 (ecalgate.py) --HTTPS--> caldav.icloud.com
                                                                  --HTTPS--> p42-caldav.icloud.com
                                                                  --HTTPS--> any published .ics
```

Unlike EMail's gateway (stunnel to a fixed mail server), ecalgate can reach
any server: calendars live on many hosts (iCloud moves you from
caldav.icloud.com to a server of its own, and every subscription is
somewhere else). The Atari sends each request in proxy form,
`GET https://host/path HTTP/1.1`, and ecalgate:

- checks the server's certificate against the Pi's root certificates;
- keeps one connection per server open, so a sync doesn't pay for a TLS
  handshake per request;
- asks for compressed answers and unpacks them;
- turns chunked answers into one block with a length;
- trims calendar subscriptions: when the Atari says which months it keeps
  (`X-Calendar-Window: 20260701-20271101`), ecalgate passes on only the
  events in that window, every repeating event that still repeats, and the
  time zones, and leaves out HTML descriptions and attached files. A public
  holiday or sports feed can be several megabytes; trimmed, it fits an ST.

Any computer with Debian, Ubuntu or Raspberry Pi OS works, and the same Pi
can run EMail's gateway and Claude ST's bridge too.

## Setting it up

On the Pi:

```
git clone https://github.com/whomper/ECal
cd ECal/gateway
sudo ./install.sh
```

It installs ecalgate as `/usr/local/bin/ecalgate.py`, runs it as the
`ecalgate` service (now and at every boot) and prints the address to type
into ECal. Check it from any browser on your network:
`http://PI-ADDRESS:8180/` answers "ecalgate is running".

Options:

```
sudo ./install.sh --allow 192.168.1.50    only the Atari at that address may use it
sudo ./install.sh --port 8181             another port
sudo ./install.sh --allow-local           also reach servers on your home network
                                          (a Radicale or Nextcloud at home)
sudo ./install.sh --remove                take it away
```

Run it again to change the options. `journalctl -u ecalgate` shows its
messages; add `-v` to the `ExecStart` line in
`/etc/systemd/system/ecalgate.service` to log every request.

## In ECal

≡ menu > **Connection**: Gateway address = the Pi's IP address (plain TOS
with STinG can't always look up names on the local network), port 8180.
Then add your accounts as usual, with their real `https://` addresses:
ecalgate does the rest.

## Keeping it private

Between the Atari and the Pi nothing is encrypted, as on every network in
the 1990s: keep the gateway on your home network and never forward its
port on your router. ecalgate answers only addresses on the private
networks (192.168.x.x, 10.x.x.x, 172.16-31.x.x), or with `--allow` only
your Atari; it refuses to connect onward to your own network unless
`--allow-local` is given, so it can't be used to reach other machines at
home. If the Pi's firewall (ufw) is on, install.sh opens the port for the
home network (or just the `--allow` address).

A Pi on a network that only goes out through a proxy: ecalgate follows
`HTTPS_PROXY`, set it in the service file with an `Environment=` line.

## Tested

`make test` runs ecalgate's own tests (trimming, compression, chunked
answers, redirects, the local-network rule), `GATEWAY=1
tests/integration.sh` syncs, adds, changes and deletes events on a CalDAV
server through it, and `tests/hatari/net_test.py` does the same from an
emulated ST.
