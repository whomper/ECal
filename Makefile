# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
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
#
# The Atari runtime, TLS and Hebrew text come from EMail
# (github.com/whomper/EMAIL), which took them from Claude ST.

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DCAL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include \
           -Isrc -Icommon/src -Iatari -Icommon/atari -Iui -Icommon/ui -Icommon/third_party/bearssl/inc \
           -DTLS_FULL_EDITION=\"ECAL.ACC\"
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

CORE    := util charset conn tls bidi secret futil ctime tz ics rrule xml inflate http caldav calstore
ATARI   := libc tos plat_tos net_tos sting gem dsprsa
UI      := main win draw popup dsp views sidebar editor dialogs field about font help alert aboutbox

OBJDIR  := build
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/crash.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
OBJS    := $(OBJS0) $(OBJDIR)/version.o
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h \
           common/src/*.h common/atari/*.h common/atari/include/*.h common/dsp/*.h)

# what goes onto the Atari: binary/
BIN     := binary

all: $(BIN)/ECAL.ACC $(BIN)/ECAL.PRG $(BIN)/ECALST.ACC $(BIN)/ROOTS.DAT

# the ST edition: no TLS of its own (it uses the Pi gateway), 150 KB
# smaller; only BearSSL's MD5 (for Digest logins), as 68000 code. Only
# ever an accessory, so no crash catcher (crash_none.c)
STBSSL  := hash/md5 codec/dec32le codec/enc32le
STOBJS  := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/crash_none.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(filter-out tls,$(CORE)) $(ATARI) $(UI) tls_none)) \
           $(addprefix $(OBJDIR)/st/,$(addsuffix .o,$(notdir $(STBSSL)))) $(OBJDIR)/version.o

$(BIN)/ECALST.ACC: $(OBJDIR)/ecalst.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@

$(OBJDIR)/ecalst.elf: $(STOBJS) common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(STOBJS) -lgcc

$(OBJDIR)/st/%.o: $(BSSL)/src/hash/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<
$(OBJDIR)/st/%.o: $(BSSL)/src/codec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/ECAL.ACC: $(OBJDIR)/cal.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
$(BIN)/ECAL.PRG: $(BIN)/ECAL.ACC
	cp $< $@

# ECal's icon for the desktop: binary/icons/ (ui/icon*.h too)
icons:
	python3 tools/icon/make_rsc.py

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
$(OBJDIR)/sting_s.o: common/atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

# every build counts the version up (VERSION, major.minor as in EMail):
# version.c is written again
# whenever anything in either edition was compiled again
$(OBJDIR)/version.c: $(OBJS0) $(filter-out $(OBJDIR)/version.o,$(STOBJS)) src/version.h tools/bump.py
	python3 tools/bump.py VERSION $@
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
$(BIN)/CACERT.PEM: common/certs/CACERT.PEM
	cp $< $@
$(BIN)/ROOTS.DAT: $(BIN)/CACERT.PEM
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots $(BIN)/CACERT.PEM $@

# the help file for ST-Guide and HypView, from docs/GUIDE.md; needs hcp
# (github.com/th-otto/hypview): make help HCP=/path/to/hcp
HCP     ?= hcp
help: $(BIN)/ECAL.HYP
$(BIN)/ECAL.HYP: docs/GUIDE.md tools/guide2stg.py VERSION | $(OBJDIR)
	python3 tools/guide2stg.py docs/GUIDE.md $(OBJDIR)/ECAL.STG VERSION
	$(HCP) -o $@ $(OBJDIR)/ECAL.STG

test: $(OBJDIR)/unit
	$(OBJDIR)/unit
	python3 -m unittest discover -s gateway -p 'test_*.py'

itest: $(OBJDIR)/cal-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN)/ECAL.ACC $(BIN)/ECAL.PRG $(BIN)/ECALST.ACC

.PHONY: icons fakeguide all help test itest clean
