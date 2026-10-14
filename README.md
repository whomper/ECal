# AERA common code

This is the code the AERA Atari programs share. Each app carries a copy in its own `common/` folder, so
it still clones and builds on its own. The copy is pulled with `git subtree` from this repo's `common`
branch, which holds just this folder and is kept up to date by a workflow.

**Change shared code here, never in an app's `common/`.** Then, in each app:

    tools/sync-common.sh        # git subtree pull --prefix=common <AERA-Atari> common --squash

## What's here (layer 1)

These files were the same code in ECal, EContact, EMail and EFTP when they moved here (October 2026).

| Folder | Files | What they are |
|---|---|---|
| `atari/` | `link.ld`, `nf.S`, `sting.S`, `dsprsa.c/.h` | linker script; NatFeats; STinG's calling glue; RSA on the Falcon's DSP |
| `atari/include/` | `ctype.h` `limits.h` `stdio.h` `string.h` `strings.h` | the parts of the freestanding C library headers every app shares |
| `src/` | `bidi.c/.h`, `ctime.c/.h`, `futil.c`, `secret.c/.h` | Hebrew right-to-left layout; dates and times; safe file writes; password encryption |
| `ui/` | `dsp.c` | checks the DSP and turns Falcon mode on |
| `dsp/` | `rsa.a56`, `rsa_prog.h`, `mkh.py` | the DSP program (source, and as a C array) |
| `tools/` | `elf2tos.py`, `mkroots.c` | ELF to TOS program header; root certificates to `ROOTS.DAT` |
| `third_party/bearssl/` | BearSSL 0.6 | TLS for Falcon mode (MIT licence) |
| `certs/` | `CACERT.PEM` | Mozilla's root certificates (MPL 2.0) |

### What the app provides
The shared files include headers that each app keeps itself, because their contents differ per app.
These are found through the app's `-I` paths:

- `plat.h`: the platform layer (`pf_*`). Used by `futil.c`, `secret.c` and `ui/dsp.c`.
- `util.h`, `conn.h`, `tls.h`: used by `ui/dsp.c` and `secret.c`.
- `ui.h`: the app's GEM layer, used by `ui/dsp.c`.
- the rest of the C library: `stdlib.h` and `tos.h` in `atari/include/`, plus `libc.c`.

### Build flags
Add the shared folders after the app's own, so an app header always wins:

```make
CFLAGS += -Iatari/include -Icommon/atari/include -Isrc -Icommon/src -Iatari -Icommon/atari -Iui
BSSL   := common/third_party/bearssl
LDFLAGS += -Wl,-T,common/atari/link.ld
```

Then add pattern rules for `common/src/%.c`, `common/atari/%.c`, `common/ui/%.c` and the two `.S`
files. `binary/CACERT.PEM` is copied from `common/certs/`. EFTP's Makefile is the reference.

## Coming next (layer 2)

These files exist in every app but have drifted in two directions. They move here once merged, best
copy first:

| Area | Files | Merge |
|---|---|---|
| TLS and connections | `tls.c/.h`, `conn.c/.h`, `tls_none.c` | ECal's session resumption + EMail's ChaCha/AES fallback |
| network | `sting.c/.h`, `net_tos.c` | EFTP's `net_close_wait`, plus a random DNS ID |
| runtime | `tos.c`, `libc.c`, `crt0.S`, `gem.c/.h`, `stdlib.h`, `tos.h` | ECal's crt0 (PRG+ACC) and fast mul/div, EContact's heap sort and scrap calls, EFTP's VDI cache |
| text | `charset.c/.h`, `charset_tab.h`, `util.c/.h`, `tz.c/.h` | |
| GEM building blocks | `popup.c`, `about.c`, `font.c`, alert, help launcher, menu builder, crash handler | the AERA guidelines' reference copies |
