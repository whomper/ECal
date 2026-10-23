# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ECal - a calendar desk accessory for the Atari ST/STE/TT/Falcon, in C.
#
#   make            builds binary/ECAL.ACC (the desk accessory) and ECAL.PRG
#                   (the same program, to start from the desktop) with a stock
#                   m68k ELF cross compiler (Debian/Ubuntu: apt install
#                   gcc-m68k-linux-gnu). No MiNTLib: the program is freestanding
#                   and common/tools/elf2tos.py writes the TOS header.
#   make test       builds the calendar core for this computer and runs the
#                   unit tests; `make itest` also syncs with a local CalDAV
#                   server (Radicale) directly and through the Pi gateway.
#   make help       binary/ECAL.HYP, the guide for ST-Guide (needs hcp:
#                   common/tools/build-hcp.sh builds it)
#   make dist       a release's files in dist/: each file, and ECAL-x.y.zip
#
# binary/ is what make builds, never committed: releases carry the
# programs (.github/workflows/release.yml, on a tag vX.Y).
#
# The Atari runtime, TLS and Hebrew text come from EMail
# (github.com/whomper/EMAIL), which took them from Claude ST.

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fdisable-tree-bswap -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DCAL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include \
           -Isrc -Icommon/src -Iatari -Icommon/atari -Iui -Icommon/ui -Icommon/third_party/bearssl/inc \
           -DTLS_FULL_EDITION=\"ECAL.ACC\"
# make AESPROF=50: every stretch of 50 ms or more between event calls
# goes to the emulator's console (common/atari/gem.c; make profile)
ifdef STACKPROBE
CFLAGS  += -DSTACK_PROBE
endif
ifdef AESPROF
CFLAGS  += -DAES_PROFILE=$(AESPROF)
endif
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

# BearSSL 0.6 for Falcon mode, built for the 68030 (the rest stays 68000 code)
BSSL    := common/third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
BSSLCF  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF)
BSSLOBJ  = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/bssl/%.o,$(BSSLSRC))
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

CORE    := util charset conn tls bidi secret futil ctime tz ics rrule xml inflate http caldav calstore alarms
ATARI   := libc tos plat_tos net_tos sting gem dsprsa
UI      := main win draw popup dsp views sidebar editor dialogs field about font fontsel help av alert aboutbox iconify clip dd mbar twin busy bgwork accmem dialog av_people people_ask people_file

OBJDIR  := build
# Falcon mode's elliptic curves on the DSP (common/README.md); not in the ST edition
ECDSP   := ecdsp dspec dsp_ec
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/crash.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI) $(ECDSP)))
OBJS    := $(OBJS0) $(OBJDIR)/version.o
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h \
           common/src/*.h common/atari/*.h common/atari/include/*.h common/ui/*.h common/dsp/*.h)

# what goes onto the Atari: binary/ (built, never committed)
BIN     := binary

all: $(BIN)/ECAL.ACC $(BIN)/ECAL.PRG $(BIN)/ECALST.ACC $(BIN)/ROOTS.DAT

# the ST edition: no TLS of its own (it uses the Pi gateway), about 120 KB
# smaller; only BearSSL's MD5 (for Digest logins), as 68000 code. Only
# ever an accessory, so no crash catcher (crash_none.c); never in 256
# colours, so the About box without the 256-colour icon (NO_ICON256)
STBSSL  := hash/md5 codec/dec32le codec/enc32le
STOWN   := about aboutbox
STOBJS  := $(OBJDIR)/crt0_st.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/crash_none.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(filter-out tls,$(CORE)) $(ATARI) \
                                   $(filter-out $(STOWN),$(UI)) tls_none dsp_ec_none)) \
           $(addprefix $(OBJDIR)/st/,$(addsuffix .o,$(notdir $(STBSSL)) $(STOWN))) $(OBJDIR)/version.o

$(BIN)/ECALST.ACC: $(OBJDIR)/ecalst.elf common/tools/elf2tos.py | $(BIN)
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@

$(OBJDIR)/ecalst.elf: $(STOBJS) common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(STOBJS) -lgcc

# as 68000 code: no stores or loads joined into words (an odd address
# would be an address error), as in CFLAGS
STBSSLCF = $(subst -m68030,-m68000 -fno-store-merging -fdisable-tree-bswap,$(BSSLCF))
$(OBJDIR)/st/%.o: $(BSSL)/src/hash/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STBSSLCF) -c -o $@ $<
$(OBJDIR)/st/%.o: $(BSSL)/src/codec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STBSSLCF) -c -o $@ $<
$(OBJDIR)/st/about.o: ui/about.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DNO_ICON256 -c -o $@ $<
$(OBJDIR)/st/aboutbox.o: common/ui/aboutbox.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DNO_ICON256 -c -o $@ $<

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/ECAL.ACC: $(OBJDIR)/cal.elf common/tools/elf2tos.py | $(BIN)
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
$(BIN)/ECAL.PRG: $(BIN)/ECAL.ACC
	cp $< $@

# ECal's icon for the desktop: icons/, ui/icon*.h and docs/icon*.png, from
# its drawing in common/icons/ECAL.txt (the guidelines, 5.5), committed; a
# release carries icons/
icons:
	python3 common/tools/appicon.py ECAL --rsc icons/ECAL.RSC --rsc16 icons/ECAL16.RSC \
		--icn icons/ECAL.ICN --mask icons/ECALMK.ICN \
		--headers ui --docs docs

# a stand-in ST-Guide for tests/hatari/help_test.py
fakeguide: $(OBJDIR)/ST-GUIDE.ACC
$(OBJDIR)/ST-GUIDE.ACC: $(OBJDIR)/fakeguide.elf common/tools/elf2tos.py
	python3 common/tools/elf2tos.py $< $@
$(OBJDIR)/fakeguide.elf: $(OBJDIR)/crt0.o $(OBJDIR)/gem.o $(OBJDIR)/fakeguide.o common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJDIR)/crt0.o $(OBJDIR)/gem.o $(OBJDIR)/fakeguide.o -lgcc
$(OBJDIR)/fakeguide.o: tests/hatari/fakeguide.c common/atari/gem.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/cal.elf: $(OBJS) $(OBJDIR)/libbearssl.a common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(OBJDIR)/libbearssl.a -lgcc

$(OBJDIR)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSSLCF) -c -o $@ $<

$(OBJDIR)/libbearssl.a: $(BSSLOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^

$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/crt0.o: common/atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
# the ST edition's stack: what the rigs measured (tests/hatari, STACKFREE
# with make STACKPROBE=1: 13.8 KB at most, syncing), with room to spare
# for STinG, whose calls run on it; 48 KB left a 1 MB ST's desktop none
ST_STACK ?= 20480
$(OBJDIR)/crt0_st.o: common/atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -DSTACK_SIZE=$(ST_STACK) -c -o $@ $<
$(OBJDIR)/sting_s.o: common/atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR) $(BIN):
	mkdir -p $@

# the version: VERSION holds the last release (major.minor, e.g. 1.97),
# and the build never writes it; every build shows it as it is.
# tools/bump.py starts a release.
VERSION_STR := $(shell cat VERSION)

# written again whenever the string changes
$(OBJDIR)/version.c: FORCE | $(OBJDIR)
	@printf '/* written by the Makefile: do not edit */\nconst char cal_version[] = "%s";\n' "$(VERSION_STR)" > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
FORCE:
$(OBJDIR)/version.o: $(OBJDIR)/version.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- host build: the same calendar core, for tests and cal-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -Icommon/src -fsanitize=address,undefined
HOSTSRC    := $(foreach m,$(CORE),$(firstword $(wildcard src/$(m).c common/src/$(m).c))) tests/host/plat_posix.c tests/host/version_host.c
HOSTLIB    := $(OBJDIR)/libbearssl-host.a

$(OBJDIR)/hbssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -I$(BSSL)/inc -I$(BSSL)/src -c -o $@ $<

$(HOSTLIB): $(HBSSLOBJ)
	rm -f $@
	ar rcs $@ $^

$(OBJDIR)/cal-cli: tests/host/cal_cli.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/host/cal_cli.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/unit.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/mkroots: common/tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ common/tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates: Mozilla's list, kept in common/
$(BIN)/CACERT.PEM: common/certs/CACERT.PEM | $(BIN)
	cp $< $@
# made again, and mkroots with it, whenever common/ changes: the rigs use
# build/mkroots too, and an old one wrote ROOTS.DAT in an old format
$(BIN)/ROOTS.DAT: $(BIN)/CACERT.PEM $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots $(BIN)/CACERT.PEM $@

# the help file for ST-Guide and HypView, from docs/GUIDE.md; needs hcp
# (github.com/th-otto/hypview): make help HCP=/path/to/hcp
HCP     ?= hcp
help: $(BIN)/ECAL.HYP
$(BIN)/ECAL.HYP: docs/GUIDE.md tools/guide2stg.py VERSION docs/front.json docs/help/WINDOW.IMG \
                 common/help/ECAL.IMG common/tools/helpfront.py common/tools/gemimg.py | $(OBJDIR) $(BIN)
	python3 tools/guide2stg.py docs/GUIDE.md $(OBJDIR)/ECAL.STG VERSION
	python3 common/tools/helpfront.py page docs/front.json $(OBJDIR)/ECAL.STG $(OBJDIR)/ECAL.STG
	$(HCP) -o $@ $(OBJDIR)/ECAL.STG

# a release's files: each one in dist/, and all of them in
# dist/ECAL-x.y.zip (the icons in icons/), for the release workflow
DIST    := $(BIN)/ECAL.ACC $(BIN)/ECALST.ACC $(BIN)/ECAL.PRG $(BIN)/CACERT.PEM $(BIN)/ROOTS.DAT \
           $(BIN)/ECAL.HYP $(BIN)/ECAL.REF
ICONS   := icons/ECAL.RSC icons/ECAL16.RSC icons/ECAL.ICN icons/ECALMK.ICN
dist: all help
	rm -rf dist
	mkdir -p dist/zip/icons
	cp $(DIST) $(ICONS) dist/
	cp $(DIST) dist/zip/
	cp $(ICONS) dist/zip/icons/
	cd dist/zip && zip -q -9 -r ../ECAL-$(VERSION_STR).zip .
	rm -rf dist/zip

# ---- fuzzing (the AERA guidelines, 7.1): what servers send, from
# libFuzzer, with AddressSanitizer and UBSan. The network is
# common/tests/fuzz/fuzz_net.c: the server says the fuzz input. Needs
# clang with libFuzzer (Debian/Ubuntu: clang, libclang-rt-dev); make fuzz
# runs each target FUZZTIME seconds from tests/fuzz/seeds (mkseeds.py)
FUZZCC    ?= clang
FUZZTIME  ?= 60
FUZZERS   := dav ics rrule xml inflate
FUZZFLAGS := -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DFUZZ_NET \
             -Isrc -Icommon/src -Icommon/tests/fuzz -I$(BSSL)/inc

$(OBJDIR)/fuzz_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(HOSTLIB) common/tests/fuzz/fuzz_net.c | $(OBJDIR)
	$(FUZZCC) $(FUZZFLAGS) -o $@ $< $(HOSTSRC) common/tests/fuzz/fuzz_net.c $(HOSTLIB)

fuzz: $(addprefix $(OBJDIR)/fuzz_,$(FUZZERS))
	@for f in $(FUZZERS); do \
	  mkdir -p $(OBJDIR)/corpus/$$f; \
	  echo "fuzz_$$f: $(FUZZTIME) s"; \
	  $(OBJDIR)/fuzz_$$f -max_total_time=$(FUZZTIME) -timeout=10 -rss_limit_mb=1024 \
	    -artifact_prefix=$(OBJDIR)/crash-$$f- $(OBJDIR)/corpus/$$f tests/fuzz/seeds/$$f \
	    $$(ls -d tests/fuzz/found/$$f 2>/dev/null) \
	    > $(OBJDIR)/fuzz_$$f.log 2>&1 || { tail -40 $(OBJDIR)/fuzz_$$f.log; exit 1; }; \
	  grep -E "^#[0-9]+.*DONE" $(OBJDIR)/fuzz_$$f.log | tail -1; \
	done

# the same targets without libFuzzer (common/tests/fuzz/replay.c), with
# this computer's compiler: make test runs the seeds again, and the inputs
# that once broke a parser (tests/fuzz/found)
REPLAYFLAGS := $(HOSTCFLAGS) -fno-sanitize-recover=undefined -DFUZZ_NET -Icommon/tests/fuzz -I$(BSSL)/inc
REPLAYSRC   := common/tests/fuzz/fuzz_net.c common/tests/fuzz/replay.c

$(OBJDIR)/replay_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(HOSTLIB) $(REPLAYSRC) | $(OBJDIR)
	$(HOSTCC) $(REPLAYFLAGS) -o $@ $< $(HOSTSRC) $(REPLAYSRC) $(HOSTLIB)

test: $(OBJDIR)/unit $(addprefix $(OBJDIR)/replay_,$(FUZZERS)) $(OBJDIR)/cal-cli
	$(OBJDIR)/unit
	CLI=$(OBJDIR)/cal-cli tests/alarms.sh
	@for f in $(FUZZERS); do \
	  $(OBJDIR)/replay_$$f tests/fuzz/seeds/$$f $$(ls -d tests/fuzz/found/$$f 2>/dev/null) || exit 1; \
	done
	python3 -m unittest discover -s gateway -p 'test_aeragate.py'
	python3 -m unittest discover -s gateway -p 'test_ecalgate.py'

# the programs built to time themselves, in build/prof/bin, for
# tests/hatari/quiet_test.py (binary/ stays as it is)
profile:
	$(MAKE) OBJDIR=build/prof BIN=build/prof/bin AESPROF=50 all

itest: $(OBJDIR)/cal-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN) dist

.PHONY: icons fakeguide all help dist test itest fuzz clean profile FORCE
