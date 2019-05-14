#ifndef included_txmempool_debugging_h_
#define included_txmempool_debugging_h_

#include <uint256.h>

// #define DEBUG_SEQS
// #define DEBUG_TXID uint256S("33d67e13264f99058f5a0b6b1f602bbed0289afcdde75aecf4e7955735d4f740")
// #define DEBUG_SEQ 10339
// #define DEBUG_SEQ2 5407060
// #define DEBUG_TAG "IN"
// #define DEBUG_TAG2 "OUT"

#ifdef DEBUG_TXID
    static const uint256 debug_txid = DEBUG_TXID;
#   define DTX(txid, fmt...) if (txid == debug_txid) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[TX] " fmt); }
#   define DCOND
#else
#   define DTX(txid, fmt...)
#endif

#ifdef DEBUG_SEQ
#   ifdef DEBUG_TAG
#       define DEBUG_SEQ_COND1(s) (s == DEBUG_SEQ && tag == DEBUG_TAG)
#   else
#       define DEBUG_SEQ_COND1(s) (s == DEBUG_SEQ)
#   endif
#   ifdef DEBUG_SEQ2
#       ifdef DEBUG_TAG2
#           define DEBUG_SEQ_COND2(s) (s == DEBUG_SEQ2 && tag == DEBUG_TAG2)
#       else
#           define DEBUG_SEQ_COND2(s) (s == DEBUG_SEQ2)
#       endif
#       define DEBUG_SEQ_COND(s) (DEBUG_SEQ_COND1(s) || DEBUG_SEQ_COND2(s))
#   else
#       define DEBUG_SEQ_COND(s) DEBUG_SEQ_COND1(s)
#   endif
#   define DSL(s, fmt...) if DEBUG_SEQ_COND(s) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[SEQ:%" PRIseq "] ", s); printf(fmt); }
#else
#   define DSL(s, fmt...)
#endif

#ifdef DEBUG_SEQS
#   define VERIFY_SEQ(txid, seq)    verify_seq(txid, seq)
#else
#   define VERIFY_SEQ(txid, seq)
#endif

#endif // included_txmempool_debugging_h_
