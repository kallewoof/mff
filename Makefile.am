# Copyright (c) 2013-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
AM_LDFLAGS = $(LIBTOOL_LDFLAGS)
EXTRA_LIBRARIES =

BITCOIN_INCLUDES=-I$(builddir)

LIBBITCOIN=libbitcoin.a

EXTRA_LIBRARIES += \
  $(LIBBITCOIN)

# lib_LTLIBRARIES = $(LIBBITCOIN)

bin_PROGRAMS = mff-reader
noinst_PROGRAMS =

.PHONY: FORCE check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
	compat/byteswap.h \
	compat/endian.h \
	prevector.h \
	serialize.h \
	streams.h \
	support/allocators/secure.h \
	support/allocators/zeroafterfree.h \
	support/cleanse.h \
	tinyformat.h \
	uint256.h \
	utilstrencodings.h

# bitcoin: shared between all the tools
libbitcoin_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_a_CXXFLAGS = $(AM_CXXFLAGS)
libbitcoin_a_SOURCES = \
	support/cleanse.cpp \
	uint256.cpp \
	utilstrencodings.cpp \
    $(BITCOIN_CORE_H)

# mff binary #
mff_reader_SOURCES = \
	mff.cpp \
	cliargs.h \
	tinytx.h \
	txmempool_format.cpp \
	txmempool_format.h
mff_reader_CPPFLAGS = $(AM_CPPFLAGS)
mff_reader_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_reader_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

mff_reader_LDADD = \
	$(LIBBITCOIN)

clean-local:
	-rm -f config.h $(LIBBITCOIN) $(LIBMFF)

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<

