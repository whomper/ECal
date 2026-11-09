# Developing ECal

Building, the source tree and the tests, in full. The [README](../README.md) has the short version.

## Building

```sh
sudo apt install gcc gcc-m68k-linux-gnu python3 zip   # Debian/Ubuntu; no MiNTLib needed
make                          # binary/ECAL.ACC, ECAL.PRG, TLS.OVL (common's), CACERT.PEM, ROOTS.DAT
make test                     # the core's unit tests on this computer, the fuzz inputs, the gateway's tests
make itest                    # against a local CalDAV server (Radicale)
make help HCP=/path/to/hcp    # binary/ECAL.HYP from docs/GUIDE.md (hcp: common/tools/build-hcp.sh)
make dist HCP=/path/to/hcp    # dist/: a release's files, each on its own and in ECAL-x.y.zip
make icons                    # icons/, ui/icon*.h, docs/icon*.png: ECal's icon (committed),
                              # from common/icons/ECAL.txt
```

The program is freestanding (no MiNTLib): the C library and the TOS runtime are in `common/atari/`,
and `common/tools/elf2tos.py` writes the TOS executable. One binary is both the accessory and the
program: the start-up code (`common/atari/crt0.S`) sees which way TOS started it. It is all 68000
code, for every Atari. The TLS (BearSSL and the DSP's code, built for the 68030) is in `TLS.OVL`, the
family's file built in AERA-Atari and taken with `common/` (`common/tlsovl/`), never built here; the
build machine's tests link the same TLS code (`common/src/tls.c`) into cal-cli.

`binary/` and `dist/` are what make builds; they are never committed, and a build changes no tracked
file.

**Versions and releases.** `VERSION` holds the last release, major.minor (e.g. 2.10), and the build
never changes it. Every build shows it as it is in the About box and the User-Agent ECal sends. Only
releases leave the build machine. To release: `tools/bump.py`, commit and push, then run
the Release workflow (**Actions > Release > Run workflow**, on `main`) with what is new:
[`.github/workflows/release.yml`](../.github/workflows/release.yml) tags the commit `v2.11`, tests, builds
the programs and the help file, and publishes them as a GitHub Release with those notes. Pushing a tag
`v2.11` made with `git tag -a` does the same, with the tag's message as the notes.

## Source

```
src/      the calendar core, also built on Linux for the tests:
          ics.c       iCalendar               rrule.c    repeat rules
          xml.c       WebDAV answers          inflate.c  compressed answers
          http.c      HTTP/1.1, via the Gateway or with TLS
          caldav.c    CalDAV                  calstore.c accounts, caches, sync, saving changes
ui/       the GEM program: window, views, sidebar, event editor, dialogs, fonts, About box,
          and field.c, dialogs whose text fields take Hebrew
atari/    plat_tos.c: files, folders and time on GEMDOS
common/   code shared with the other AERA programs (from AERA-Atari): the TOS runtime and
          C library, STinG, TLS and connections, BearSSL, the DSP RSA program, charsets,
          bidi, dates and the Hebrew calendar (ctime.c), time zones (tz.c), passwords,
          GEM building blocks (menus, alerts, help, AV, iconify), build tools
gateway/  the Gateway's services (ecalgate.py is ECal's) and aera-gateway, their installer:
          a copy of AERA-Gateway's
tests/    unit tests, fuzz targets, CalDAV integration tests, Hatari tests
tools/    bump.py (a release), guide2stg.py (the help file), icon/ (the icons),
          sync-common.sh (common/ from AERA-Atari), sync-gateway.sh (gateway/ from AERA-Gateway)
docs/     the user guide (GUIDE.md, the source of ECAL.HYP) and its pictures
```

## Tests

```
make test                    # the core: dates, Hebrew dates, zones, iCalendar, repeats,
                             # repeat rules with all of RFC 5545's examples, XML, URLs
                             # (305 checks); the fuzz inputs again (tests/fuzz/seeds,
                             # and found/: what once broke a parser); the gateway's tests
make fuzz                    # the CalDAV answers, iCalendar, repeat rules, XML and
                             # inflate from libFuzzer (clang), FUZZTIME seconds each
make itest                   # cal-cli against a real CalDAV server (Radicale): sync,
GATEWAY=1 make itest         # add, rename, move one occurrence, delete one or the
FALCON=1 make itest          # rest, changes from elsewhere, subscriptions, import;
                             # direct, through ecalgate, and with ECal's own TLS
                             # (pip install radicale)
```

`tests/hatari/` runs the real accessory in the [Hatari](https://hatari.tuxfamily.org/) emulator with
[EmuTOS](https://emutos.sourceforge.io/) 1.4, driven by Xvfb and xdotool. The machine is one of the
family's test machines from `common/tools/machines.py`, named with `--machine`: `ST` (Erez's 1040 ST,
4 MB) and `ST-1` (1 MB), each with `:mono` or `:med`; `TT` and `Falcon` (`:16` or `:256` colours),
where the Direct connection rigs run. The program is ECAL.ACC or ECAL.PRG on all of them; the rigs put
`binary/TLS.OVL` in ECal's folder. Each rig has a default, and prints the machine and TOS it
ran on. An ST has no clock, so the rigs set its date and time at boot (`C:\AUTO\CLOCK.PRG`), as you
would from the Control Panel:

```
tests/hatari/acc_test.py WORK [--machine M]       # as a desk accessory (Falcon)
tests/hatari/acc_files.py WORK [--machine M] [--run quiet|sync|falcon]
                                                  # the accessory's disk access, job by job,
                                                  # from Hatari's GEMDOS trace (ST-1:mono)
tests/hatari/font_acc.py WORK [--machine M] [--prg] [--font-id N]
                                                  # a GDOS font chosen: the window redraws in
                                                  # it, and opens again (Falcon; skipped
                                                  # without a GDOS)
tests/hatari/edit_test.py WORK [--machine M]      # add, Hebrew title, find, delete, one occurrence,
                                                  # custom repeats (Falcon; on an ST up to delete)
tests/hatari/alarm_test.py WORK [--machine M]     # reminders from ALARMS.IDX at boot (Falcon)
tests/hatari/net_test.py WORK [--machine M]       # syncing through ecalgate (ST-1)
tests/hatari/net_test.py WORK --falcon-mode       # a Direct connection on the Falcon (TLS.OVL),
                                                  # RSA and X25519 on the DSP
tests/hatari/tlsovl_test.py WORK [--machine M]    # Direct connection ticked: without TLS.OVL an
                                                  # alert, no crash; with it, loaded (Falcon)
tests/hatari/stop_test.py WORK [--machine M]      # Esc stops a server that says nothing (ST-1)
tests/hatari/quiet_test.py WORK [--machine M]     # make profile first: the accessory's quiet
                                                  # start, timed between event calls (Falcon)
tests/hatari/bgsync_test.py WORK [--machine M] [--falcon-mode]
                                                  # make profile first: a background refresh
                                                  # with the window closed, timed (Falcon)
tests/hatari/mem_test.py WORK [--machine M] [--falcon-mode] [--cals 3] [--events 500] [--boots N]
                                                  # make profile first: the accessory's memory
                                                  # (heap peak, MEMKB=) as calendars come new
tests/hatari/esc_test.py WORK [--machine M]       # Esc and Undo cancel dialogs and alerts, and
                                                  # nothing is written (Falcon)
tests/hatari/people_test.py WORK [--machine M] [--acc]
                                                  # inviting through EContact (PEOPLE), a
                                                  # stand-in (make fakecontact; Falcon)
tests/hatari/invite_test.py WORK [--machine M] [--acc]
                                                  # an .ics invitation by VA_START: the New
                                                  # Event dialog filled, saved with its UID
tests/hatari/peoplefile_test.py WORK [--machine M] [--acc] [--econtact]
                                                  # EContact's CONTACTS.TXT with EContact not
                                                  # loaded: names, Add...; with the stand-in
                                                  # EContact (--econtact), never read
tests/hatari/notes_test.py WORK [--machine M] [--acc]
                                                  # notes read from the cache when wanted:
                                                  # Find, the details, saved with an edit
tests/hatari/field_test.py WORK [--machine M] [--acc]
                                                  # fields hold more than they show (5.4):
                                                  # a long title, ^C ^V, a long address
tests/hatari/title_test.py WORK [--machine M]     # the toolbar's title fits ("18-24 Oct", not
                                                  # "18-24 Oct 202"), each view (Falcon)
tests/hatari/twin_test.py WORK [--machine M]      # ECAL.PRG and ECAL.ACC on one machine
                                                  # (EmuTOS: its desktop is clicked)
tests/hatari/help_test.py WORK [--machine M]      # the Help key (make fakeguide first; Falcon)
tests/hatari/ui_tour.py WORK [--machine M]        # screenshots of every view (Falcon)
TZ=America/New_York tests/hatari/docs_shots.py WORK --update [--help-picture]
                                                  # the screenshots in docs/ (Falcon:256, and
                                                  # st-mono.png on ST:mono; EmuTOS); with
                                                  # --help-picture docs/help/WINDOW.IMG (Falcon:16)
```

Which machines a change passes (the family's guidelines): the accessory's rigs on `ST` and `ST-1`, in
mono and medium resolution; the others, the Direct connection ones among them, on `TT` and `Falcon`;
what draws also on `Falcon:256`. On `ST-1` with the network the desktop must still start beside
ECAL.ACC and FAKESTNG; ECal's log says how much memory was free at boot and how much the accessory
took. `make STACKPROBE=1` builds the programs to report the least stack left untouched (`STACKFREE`
in Hatari's log), which sets the stack (`STACK` in the Makefile).

The network tests use the family's `FAKESTNG.PRG`, which carries the emulated Atari's TCP over its
serial port to `serial_bridge.py` (both in `common/tools/fakesting`, started by `machines.py`); it's
for the emulator only, never for a real Atari.

ECal's tests run ECal alone (the family's guidelines, 1.2, Erez 8 Nov 2026): no other AERA program or
accessory is loaded beside it, started by it or fetched for it. What ECal does with another program is
tested against a stand-in from this repository that plays the other side: `ECONTACT.ACC`
(`tests/hatari/fakecontact.c`, `make fakecontact`) answers PEOPLE as EContact, `ST-GUIDE.ACC`
(`fakeguide.c`) takes the Help key's VA_START. ECal with the real EContact, EMail and EHypView is the
hub's family checks. ECAL.PRG with ECAL.ACC (`twin_test.py`) is ECal alone.
