# Notes for working on ECal

ECal is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Speed is set by the stock machines** (Erez, 26 Oct 2026; the guidelines' "Speed"): an 8 MHz ST
  and a stock Falcon (`ST`, `ST-1`, `Falcon-4`), not the DFB1. Design and code for them: draw only
  what changed, keep the 68000's slow operations out of inner loops, do work once. A change to
  drawing, a loop over the data or the start gives its `AESGAP` stretches on `ST` and `Falcon-4`,
  before and after, in its commit message. A GEM release passes the speed rig within the
  guidelines' times. One exception, Erez's (31 Oct 2026): scrolling the hours a line keeps its
  speed, an ST's glide steps of 115 to 155 ms (the copy 40-50, the strip coming in 40-95), not
  smaller steps that would keep each under 100 ms and scroll slower.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a
  GitHub Release. A normal build must leave every tracked file unchanged and shows the version in
  `VERSION` as it is (there is no `-dev` label). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from `main`, nothing else: no test builds, no CI artifacts, no builds from a branch. Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, roll back with a release: revert on `main`, release the next number, and run the Withdraw workflow (`.github/workflows/withdraw.yml`) on the bad one: it puts "Withdrawn: use vX.Y" at the top of its notes.
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  - this repo's checks pass;
  - the rigs on his TOS pass, where this file asks for them;
  - the hub's family checks pass.

  Then give Erez the link in one line, with anything he has to do, and send the hub the link. Roll
  back a bad release the same way. Decisions that are his still go to him first.
- **Who decides what** (Erez, 31 Oct 2026; the guidelines' "Who decides what"). Erez's part, put
  to him before the work as a decision with a short context:
  - new programs and ideas, their core capabilities, and how they work with the other programs;
  - the user experience: the look, the layout, the way through a task, the words, the icons, the
    defaults;
  - phases: work stays inside the phase he approved; at its end, one phase report (what it set out
    to do, what was done with the release links, what is left, the next phase as a proposal), and
    the next phase starts when he says so;
  - priorities and conflicts between programs or sessions (through the hub, with its options and
    recommendation);
  - packages on the Pi and ports.

  How it is made is the team's, without asking: code, structure, shared code, tests, measurements,
  fixes, docs and releases inside an approved phase.
- **No test with another AERA program or accessory** (Erez, 8 Nov 2026; guidelines 1.2, AERA-Atari
  76417f7): `make test`, `make itest`, the Hatari rigs, the speed rig and session checks run ECal alone;
  nothing AERA is loaded beside it, started by it or fetched for it (no `getrelease.py`). What ECal does
  with another program is tested against a stand-in this repo owns: `fakecontact.c` (ECONTACT.ACC,
  PEOPLE), `fakeguide.c` (ST-GUIDE.ACC, the Help key). The real programs together are the hub's family
  checks. ECAL.PRG with ECAL.ACC (twin) is ECal alone and stays.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## ECal

- **A release** carries `ecal.acc`, `ecal.prg`, `tls.ovl` (common's, never built here; guidelines
  7.4), `ecal.hyp` and `.ref` (hcp from `common/tools/build-hcp.sh`), `cacert.pem`, `roots.dat` and
  the icon, all in `ecal-x.y.zip` too. The icons are committed in
  `icons/` (`make icons`).
- **File names in lowercase** (guidelines 6.6, Erez, 2 Nov 2026): every file and folder ECal opens,
  creates, looks for, renames or deletes by a name of its own is written in lowercase in the code
  (`ecal.inf`, `cache\cal00001.dat`, `tls.ovl`...), and `make dist` checks the release has no capital.
  AES names (`ECAL    `), the clipboard's `SCRAP.*` and names in texts, alerts and help stay as they
  are; names from outside are used as they came. A rig reading ECal's files from the host uses
  `ci_path` (`common/tools/machines.py`).
- Speed: `tests/hatari/speed_test.py` (a profiled build: `make OBJDIR=build/prof20 BIN=build/prof20/bin
  AESPROF=20 all`) times the guidelines' actions on `ST:mono`, `ST:med` and `Falcon-4:16` (with
  `AERA_WITH_NVDI=1` too); `--acc` the accessory (on `ST-1` and `Falcon:16` only), `--again` a second start on the first's files (the
  snapshots). Its VIEWMS/REDRAWMS lines say where a view's time goes.
- Before pushing: `make test` (the core, the fuzz inputs again, the
  gateway, the snapshots, a series' occurrences with and without the jump; `make rrjump-m68k` runs
  that one as 68000 code under qemu-m68k), `make itest` (cal-cli against Radicale: `pip install
  radicale`; also `GATEWAY=1` and `FALCON=1` when the connection code
  changes), and `make` with no warnings. When the GEM side changes, the
  Hatari rigs in `tests/hatari/`, on the family's machines (`--machine`, from
  `common/tools/machines.py`): the accessory's rigs on `ST-1` (`:mono` and `:med`), the
  others, Direct connection among them, on `TT` and `Falcon`, drawing also on `Falcon:256`. One
  ecal.acc/prg for every machine; the rigs put `binary/tls.ovl` in ECal's folder.
- **Before a GEM release**, run these rigs on his TOS images (`~/.aera/tos`: TOS 1.04 on
  ST/ST-1, TOS 4.04 on the Falcons; and his NVDI in `~/.aera/nvdi/x` with `AERA_WITH_NVDI=1`), then send the
  hub the commit and the results; the hub runs only the family checks (`acc_all.py`):
  - `acc_test.py` and `font_acc.py` (accessory, `--prg`, `--font-id`) on `Falcon:16`, with and
    without NVDI;
  - `acc_test.py`, `acc_files.py` and `quiet_test.py` on `ST-1` (TOS 1.04);
  - `edit_test.py`, `esc_test.py` and `ui_tour.py` on `Falcon:16`; `Falcon:256` when drawing changes.

  The accessory isn't tested on the 4 MB machines, `ST` and `Falcon-4` (Erez, 4 Nov 2026; AERA-Atari
  21ba998): `ST-1`, `TT` and `Falcon` only. ECAL.PRG on its own stays tested on `ST` and `Falcon-4`.

  His TOS images and NVDI are his copies: never in git (nor a test's `hd`), never sent anywhere. EmuTOS
  stays for development and CI.
- `gateway/` is a read-only copy of [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s
  (ecalgate, the other gateways, `aera-gateway`): change it there, with that repository's owner
  session, and bring it here with `tools/sync-gateway.sh`. ECal decides ecalgate's protocol
  (feed trimming, texts, port 8180): review changes to it there.
- ECal and EContact meet only through PEOPLE (guideline 4.1): EContact
  runs its joint rigs (PEOPLE, quiet start and sync with ECal) against
  ECal's releases.

## Talking with Erez

Erez's rule for the whole AERA session family:

- No progress or in-between messages. Write to him only when he has to decide something: a short
  context, then the decision.
- Findings and analysis: the information, your assessment, and what he should do. Nothing more.

Messages to the hub session stay complete, with the evidence.

## Maintenance mode (Erez, 1 Nov 2026)

ECal is in maintenance mode while EWeb, EPDF and EPlay go to 1.0 (AERA-Atari, ATARI-APP-GUIDELINES.md, section 8). Until Erez ends it:
- No new features, speed rounds, clean-ups, or catching up with new or changed guidelines.
- No sync of the shared code (`common/`, `xl/`, `gateway/`). Shared code moving is no reason to rebuild or release.
- A release only for:
  - a security or data-loss fault;
  - a crash or breakage Erez reports;
  - something outside that stops it working (a certificate, a server);
  - something EWeb, EPDF or EPlay need from it.

  Such a release takes only that fix (one change from AERA-Atari, not a sync), and its notes say why.
- Work on `main` that isn't released stays unreleased.
- Nothing runs in the background; the session waits for Erez or the hub.
- Only Erez's explicit word ends this mode, for instance that every app is to take the latest shared code and release. The hub passes it on quoting him.
- The one exception (Erez, 2 Nov 2026: "Start fresh, and roll it out to all apps"): file names in lowercase (guidelines 6.6), with the shared code it needs and a release. Nothing else rides along.
