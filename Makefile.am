# Copyright (c) 2013-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
AM_LDFLAGS = $(LIBTOOL_LDFLAGS) -pthread
EXTRA_LIBRARIES =

BITCOIN_INCLUDES=-I$(builddir)

LIBBITCOIN=libbitcoin.a
LIBBCQ=libbcq.a

EXTRA_LIBRARIES += \
  $(LIBBITCOIN) \
  $(LIBBCQ)

bin_PROGRAMS = aj2bin mff-parse-ajb mff-findtx
noinst_PROGRAMS = test-mff
lib_LIBRARIES = libbcq.a

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

# CQ library (which uses libbcqdb) #

libbcq_a_SOURCES = \
	bcq/bitcoin.cpp \
	bcq/bitcoin.h
libbcq_a_CPPFLAGS = $(AM_CPPFLAGS)
libbcq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -I/usr/local/include
bcqdbincludedir = $(includedir)/bcq
bcqdbinclude_HEADERS = bcq/bitcoin.h

# aj2bin binary #
aj2bin_SOURCES = \
	aj2bin.cpp
aj2bin_CPPFLAGS = $(AM_CPPFLAGS)
aj2bin_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
aj2bin_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS)

aj2bin_LDADD = \
    $(LIBBITCOIN)

# mff-parse-ajb binary #
mff_parse_ajb_SOURCES = \
	ajb.h \
	ajb.cpp \
    amap.h \
    amap.cpp \
	mff-parse-ajb.cpp \
	bcq/utils.h \
	bcq/utils.cpp \
    tinymempool.h \
    tinymempool.cpp
mff_parse_ajb_CPPFLAGS = $(AM_CPPFLAGS)
mff_parse_ajb_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_parse_ajb_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS) -lcqdb

mff_parse_ajb_LDADD = \
	$(LIBBCQ) \
	$(LIBBITCOIN)

# mff-findtx binary #
mff_findtx_SOURCES = \
	mff-findtx.cpp
mff_findtx_CPPFLAGS = $(AM_CPPFLAGS)
mff_findtx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
mff_findtx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS) -lcqdb

mff_findtx_LDADD = \
	$(LIBBCQ) \
	$(LIBBITCOIN)

# test-mff binary #
test_mff_SOURCES = \
	test/catch.hpp \
	test/helpers.h \
	test/test-cq-bitcoin.cpp \
	test/test-cqb-primitives.cpp \
	test/test-mff.cpp
test_mff_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
test_mff_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
test_mff_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_AP_LDFLAGS) -lcqdb

test_mff_LDADD = \
	$(LIBBCQ) \
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

uninstall-hook:
	-rmdir ${bcqdbincludedir}
