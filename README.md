# ECal

<img src="docs/icon.png" width="96" align="right" alt="ECal icon: a white desk calendar with a red band and two grey rings at the top and grey day boxes, and a blue alarm clock in front of its lower right corner">

A calendar for the Atari ST, STE, TT and Falcon: a GEM desk accessory that syncs with iCloud and any
CalDAV server.

**Highlights**

- **Like the Mac's Calendar:** day, week, month and year views, two-way sync, subscriptions, every repeat.
- **Modern but Atari:** accessory or program, right-click menus, drag and drop, reminders over any program.
- **Secure:** TLS 1.2 on the TT and Falcon (the DSP helps); ST and STE through the Gateway.
- **Hebrew** throughout: right to left, the Israeli keyboard (F10), Hebrew dates and repeats.
- **Invitations** by name from [EContact](https://github.com/whomper/EContact); works offline.

![ECal's month view on a Falcon in 256 colours (Hatari): October 2026 with the Home, Work and Family calendars in red, olive and yellow, today in red, and the calendar list and a small month on the left](docs/month.png)

## Getting started

Download `ECAL-x.y.zip` (or each file) from **[Releases](https://github.com/whomper/ECal/releases/latest)**:

| File | What it is |
|---|---|
| `ECAL.ACC` / `ECAL.PRG` | the accessory, or the same as a program, for every Atari |
| `TLS.OVL`, `CACERT.PEM`, `ROOTS.DAT` | the Direct connection (TT, Falcon), loaded only when used |
| `ECAL.HYP`, `ECAL.REF` | the guide, opened by the Help key |
| `icons/` | ECal's desktop icon ([how](docs/GUIDE.md#the-desktop-icon)) |

1. Copy `ECAL.ACC` to the root of the boot drive, the rest to a folder of its own (it asks where).
2. On an ST or STE, set up the Gateway on a Raspberry Pi ([steps](docs/GUIDE.md#settings)).
3. Reboot, choose **ECal** in the Desk menu; in **Settings...** give the Gateway or tick **Direct
   connection**, and the clock's time zone.
4. **Add account or calendar...** with an [app password](docs/GUIDE.md#app-passwords-icloud-fastmail-yahoo).
5. Press **Help** for the [guide](docs/GUIDE.md).

**Needs:** TOS 1.04 to 4.x, EmuTOS, MagiC or MiNT; ST medium or high, TT or Falcon resolution; 1 MB
(2 MB for big calendars); STinG or MiNTnet. A Direct connection needs a 68020 or better. On a 1 MB ST,
ECal's accessory or EContact's, not both. Under TOS, `FOLDR100.PRG` first in `AUTO`; with STinG, its
Delay set to the machine ([why](docs/GUIDE.md#installing)).

Everything ECal does, and how fast: [docs/FEATURES.md](docs/FEATURES.md).

## How it reaches the servers

```
ST, STE:     Atari (ECAL.ACC) --plain HTTP, home LAN--> Gateway (ecalgate.py) -----HTTPS--> the server
Falcon, TT:  Atari (ECAL.ACC + TLS.OVL: TLS 1.2, RSA, curves on the DSP) -------------HTTPS--> the server
             (a Direct connection; or through the Gateway, as an ST)
```

## Building

```sh
sudo apt install gcc gcc-m68k-linux-gnu python3 zip   # Debian/Ubuntu; no MiNTLib needed
make                          # binary/: ECAL.ACC, ECAL.PRG, TLS.OVL, CACERT.PEM, ROOTS.DAT
make test                     # the core's unit tests, the fuzz inputs, the gateway's tests
make itest                    # against a local CalDAV server (Radicale)
make dist HCP=/path/to/hcp    # dist/: a release's files and ECAL-x.y.zip
```

Releases, the help file and the icons: [docs/DEVELOPING.md](docs/DEVELOPING.md).

## Source

```
src/      the calendar core: iCalendar, repeats, CalDAV, sync
ui/       the GEM program
atari/    GEMDOS files and time
common/   shared with the other AERA programs (AERA-Atari)
gateway/  the Gateway's services, from AERA-Gateway
tests/    unit, fuzz, CalDAV and Hatari tests
tools/    release, help file, icons
docs/     the user guide (ECAL.HYP's source), FEATURES.md, DEVELOPING.md
```

## Tests

- `make test`: dates, zones, iCalendar, every RFC 5545 repeat example, XML.
- `make fuzz`: the parsers under libFuzzer.
- `make itest`: sync against Radicale, direct, through the Gateway and with ECal's TLS.
- `tests/hatari/`: ECal in Hatari on the family's machines ([the rigs](docs/DEVELOPING.md#tests)).

## Licence

GPL version 2 or later, without any warranty ([COPYING](COPYING)). Copyright (C) 2026 AERA | Erez Yaary.
Includes [BearSSL](common/third_party/bearssl) (MIT) and Mozilla's root certificates (MPL 2.0).

---

Part of **AERA** for Atari: [ECal](https://github.com/whomper/ECal) · [EContact](https://github.com/whomper/EContact) · [EMail](https://github.com/whomper/EMail) · [EFTP](https://github.com/whomper/EFTP) · [EHypView](https://github.com/whomper/EHypView) · [ClaudeST](https://github.com/whomper/ClaudeST) · [EWeb](https://github.com/whomper/EWeb) · [EUpdate](https://github.com/whomper/EUpdate) · [EPDF](https://github.com/whomper/EPDF) · 8-bit: [EDOS](https://github.com/whomper/EDOS) · [EFTP_XL](https://github.com/whomper/EFTP_XL) · Pi: [AERA-Gateway](https://github.com/whomper/AERA-Gateway)
