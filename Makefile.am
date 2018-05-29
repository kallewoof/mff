# Copyright (c) 2013-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
AM_LDFLAGS = $(LIBTOOL_LDFLAGS)
EXTRA_LIBRARIES =

BITCOIN_INCLUDES=-I$(builddir)

LIBBITCOIN=libbitcoin.a
LIBMFF=libmff.a

EXTRA_LIBRARIES += \
  $(LIBBITCOIN) \
  $(LIBMFF)

# lib_LTLIBRARIES = $(LIBBITCOIN)

bin_PROGRAMS = mff-reader mff-conv
noinst_PROGRAMS =

.PHONY: FORCE check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
	compat/byteswap.h \
	compat/endian.h \
	crypto/common.h \
	crypto/sha256.h \
	hash.h \
	prevector.h \
	serialize.h \
	streams.h \
	support/allocators/secure.h \
	support/allocators/zeroafterfree.h \
	support/cleanse.h \
	tinyformat.h \
	uint256.h \
	utilstrencodings.h \
    utiltime.h

# bitcoin: shared between all the tools
libbitcoin_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_a_CXXFLAGS = $(AM_CXXFLAGS)
libbitcoin_a_SOURCES = \
	crypto/sha256.cpp \
	support/cleanse.cpp \
	uint256.cpp \
	utilstrencodings.cpp \
    utiltime.cpp \
    $(BITCOIN_CORE_H)

# mff library #
libmff_a_SOURCES = \
	cliargs.h \
	tinytx.h \
	txmempool_format.cpp \
	txmempool_format.h
libmff_a_CPPFLAGS = $(AM_CPPFLAGS)
libmff_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

# mff-reader binary #
mff_reader_SOURCES = \
	mff-reader.cpp
mff_reader_CPPFLAGS = $(AM_CPPFLAGS)
mff_reader_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_reader_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

mff_reader_LDADD = \
	$(LIBBITCOIN) \
    $(LIBMFF)

# mff-conv binary #
mff_conv_SOURCES = \
    amap.h \
    amap.cpp \
    mff-conv.cpp \
    tinyblock.h \
    tinymempool.h \
    tinymempool.cpp \
    txmempool_format_rs.cpp \
    txmempool_format_rs.h \
    txmempool_format_aj.cpp \
    txmempool_format_aj.h
mff_conv_CPPFLAGS = $(AM_CPPFLAGS)
mff_conv_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_conv_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

mff_conv_LDADD = \
	$(LIBBITCOIN) \
    $(LIBMFF)

clean-local:
	-rm -f config.h $(LIBBITCOIN)

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

