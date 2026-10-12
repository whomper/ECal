# ECal

**Highlights**

- iCloud and other CalDAV calendars, two-way sync; `.ics` subscriptions; local calendars
- Day, week, month and year views, like the Mac's Calendar
- Desk accessory or program, for the ST, STE, TT and Falcon
- Falcon mode: TLS 1.2 on the Falcon itself, RSA on the DSP; any Atari through a Raspberry Pi gateway
- Hebrew throughout: right-to-left text, Israeli keyboard (F10), Hebrew dates and Hebrew-calendar repeats
- Every RFC 5545 repeat rule, with "only this one" or "all future ones" edits
- Reminders over any program; time zones; works offline from its own copy
- Runs in TT RAM when there is some; GDOS fonts; hover tooltips

A calendar for the Atari ST, STE, TT and Falcon, written in C for GEM.
It runs as a **desk accessory**, so it opens from the Desk menu over
whatever program you're working in, and it works like the Mac's Calendar:
day, week, month and year views, several calendars in colour, iCloud and
other CalDAV accounts, subscriptions to published calendars, reminders,
time zones, and Hebrew.

![Month view on a Falcon](docs/month.png)

- **iCloud** and any **CalDAV** server (Fastmail, Nextcloud, Radicale,
  Synology, Yahoo...): calendars found automatically, events added, changed
  and deleted on the server, changes made elsewhere fetched with ETags so
  only what changed comes over the line
- **Subscriptions**: `webcal://` and `https://` `.ics` feeds (holidays,
  sports, a Google calendar's secret iCal address), refreshed on a schedule
- **On My Atari** calendars, kept only on this computer; `.ics` import
- **Views**: day (with the selected event's details), week, month, year;
  all-day and multi-day events as bars, overlapping events side by side,
  "+3 more" when a day is full
- **Today**: today's date in red everywhere, a red line at the current time,
  a Today button, and the view moves on at midnight
- **Repeating events**: every kind of repeat iCalendar has (RFC 5545:
  yearly to every second, any BYDAY/BYMONTHDAY/BYYEARDAY/BYWEEKNO/BYMONTH/
  BYHOUR/BYMINUTE/BYSECOND, BYSETPOS, WKST, extra dates (RDATE), excluded
  dates and rules (EXDATE, EXRULE)), plus Hebrew-calendar repeats
  (RFC 7529 `RSCALE=HEBREW`, "every year on 12 Tishri"). The Repeat menu
  has the usual ones; **Custom...** builds any rule ("the last Friday of
  every month", "the second Monday of March and September"), or type it
- **One occurrence or the series**: move, change or delete just one
  occurrence of a repeating event, or this and all future ones, as on the
  Mac; changes made on an iPhone to one occurrence or to "this and future"
  show as they should
- **Reminders** that pop up over any program, even with the window closed
- **Time zones**: events keep their zone; with time zone support on, see
  your calendar as it looks from another city. Summer time rules for Israel
  (the Friday before the last Sunday of March), Europe, the Americas,
  Australia and more are built in; others are learned from the calendar data
- **Hebrew**: titles, places and notes in Hebrew, right to left, typed on
  the Israeli keyboard (F10) in every field, like EMail; Hebrew calendar
  names; Hebrew dates (י״ב תשרי תשפ״ז) next to the civil ones
- **Cancelled events** are left out: cancelled by the organiser,
  titled "Canceled:" (Outlook, Exchange), or invitations you declined
- **Find** (^F) through titles, places and notes
- **Tooltips**: rest the mouse on an event to see its title and times
- **Fonts**: the system font or any monospaced GDOS font (NVDI,
  SpeedoGDOS), picked in EMail's font selector; twelve calendar colours
- **The calendar list** hides with a toolbar button (or ^S), leaving the
  window to the calendar
- Keeps a copy of everything on disk: opens at once, shows today before
  it syncs, and works offline (the menu > Work offline, as in EMail)
- **TT RAM**: on a TT or an accelerated Falcon the program and its memory
  go to TT/Fast RAM, leaving ST RAM free; the About box says so
- Runs on TOS 1.04 to 4.x, EmuTOS, MagiC and MiNT, with STinG or MiNTnet,
  in ST high, ST medium, TT and Falcon resolutions; with NVDI the views
  draw several times faster than with TOS's own VDI

## How it reaches the servers

Calendar servers want HTTPS. Like [EMail](https://github.com/whomper/EMAIL),
ECal gets there one of two ways:

```
ST, STE, any Atari:  Atari (ECALST.ACC) --plain HTTP, home LAN--> Raspberry Pi (ecalgate.py) --HTTPS--> iCloud
Falcon, TT:          Falcon (ECAL.ACC, TLS 1.2, RSA on the DSP) ---------------------HTTPS--> iCloud
```

- **Through the Pi.** `gateway/ecalgate.py` on a Raspberry Pi takes the
  Atari's requests and makes the HTTPS connections. It also does what an
  8 MHz machine finds hard: it unpacks compressed answers, keeps
  connections to the servers open, and cuts multi-megabyte subscription
  feeds down to the months you keep. See [gateway/README.md](gateway/README.md).
- **Falcon mode.** On a Falcon (or a TT), ECal does TLS 1.2 itself
  with [BearSSL](https://bearssl.org/), and the DSP56001 checks the servers'
  RSA signatures (about 0.1 s each), as in EMail.

## Performance: Pi, ST and Falcon

Where the work happens decides the speed. Figures are from the emulated
Falcon (16 MHz 68030) and from counting 68030 instructions; a real
iCloud sync also depends on the network and on how many events change.

| | ST through the Pi | Falcon through the Pi | Falcon mode, DSP | Falcon mode, no DSP |
|---|---|---|---|---|
| TLS, certificates | on the Pi | on the Pi | 68030 + DSP | 68030 alone |
| New secure connection | none on the Atari | none on the Atari | a few seconds once, then ~0.7 s (resumed) | slower: the RSA check runs on the 68030 |
| RSA signature check | — | — | ~0.1 s | several times longer |
| Decrypting answers | — | — | ~200 instructions a byte | the same |
| Compressed answers | unpacked by the Pi | unpacked by the Pi | unpacked by ECal, ~10 instructions a byte | the same |
| Big subscription feeds | trimmed by the Pi to the months kept | trimmed by the Pi | parsed whole by ECal | parsed whole |

- **Through the Pi** is the fastest way for any Atari: the Atari only
  sends and reads plain HTTP on the home network, and the Pi keeps the
  connections to the servers open. On an 8 MHz ST, drawing and reading
  the calendar data take most of the time, not the network.
- **Falcon mode** needs no Pi. After the first connection each request
  costs well under a second; most of a first sync goes into the first
  handshake and into decrypting what comes back, which compression keeps
  small. Saving an event takes about a second.
- **The DSP** only speeds up the certificate checks at the start of each
  new connection; with session resumption there are few of those, so
  it matters most for the first sync after starting ECal.
- On a TT or an accelerated Falcon, running in TT/Fast RAM makes
  everything ECal does itself faster.

In Falcon mode the 68030 decrypts every byte itself and doesn't read
from STinG while it does, so receiving and decrypting take turns: the
speed is about 1/(1/network + 1/decrypt). EMail measured 13 KB/s that
way against 37 KB/s through the Pi. Faster decryption is all ECal can
do about it (table-based AES and the fastest GHASH, 16 KB reads, long
copies on the 68030); a bigger TCP receive window in STinG's own
configuration lets the two overlap. With **Write a log** on, every line
of `ECAL.LOG` starts with the time of day to the hundredth of a second,
so you can see where a sync spends its time.

## The files

| File           | For                                                               |
|----------------|-------------------------------------------------------------------|
| `ECAL.ACC`     | Falcon and TT: the accessory with Falcon mode (TLS, DSP); also works through the Pi |
| `ECALST.ACC`   | ST and STE: the same accessory without TLS (150 KB smaller), through the Pi |
| `ECAL.PRG`     | the same as ECAL.ACC, to start from the desktop instead       |
| `CACERT.PEM`, `ROOTS.DAT` | the root certificates for Falcon mode                  |

## Getting started

1. Copy `ECAL.ACC` (Falcon/TT) or `ECALST.ACC` (ST/STE, renamed to
   `ECAL.ACC` or not) into the root of your boot drive, e.g. `C:\`.
   For Falcon mode, make a folder `C:\ECAL` and put `CACERT.PEM` and
   `ROOTS.DAT` in it. (ECal keeps its own files there too.) The folder
   can be anywhere: the settings, `C:\ECAL.INF`, say where in `FOLDER=`;
   start `ECAL.PRG` from the folder once, or show the accessory where
   it is when it asks.
2. For an ST, set up the gateway on a Raspberry Pi:
   `sudo gateway/install.sh` (see [gateway/README.md](gateway/README.md)).
3. Reboot, choose **ECalendar** in the Desk menu, and add your iCloud
   account with an [app-specific password](https://support.apple.com/en-us/102654).

The [user guide](docs/GUIDE.md) describes the rest.

![Desk menu](docs/desk-menu.png) ![New event with a Hebrew title](docs/new-event.png)

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # ECAL.ACC, ECAL.PRG, ECALST.ACC
```

The program is freestanding (`atari/`, no MiNTLib); `tools/elf2tos.py`
writes the TOS executable. One binary is both the accessory and the
program: the start-up code (`atari/crt0.S`) sees which way TOS started it.
Everything is 68000 code except BearSSL, which is built for the 68030.

**Version numbers.** As in EMail, `VERSION` holds major.minor (e.g.
1.25) and every build counts the minor number up: `make` does it
whenever anything was compiled again (`tools/bump.py`). The About box
shows it, and it's in the User-Agent ECal sends. Commit `VERSION` with
the program.

## Source

```
src/      the calendar core, also built on Linux for the tests:
          ctime.c  dates and the Hebrew calendar      tz.c      time zones
          ics.c    iCalendar                          rrule.c   repeat rules
          xml.c    WebDAV answers
          http.c   HTTP/1.1, via the Pi or with TLS   caldav.c  CalDAV
          calstore.c  accounts, caches, sync, saving changes
          (charset, bidi, conn, tls, secret: from EMail)
ui/       the GEM program: window, views, sidebar, event editor, dialogs,
          and field.c, dialogs whose text fields take Hebrew
atari/    TOS runtime from EMail (start-up for accessories added), STinG, DSP
dsp/      the DSP56001 RSA program (from EMail)
gateway/  ecalgate.py, the Raspberry Pi gateway, and its installer
tests/    unit tests, CalDAV integration tests, Hatari tests
```

## Tests

```
make test                    # the core: dates, Hebrew dates, zones, iCalendar, repeats,
                             # repeat rules with all of RFC 5545's examples, XML, URLs
                             # (231 checks); and the gateway (8 tests)
make itest                   # cal-cli against a real CalDAV server (Radicale): sync,
GATEWAY=1 make itest         # add, rename, move one occurrence, delete one or the
FALCON=1 make itest          # rest, changes from elsewhere, subscriptions, import;
                             # direct, through ecalgate, and with ECal's own TLS
                             # (pip install radicale)
```

`tests/hatari/` runs the real accessory in the
[Hatari](https://hatari.tuxfamily.org/) emulator with
[EmuTOS](https://emutos.sourceforge.io/), driven by Xvfb and xdotool:

```
tests/hatari/acc_test.py WORK etos512us.img                # as a desk accessory, Falcon
tests/hatari/edit_test.py WORK etos512us.img               # add, Hebrew title, find, delete,
                                                           # one occurrence, custom repeats
tests/hatari/edit_test.py WORK etos512us.img --machine st --acc ECALST.ACC
                                                           # the same on an ST, up to delete
tests/hatari/net_test.py WORK etos512us.img                # an ST syncing through ecalgate
tests/hatari/net_test.py WORK etos512us.img --falcon-mode  # TLS on the Falcon, RSA on the DSP
tests/hatari/ui_tour.py WORK etos512us.img                 # screenshots of every view
```

The network tests use EMail's FAKESTNG.PRG (`make -C tools/fakesting`),
which carries the emulated Atari's TCP over its serial port; it's for the
emulator only, never for a real Atari.
