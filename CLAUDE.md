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
- **Testing with another AERA program uses its latest release**, what Erez runs, never a build of its
  `main`: `common/tools/getrelease.py APP DIR FILE...` fetches it. Only for a change both programs are
  still making, test against the other's `main` at a named commit, say so, and test again against its
  release when it is out.
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

- **A release** carries `ECAL.ACC`, `ECAL.PRG`, `ECALST.ACC`, `ECAL.HYP` and `.REF` (hcp from
  `common/tools/build-hcp.sh`), `CACERT.PEM`, `ROOTS.DAT` and the icon. The icons are committed in
  `icons/` (`make icons`).
- Before pushing: `make test` (the core, the fuzz inputs again, the
  gateway), `make itest` (cal-cli against Radicale: `pip install
  radicale`; also `GATEWAY=1` and `FALCON=1` when the connection code
  changes), and `make` with no warnings. When the GEM side changes, the
  Hatari rigs in `tests/hatari/`, on the family's machines (`--machine`, from
  `common/tools/machines.py`): the ECALST.ACC rigs on `ST` and `ST-1` (`:mono` and `:med`), the
  others on `TT` and `Falcon`, drawing also on `Falcon:256`.
- **Before asking Erez for a GEM release**, run these rigs on his TOS images (`~/.aera/tos`: TOS 1.04 on
  ST/ST-1, TOS 4.04 on the Falcons; and his NVDI in `~/.aera/nvdi/x` with `AERA_WITH_NVDI=1`), then send the
  hub the commit and the results; the hub runs only the family checks (`acc_all.py`):
  - `acc_test.py` and `font_acc.py` (accessory, `--prg`, `--font-id`) on `Falcon:16` and `Falcon-4:16`,
    with and without NVDI;
  - `acc_test.py`, `acc_files.py` and `quiet_test.py` on `ST:mono` and `ST-1` (TOS 1.04);
  - `edit_test.py`, `esc_test.py` and `ui_tour.py` on `Falcon:16`; `Falcon:256` when drawing changes.

  His TOS images and NVDI are his copies: never in git (nor a test's `hd`), never sent anywhere. EmuTOS
  stays for development and CI.
- `gateway/` is a read-only copy of [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s
  (ecalgate, the other gateways, `aera-gateway`): change it there, with that repository's owner
  session, and bring it here with `tools/sync-gateway.sh`. ECal decides ecalgate's protocol
  (feed trimming, texts, port 8180): review changes to it there.
- EContact's `integration/ECAL.patch` makes EContact ECal's address
  book: when ECal changes, make that patch again in EContact (see its
  CLAUDE.md) and check it still applies and builds.

## Talking with Erez

Erez's rule for the whole AERA session family:

- No progress or in-between messages. Write to him only when he has to decide something: a short
  context, then the decision.
- Findings and analysis: the information, your assessment, and what he should do. Nothing more.

Messages to the hub session stay complete, with the evidence.
