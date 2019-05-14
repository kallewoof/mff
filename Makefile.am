# Copyright (c) 2013-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
AM_LDFLAGS = $(LIBTOOL_LDFLAGS) -pthread
EXTRA_LIBRARIES =

BITCOIN_INCLUDES=-I$(builddir)

LIBBITCOIN=libbitcoin.a
LIBMFF=libmff.a
LIBCQ=libcq.a

EXTRA_LIBRARIES += \
  $(LIBBITCOIN) \
  $(LIBMFF) \
  $(LIBCQ)

# lib_LTLIBRARIES = $(LIBBITCOIN)

bin_PROGRAMS = mff-reader mff-conv aj2bin example test-mff
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

# CQ library #

libcq_a_SOURCES = \
	cq/bitcoin.cpp \
	cq/bitcoin.h \
	cq/cq.cpp \
	cq/cq.h \
	cq/io.cpp \
	cq/io.h
libcq_a_CPPFLAGS = $(AM_CPPFLAGS)
libcq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

# mff-reader binary #
mff_reader_SOURCES = \
	mff-reader.cpp
mff_reader_CPPFLAGS = $(AM_CPPFLAGS)
mff_reader_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_reader_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

mff_reader_LDADD = \
    $(LIBMFF) \
    $(LIBBITCOIN)

# mff-conv binary #
mff_conv_SOURCES = \
    amap.h \
    amap.cpp \
    mff-conv.cpp \
	tinyfs.h \
    tinyblock.h \
    tinymempool.h \
    tinymempool.cpp \
    txmempool_format_rs.cpp \
    txmempool_format_rs.h \
    txmempool_format_aj.cpp \
    txmempool_format_aj.h \
	txmempool_format_ajb.cpp \
    txmempool_format_ajb.h \
    txmempool_format_timetail.h \
    txmempool_format_timetail.cpp \
    txmempool_format_simpletime.h \
    txmempool_format_simpletime.cpp \
	txmempool_format_relseq.h \
    txmempool_format_relseq.cpp
mff_conv_CPPFLAGS = $(AM_CPPFLAGS)
mff_conv_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_conv_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

mff_conv_LDADD = \
    $(LIBMFF) \
    $(LIBBITCOIN)

# aj2bin binary #
aj2bin_SOURCES = \
	aj2bin.cpp
aj2bin_CPPFLAGS = $(AM_CPPFLAGS)
aj2bin_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
aj2bin_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

aj2bin_LDADD = \
    $(LIBMFF) \
    $(LIBBITCOIN)

# example binary #
example_SOURCES = \
	cq/example.cpp
example_CPPFLAGS = $(AM_CPPFLAGS)
example_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
example_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

example_LDADD = \
	$(LIBCQ) \
	$(LIBBITCOIN)

# test-mff binary #
test_mff_SOURCES = \
	test/catch.hpp \
	test/test-io.cpp \
	test/test-cq.cpp \
	test/test-mff.cpp
test_mff_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
test_mff_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
test_mff_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

test_mff_LDADD = \
	$(LIBCQ) \
	$(LIBBITCOIN)

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
