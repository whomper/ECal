# AERA common code

This is the code the AERA Atari programs share. Each app carries a copy in its own `common/` folder, so
it still clones and builds on its own. The copy is pulled with `git subtree` from this repo's `common`
branch, which holds just this folder and is kept up to date by a workflow.

**Change shared code here, never in an app's `common/`.** Then, in each app:

    tools/sync-common.sh        # git subtree pull --prefix=common <AERA-Atari> common --squash

## Who uses it

| App | Since | Check when it moved |
|---|---|---|
| EFTP | 14 Oct 2026 | programs byte-identical before/after; unit, FTP/FTPS and gateway tests |
| EMail | 14 Oct 2026 | programs byte-identical; unit and Dovecot tests (plain, Falcon TLS, stunnel gateway) |
| ECal | 14 Oct 2026 | programs byte-identical; unit, gateway and Radicale tests (same results as before) |
| EContact | 14 Oct 2026 | programs byte-identical; unit, low-memory and Radicale tests |

ClaudeST (`st/`) and EHypView carry older copies of some of these files; they follow once layer 2
is in, since their copies differ more.

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

## Layer 2a: merged copies (14 Oct 2026)

These had drifted between the apps; each is now the most complete copy, with every app's additions:

| Files | Taken from | What the others gain |
|---|---|---|
| `atari/sting.c/.h`, `atari/net_tos.c` | EFTP | `net_close_wait()` (close once everything sent has gone out); **and a random DNS query ID** (was the fixed "TR", which let anyone on the network answer for the name server) |
| `atari/tos.c` | ECal | `boot_drive()`; 32-bit multiply and divide with `mulu.w`/`divu.w` instead of bit loops (checked against the 68020's own instructions on 2 million cases under QEMU) |
| `atari/include/tos.h` | EFTP + ECal | `Dfree`, `Fattrib`, `Fdatime`, `Drvmap`, `boot_drive` |
| `atari/include/stdlib.h` | ECal | `heap_add()` (the accessories' fixed heap) |
| `src/util.c/.h` | ECal | `sb_room()` |
| `src/tz.c/.h` | ECal | a small cache of the last zones looked up |
| `src/charset.c/.h`, `charset_tab.h` | EMail | ECal and EContact lose the Troll-proxy guesswork EMail dropped (it could take real text for already-converted Atari text) |

## Coming next

| Area | Files | Plan |
|---|---|---|
| TLS and connections (2b) | `tls.c/.h`, `tls_none.c`, `conn.c/.h` | EFTP's `tls.c` (ECal's session resumption + EMail's ChaCha20 first); one `conn` API for all (EFTP's buffer size and Stop, EMail's small-Atari buffer, STARTTLS) |
| runtime (2c) | `libc.c`, `crt0.S`, `gem.c/.h` | ECal's crt0 (PRG+ACC), EContact's heap sort and clipboard calls, EFTP's VDI cache |
| GEM building blocks | `popup.c`, `about.c`, `font.c`, alert, help launcher, menu builder, crash handler | the guidelines' reference copies |
