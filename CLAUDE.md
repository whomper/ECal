# Notes for working on ECal

- **Releases carry the programs.** `binary/` and `dist/` are build output,
  never committed. `VERSION` holds the last release and `make` never
  writes it (a build shows `x.y-dev`; `RELEASE=1` shows `x.y`). A
  release: `tools/bump.py`, commit, `git tag -a vX.Y -m "notes"`, push the
  tag; `.github/workflows/release.yml` tests, builds the programs and
  `ECAL.HYP` (hcp from `common/tools/build-hcp.sh`) and publishes each
  file and the zip as a GitHub Release. Never reuse a number. The icons
  are committed in `icons/` (`make icons`).
- Before pushing: `make test` (the core, the fuzz inputs again, the
  gateway), `make itest` (cal-cli against Radicale: `pip install
  radicale`; also `GATEWAY=1` and `FALCON=1` when the connection code
  changes), and `make` with no warnings. When the GEM side changes, the
  Hatari rigs in `tests/hatari/` with EmuTOS.
- `gateway/ecalgate.py` is standard Python; keep it that way.
- EContact's `integration/ECAL.patch` makes EContact ECal's address
  book: when ECal changes, make that patch again in EContact (see its
  CLAUDE.md) and check it still applies and builds.
- `common/` is shared AERA code (BearSSL, the DSP RSA, bidi, dates,
  passwords, the GEM building blocks, libc headers, build tools), a git
  subtree of github.com/whomper/AERA-Atari. Don't edit it here: change it
  in AERA-Atari, then run `tools/sync-common.sh`. Follow the AERA Atari
  app guidelines (AERA-Atari, `standards/ATARI-APP-GUIDELINES.md`).
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright
  lines (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help
  file's `@author`, the docs, and as the author of every commit:
  `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer (AERA-Atari, `standards/GITHUB-IDENTITY.md`).
