#ifndef included_txmempool_debugging_h_
#define included_txmempool_debugging_h_

#include <uint256.h>

#define DEBUG_TXID uint256S("aa5e48b6372479c1c7e7bf656097c6390395b1c28731b525ce0da7e35034b948")
#define DEBUG_SEQ 515437
// #define DEBUG_SEQ2 5407060
// #define DEBUG_TAG "IN#0"
// #define DEBUG_TAG2 "OUT"

#ifdef DEBUG_TXID
static const uint256 debug_txid = DEBUG_TXID;
#define DTX(txid, fmt...) if (txid == debug_txid) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[TX] " fmt); }
#define DCOND
#else
#define DTX(txid, fmt...)
#endif

#ifdef DEBUG_SEQ
#ifdef DEBUG_TAG
#define DEBUG_SEQ_COND1(s) (s == DEBUG_SEQ && tag == DEBUG_TAG)
#else
#define DEBUG_SEQ_COND1(s) (s == DEBUG_SEQ)
#endif
#ifdef DEBUG_SEQ2
#ifdef DEBUG_TAG2
#define DEBUG_SEQ_COND2(s) (s == DEBUG_SEQ2 && tag == DEBUG_TAG2)
#else
#define DEBUG_SEQ_COND2(s) (s == DEBUG_SEQ2)
#endif
#define DEBUG_SEQ_COND(s) (DEBUG_SEQ_COND1(s) || DEBUG_SEQ_COND2(s))
#else
#define DEBUG_SEQ_COND(s) DEBUG_SEQ_COND1(s)
#endif
#define DSL(s, fmt...) if DEBUG_SEQ_COND(s) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[SEQ:%" PRIseq "] ", s); printf(fmt); }
#else
#define DSL(s, fmt...)
#endif

#endif // included_txmempool_debugging_h_
