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
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from `main`, nothing else: no test builds, no CI artifacts, no builds from a branch. Work lands on `main`; to try a fix on real hardware, release it. A fault found in a release is fixed forward by the next number, before any other work (guidelines 1.2, 17 Nov 2026): the old release stays, with one line in the new one's notes saying what it fixes (no revert, no "withdrawn" notice).
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  the candidate has passed **the regression gate** (Erez, 17 Nov 2026: "we need to be extra careful with
  regression issues as there are users of our software. This means testing across all configurations" and
  "make sure we don't loose performance"; guidelines 1.2, AERA-Atari a22574e), in this order:
  1. this repo's checks (below);
  2. its rigs on every row of `standards/TEST-MATRIX.md`'s gate table ECal runs on, on Erez's TOS images and
     NVDI (EmuTOS is never the gate's evidence), each named in the report with `describe()`, a row ECal
     can't run named with why;
  3. the paired speed run against the last release on `Falcon-4:256` with NVDI, `Falcon-4:16` and `ST:mono`,
     the timed build at the shipped layout: no row slower beyond a tick on any (098183d); a slower row is
     fixed first, and only Erez accepts one;
  4. the candidate commit and the results sent to the hub, which runs the family check and answers go or
     the faults it found.

  Every fault a user reports becomes a case in ECal's rigs, in the release that fixes it.

  Then give Erez the link in one line, with anything he has to do, and send the hub the link.
  Decisions that are his still go to him first.
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
  AESPROF=20 all`, linked at the shipped build's places modulo 256 by `common/tools/profpad.py`: the
  shipped build in `build/` is made first) times the guidelines' actions on `ST:mono`, `ST:med` and `Falcon-4:16` (with
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

  **From the next release (Erez, 10 Nov 2026; AERA-Atari 148df17):** his configuration,
  `Falcon-4:256` with `AERA_WITH_NVDI=1` (`machines.ERZ_MACHINE`, `machines.erz_env()`), replaces one
  Falcon run, not added to them: `acc_test.py` and `font_acc.py` (accessory, `--prg`, `--font-id`)
  become one run on it, in place of `Falcon:16` with and without NVDI. That run takes the accessory on a
  4 MB machine (his configuration; the 4 Nov rule above is for the others). `edit_test.py`,
  `esc_test.py` and `ui_tour.py` stay on a Falcon without NVDI only where a different function needs
  it. The gate (above, 17 Nov 2026) adds the rest of TEST-MATRIX.md's rows.

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

## Maintenance mode (Erez, 1 Nov 2026; changed 17 Nov 2026)

ECal is in maintenance mode while EWeb, EPDF and EPlay go to 1.0 (AERA-Atari, ATARI-APP-GUIDELINES.md, section 8):
- No new features and no speed rounds of its own.
- **Every family rule holds for ECal as for the focus apps** (Erez, 17 Nov 2026: "there needs to be a constant
  behavior across all applications. In the future, all applications must adhere to family rules and be
  notified. We will review whether they need to issue a release." and "Applications on maintenance does not
  mean they contain faults"; AERA-Atari 8ddb463). The hub notifies ECal of each new or changed rule and each
  shared fix: ECal adopts it, with the shared code it needs (`tools/sync-common.sh`), and the hub ticks its
  row in section 8's "Where each app stands". Each ✗, ~ or ? in ECal's column there is work for this session.
- Whether such a change needs a release of its own is reviewed with Erez through the hub; a fault fix always
  gets one.
- A release otherwise only for a security or data-loss fault, a crash or breakage, something outside that
  stops it working (a certificate, a server), or something EWeb, EPDF or EPlay need from it.
- Nothing runs in the background; the session waits for Erez or the hub.
- Only Erez's explicit word ends this mode. The hub passes it on quoting him.

## UI backlog (Erez's 54 decisions, 9 Nov 2026; AERA-Atari a261597, standards/ui)

Part of the family rules above (Erez, 17 Nov 2026): every item is to be done, its words and looks as
standards/ui/DECISIONS-2026-11-09.md has them; anything not decided there goes to the hub first, as a
question for Erez. Every GEM release still passes the speed rig. When an item is done, send the
hub one line with the commit (it ticks ECal's row in the guidelines' section-8 table). [hub] = waits for the
shared code first; the hub says when it is in `common/`.

1. File lists `Quit` alone in the program; the accessory's ≡ menu keeps Close (D3, MD7). S.
2. ≡ menu: About first; help entry `ECal help  Help` and `Keys...`, as the bar (MG16, MD16). S. Adjust
   `tests/hatari/font_acc.py`, which counts Font... from the end.
3. `Direct connection` without `(TLS)` (D9, MD6). S.
4. `Add account or calendar...` moves to Options, before Settings (D13, MD27). S.
5. The event pop-up says `Copy event ^C`, the bar's words (D14, MD19). S.
6. A black title band in dialogs that lack one; in a MiNT or MagiC window the window's title replaces it
   (D22, C4). S.
7. Tick boxes become bitmap ticks (D23, C5). S.
8. One-of-several choices of four or fewer become radio buttons (D24, C5). [hub: the engine]
9. A pop-up opens with the choice ticked over its button (`ui/field.c:dlg_run`, FT_POPUP, into `popup()`)
   (D27, C8). S. [hub]
10. Window: one minimum for the open check (50 x 16 today) and `WM_SIZED` (40 x 14) (D38, W10); the first run
    on the whole desk (D39, W11). S each.
11. Insert opens the context menu (D17, MD20). H. Where the box opens is still open.
12. Buttons act on the release over them (C7); keyboard reach: Space on ticks, arrows in lists (D25, C6). H. [hub]
13. ECAL.INF: `WINDOW=x,y,w,h` and `FONT=name,points`, the old keys read once (D43, D44, W18). M. `SIDEBAR=`
    stays.
14. The older FIELD dialogs to the AES look (rows, focus ring) (C24). M, several days.
15. Tooltips on the toolbar, 0.7 s (D18). [hub] The event tips (700 ms) and dialog tips (500 ms) stay.

Still open, not to start: Find's words (D4); press and release for ticks and pop-ups (C7). Nothing to do: the
out-of-range alerts (ECal is the model, D30), `Show|Later` (D51), the palette (D42), the toolbar's look (D33).
