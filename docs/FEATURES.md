# ECal in detail

What ECal does, how it reaches the servers and how fast it is. The [README](../README.md) is the
front page; the [user guide](GUIDE.md) says how to use it.

## What it does

- **iCloud** and any **CalDAV** server (Fastmail, Nextcloud, Radicale, Synology, Yahoo...): calendars
  found automatically; events added, changed and deleted on the server; changes made elsewhere fetched
  with ETags, so only what changed comes over the line.
- **Subscriptions**: `webcal://` and `https://` `.ics` feeds (holidays, sports, a Google calendar's
  secret iCal address), refreshed on a schedule.
- **On My Atari** calendars, kept only on this computer; `.ics` import.
- **Invitations**: an `.ics` invitation (handed over by EMail, opened on the desktop or with File >
  Import .ics...) fills the New Event dialog; saved, it keeps its UID and its organiser, so a later
  update of the same invitation finds the event. Files with several events are imported after asking.
- **Views**: day (with the selected event's details), week, month and year; all-day and multi-day
  events as bars, overlapping events side by side, "+3 more" when a day is full.
- **Today** in red everywhere, a red line at the current time, a **Today** button, and the view moves
  on at midnight.
- **Repeating events**: every kind of repeat iCalendar has (RFC 5545: yearly to every second, any
  BYDAY/BYMONTHDAY/BYYEARDAY/BYWEEKNO/BYMONTH/BYHOUR/BYMINUTE/BYSECOND, BYSETPOS, WKST, extra dates
  (RDATE), excluded dates and rules (EXDATE, EXRULE)), plus Hebrew-calendar repeats (RFC 7529
  `RSCALE=HEBREW`, "every year on 12 Tishri"). The **Repeat** menu has the usual ones; **Custom...**
  builds any rule ("the last Friday of every month", "the second Monday of March and September"), or
  type it in.
- **One occurrence or the series**: move, change or delete just one occurrence of a repeating event,
  or this and all future ones, as on the Mac. Changes made on an iPhone to one occurrence or to "this
  and future" show as they should.
- **Reminders** pop up over any program, even with the window closed.
- **Syncs in the background**: with its window closed the accessory brings the calendars up to date a
  little at a time while the machine is idle, fetching only what changed, so new events and their
  reminders arrive without opening ECal.
- **Time zones**: events keep their zone; with time zone support on, see your calendar as it looks
  from another city. Summer time rules for Israel (the Friday before the last Sunday of March),
  Europe, the Americas, Australia and more are built in; others are learned from the calendar data.
- **Hebrew**: titles, places and notes in Hebrew, right to left, typed on the Israeli keyboard (F10)
  in every field, as in EMail; Hebrew calendar names; Hebrew dates (י״ב תשרי תשפ״ז) next to the civil
  ones.
- **Cancelled events** are left out: cancelled by the organiser, titled "Canceled:" (Outlook,
  Exchange), or invitations you declined.
- **Find** (^F) through titles, places and notes.
- **Tooltips**: rest the mouse on an event to see its title and times.
- **Fonts**: the system font or any monospaced GDOS font (NVDI, SpeedoGDOS), picked in the family's
  Font dialog; twelve calendar colours, the same in 16 and 256 colours. With NVDI the views draw
  several times faster than with TOS's own VDI.
- **3D dialogs**: where the AES draws 3D (TOS 4, MagiC, N.AES, XaAES, EmuTOS in colour), ECal's
  dialogs and alerts take its look in 16 colours or more: grey ground, the AES's own raised
  buttons, white fields; TOS 1 to 3 keep the flat look.
- **Keys as in GEM**: Esc empties the text field it is in, Undo cancels a dialog; ^U closes the
  window (`ECAL.PRG` quits). Reminders are BGEM.NG notifications where it runs (MiNT).
- **MiNT and MagiC**: where the AES runs several programs at once (XaAES, N.AES, MyAES, MagiC),
  alerts are the AES's own and each dialog opens in a window of its own, named after it: move it
  by its title bar, close it with its closer or Esc, and the other programs go on running.
- **The calendar list** hides with a toolbar button (or ⇧^S), leaving the window to the calendar.
- **Offline**: ECal keeps a copy of everything on disk, so it opens at once, shows today before it
  syncs, and works offline (**Work offline** in the Options or ≡ menu, as in EMail).
- **TT RAM**: on a TT or an accelerated Falcon the program and its memory go to TT/Fast RAM, leaving
  ST RAM free; the About box says so.

## How it reaches the servers

Calendar servers want HTTPS. Like [EMail](https://github.com/whomper/EMail), ECal gets there one of two
ways:

```
ST, STE:     Atari (ECAL.ACC) --plain HTTP, home LAN--> Gateway (ecalgate.py) -----HTTPS--> the server
Falcon, TT:  Atari (ECAL.ACC + TLS.OVL: TLS 1.2, RSA, curves on the DSP) -------------HTTPS--> the server
             (a Direct connection; or through the Gateway, as an ST)
```

- **Through the Gateway.** `gateway/ecalgate.py` on a Raspberry Pi takes the Atari's requests and makes the
  HTTPS connections. It also does what an 8 MHz machine finds hard: it unpacks compressed answers,
  keeps connections to the servers open, and cuts multi-megabyte subscription feeds down to the months
  you keep. Any Atari can use it, a Falcon too, to save time.
- **Direct connection.** On a Falcon (or a TT), ECal does TLS 1.2 itself with
  [BearSSL](https://bearssl.org/), from `TLS.OVL`, the family's file it loads the first time it
  needs it (about 140 KB; an ST never loads it), and the DSP56001 checks the servers' RSA signatures (about 0.1 s
  each), as in EMail, and does the elliptic curves: the key exchange (X25519: two steps of about 0.07 s
  each, 0.4 s on a 16 MHz 68030) and ECDSA certificates (P-256, P-384).

The gateway's own README, with its options and how to keep it private:
[gateway/README.md](../gateway/README.md).

## Performance: Gateway, ST and Falcon

Where the work happens decides the speed. Figures are from the emulated Falcon (16 MHz 68030) and from
counting 68030 instructions; a real iCloud sync also depends on the network and on how many events
change.

| | ST through the Gateway | Falcon through the Gateway | Direct connection, DSP | Direct connection, no DSP |
|---|---|---|---|---|
| TLS, certificates | on the Gateway | on the Gateway | 68030 + DSP | 68030 alone |
| New secure connection | none on the Atari | none on the Atari | a few seconds once, then ~0.7 s (resumed) | slower: the RSA check runs on the 68030 |
| RSA signature check | — | — | ~0.1 s | several times longer |
| Key exchange (X25519) | — | — | ~0.15 s | ~0.8 s |
| Decrypting answers | — | — | ~200 instructions a byte | the same |
| Compressed answers | unpacked by the Gateway | unpacked by the Gateway | unpacked by ECal, ~10 instructions a byte | the same |
| Big subscription feeds | trimmed by the Gateway to the months kept | trimmed by the Gateway | parsed whole by ECal | parsed whole |

- **Through the Gateway** is the fastest way for any Atari: the Atari only sends and reads plain HTTP on
  the home network, and the Gateway keeps the connections to the servers open. On an 8 MHz ST, drawing and
  reading the calendar data take most of the time, not the network.
- **A Direct connection** needs no Gateway. After the first connection each request costs well under a second;
  most of a first sync goes into the first handshake and into decrypting what comes back, which
  compression keeps small. Saving an event takes about a second.
- **The DSP** only speeds up the handshake at the start of each new connection (the certificate checks
  and the key exchange: 2.1 s of work became 1.3 s in the emulated Falcon); with session resumption
  there are few of those, so it matters most for the first sync after starting ECal.
- On a TT or an accelerated Falcon, running in TT/Fast RAM makes everything ECal does itself faster.

With a Direct connection the 68030 decrypts every byte itself and doesn't read from STinG while it does, so
receiving and decrypting take turns: the speed is about 1/(1/network + 1/decrypt). EMail measured
13 KB/s that way against 37 KB/s through the Gateway. Faster decryption is all ECal can do about it
(table-based AES and the fastest GHASH, 16 KB reads, long copies on the 68030); a bigger TCP receive
window in STinG's own configuration lets the two overlap. With **Write a log** on, every line of
`ECAL.LOG` starts with the time of day to the hundredth of a second, so you can see where a sync spends
its time.
