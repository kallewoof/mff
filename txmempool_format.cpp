// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include <tinytx.h>
#include <tinyblock.h>

#include <txmempool_format.h>

static uint32_t frozen_max = 0;

// #define DEBUG_TXID uint256S("7505b0a3fc9ebde0994dcac8d8d1e4c424c0adbda4a0ab4670abfb027fe2189f")
// #define DEBUG_SEQ 1314
// #define l(args...) if (active_chain.height == 521703) { printf(args); }
// #define l1(args...) if (active_chain.height == 521702) { printf(args); }

#ifdef DEBUG_TXID
static const uint256 debug_txid = DEBUG_TXID;
#define DTX(txid, fmt...) if (txid == debug_txid) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[TX] " fmt); }
#define DCOND
#else
#define DTX(txid, fmt...)
#endif

#ifdef DEBUG_SEQ
#define DSL(s, fmt...) if (s == DEBUG_SEQ) { nlprintf("%s%s", tag.c_str(), tag == "" ? "" : "   "); printf("[SEQ] " fmt); }
#else
#define DSL(s, fmt...)
#endif

uint64_t skipped_recs = 0;

namespace mff {

rseq_container* g_rseq_ctr[MAX_RSEQ_CONTAINERS];
size_t initialized_rseq_ctrs = 0;

inline rseq_container* get_rseq_ctr(size_t idx) {
    while (idx >= initialized_rseq_ctrs) {
        g_rseq_ctr[initialized_rseq_ctrs++] = nullptr;
    }
    return g_rseq_ctr[idx];
}

template<typename T>
inline bool find_erase(std::vector<T>& v, const T& e) {
    auto it = std::find(std::begin(v), std::end(v), e);
    if (it == std::end(v)) return false;
    v.erase(it);
    return true;
}

#define showinfo false
// bool showinfo = false;
#define mplinfo_(args...) //if (showinfo) printf(args)
#define mplinfo(args...)  //if (showinfo) printf("[MPL::info] " args)
#define mplwarn(args...) printf("[MPL::warn] " args)
#define mplerr(args...)  fprintf(stderr, "[MPL::err]  " args)

#define _read_time(t) \
    if (timerel < 3) { \
        t = last_time + timerel; if (in.debugging) printf("timerel=%d\n", timerel); \
    } else { \
        uint64_t r; \
        in >> VARINT(r); \
        if (in.debugging) printf("timerel=%d, read varint %llu\n", timerel, r); \
        t = last_time + r; \
    }

#define read_cmd_time(u8, cmd, known, timerel, time) do {\
        in >> u8;\
        cmd = (CMD)(u8 & 0x0f);\
        known = 0 != (u8 & CMD::TX_KNOWN_BIT_V2);\
        timerel = time_rel_value(u8);\
        _read_time(time);\
    } while(0)

#define _write_time(rel) do {\
        if (time_rel_value(rel) > 2) {\
            int64_t time = shared_time ? *shared_time : GetTime();\
            uint64_t tfull = uint64_t(time - last_time);\
            if (in.debugging) printf("timerel=%d, write varint %llu (time=%lld)\n", rel, tfull, time); \
            in << VARINT(tfull);\
            last_time = time;\
        } else {\
            last_time += time_rel_value(rel);\
        }\
        sync();\
    } while (0)

#define start(u8) do {\
        in << u8;\
        _write_time(u8);\
    } while(0)

#define _write_set_time(rel, time) do {\
        if (time_rel_value(rel) > 2) {\
            uint64_t tfull = uint64_t(time - last_time);\
            in << VARINT(tfull);\
            last_time = time;\
        } else {\
            last_time += time_rel_value(rel);\
        }\
        sync();\
    } while (0)

#define write_cmd_set_time(u8, time) do {\
        in << u8;\
        _write_set_time(u8, time);\
    } while(0)

#define read_txseq_keep(known, seq, h) \
    if (known) { \
        seq = seq_read(); \
    } else { \
        in >> h; \
        seq = seqs.count(h) ? seqs[h] : 0; \
    }

#define read_txseq(known, seq) \
    if (known) { \
        seq = seq_read(); \
    } else { \
        uint256 h; \
        in >> h; \
        seq = seqs.count(h) ? seqs[h] : 0; \
    }

#define write_txref(seq, id) \
    if (seq) { \
        seq_write(seq); \
    } else { \
        in << id; \
    }

inline FILE* setup_file(const char* path, bool readonly) {
    FILE* fp = fopen(path, readonly ? "rb" : "rb+");
    if (!readonly && fp == nullptr) {
        fp = fopen(path, "wb");
    }
    if (fp == nullptr) {
        fprintf(stderr, "unable to open %s\n", path);
        assert(fp);
    }
    return fp;
}

template<int I>
mff_rseq<I>::mff_rseq(const std::string path, bool readonly) : mff_rseq(setup_file(path.length() > 0 ? path.c_str() : (std::string(std::getenv("HOME")) + "/mff.out").c_str(), readonly), readonly) {
    mplinfo("start %s\n", path.c_str());
}

template<int I>
mff_rseq<I>::mff_rseq(FILE* fp, bool readonly) : in_fp(fp), in(in_fp, SER_DISK, 0) {
    assert(get_rseq_ctr(I) == nullptr);
    g_rseq_ctr[I] = this;
    entry_counter = 0;
    last_seq = 0;
    nextseq = 1;
    lastflush = GetTime();
    // in.debugme(true);
    char magic[3];
    if (2 == fread(magic, 1, 2, in_fp)) {
        magic[2] = 0;
        if (strcmp(magic, "BM")) {
            fprintf(stderr, "invalid file header - assuming pre-magic version\n");
            fseek(in_fp, 0, SEEK_SET);
        }
    } else if (!readonly) {
        sprintf(magic, "BM");
        fwrite(magic, 1, 2, in_fp);
    }
}

template<int I>
mff_rseq<I>::~mff_rseq() {
    g_rseq_ctr[I] = nullptr;
}

inline bool get_block(uint256 blockhex, tiny::block& b, uint32_t& height) {
    std::string dstfinal = "blockdata/" + blockhex.ToString() + ".mffb";
    FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp) return false;
    fread(&height, sizeof(uint32_t), 1, fp);
    CAutoFile deserializer(fp, SER_DISK, 0);
    deserializer >> b;
    return true;
}

template<int I>
void mff_rseq<I>::apply_block(std::shared_ptr<block> b) {
    if (active_chain.chain.size() > 0 && b->hash == active_chain.chain.back()->hash) {
        // we are already on this block
        return;
    }
    tiny::block bx;
    uint32_t hx;
    if (get_block(b->hash, bx, hx)) {
        assert(hx == b->height);
        std::set<uint256> in_block_txid;
        for (auto& x : bx.vtx) {
            DTX(x.hash, "actually in block %u\n", hx);
            in_block_txid.insert(x.hash);
        }
        for (auto& x : b->known) {
            assert(txs.count(x));
            DSL(x, "supposedly in block %u (known)\n", hx);
            DTX(txs[x]->id, "supposedly in block %u (known)\n", hx);
            auto it = in_block_txid.find(txs[x]->id);
            if (!(it != in_block_txid.end())) {
                printf("%s (seq=%llu) not found in block %u, but listed in b for %u\n", txs[x]->id.ToString().c_str(), x, hx, b->height);
            }
            assert(it != in_block_txid.end());
            in_block_txid.erase(it);
        }
        for (auto& x : b->unknown) {
            DTX(x, "supposedly in block %u (unknown)\n", hx);
            auto it = in_block_txid.find(x);
            in_block_txid.erase(it);
            assert(it != in_block_txid.end());
        }
        if (in_block_txid.size() > 0) {
            nlprintf("missing %zu entries in internal block representation:\n", in_block_txid.size());
            for (auto& x : in_block_txid) printf("- %s\n", x.ToString().c_str());
            assert(!"block missing transactions");
        }
        // printf("block %u ok\n", b->height);
    } // else printf("block %u not checked (block data missing)\n", b->height);
    // printf("appending block %u over block %u = %u\n", b->height, active_chain.height, active_chain.chain.size() == 0 ? 0 : active_chain.chain.back()->height);
    // l1("apply block %u (%s)\n", b->height, b->hash.ToString().c_str());
    if (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
        mplwarn("dealing with BLOCK_UNCONF missing bug 20180502153142\n");
        while (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
            mplinfo("unconfirming block #%u\n", active_chain.height);
            undo_block_at_height(active_chain.height);
        }
    }
    if (active_chain.chain.size() != 0 && b->height != active_chain.height + 1) {
        fprintf(stderr, "*** new block height = %u; active chain height = %u; chain top block height = %u!\n", b->height, active_chain.height, active_chain.chain.back()->height);
    }
    // assert(active_chain.chain.size() == 0 || b->height == active_chain.height + 1);
    active_chain.height = b->height;
    active_chain.chain.push_back(b);
    assert(blocks[b->hash] == b);
    while (active_chain.chain.size() > MAX_BLOCKS) {
        auto rblk = active_chain.chain[0];
        blocks.erase(rblk->hash);
        active_chain.chain.erase(active_chain.chain.begin());
    }
    // l("block for #%u; %zu known, %zu unknown\n", b->height, b->known.size(), b->unknown.size());
    // we need to mark all transactions as confirmed
    for (auto seq : b->known) {
        DSL(seq, "block confirm\n");
        DTX(txs[seq]->id, "block confirm %u (known)\n", b->height);
        assert(txs.count(seq));
        txs[seq]->location = tx::location_confirmed;
        last_seqs.push_back(seq);
        tx_freeze(seq);
        // l("- %s\n", txs[seq]->id.ToString().c_str());
    }
#ifdef DCOND
    for (auto txid : b->unknown) {
        DTX(txid, "block confirm %u (unknown)\n", b->height);
    }
#endif
    update_queues();
}

template<int I>
void mff_rseq<I>::undo_block_at_height(uint32_t height) {
    mplinfo("undo block %u\n", height);
    // we need to unmark confirmed transactions
    assert(height == active_chain.height);
    auto b = active_chain.chain.back();
    active_chain.chain.pop_back();
    active_chain.height--;
    for (auto seq : b->known) {
        DSL(seq, "block orphan\n");
        assert(txs.count(seq));
        DTX(txs[seq]->id, "unconfirm (known) in block reorg at #%u\n", height);
        txs[seq]->location = tx::location_in_mempool;
        tx_thaw(seq);
        last_seqs.push_back(seq);
    }
#ifdef DCOND
    for (auto txid : b->unknown) {
        DTX(txid, "unconfirm (unknown) in block reorg at #%u\n", height);
    }
#endif // DCOND
}

inline std::string bits(uint8_t u) {
    std::string s = "";
    for (uint8_t i = 128; i; i>>=1) {
        s += (u & i) ? "1" : "0";
    }
    return s;
}

// static uint256 foo = uint256S("163ee79aa165df2dbd552d41e89abb266cf76b0763504e6ef582cb6df2e0befc");

template<int I>
seq_t mff_rseq<I>::touched_txid(const uint256& txid, bool count) {
    // static int calls = 0;calls++;
    if (count) {
        std::set<uint256> counted;
        // int i = 0;
        for (seq_t seq : last_seqs) {
            if (txs.count(seq) && counted.find(txs[seq]->id) == counted.end()) {
                txid_hits[txs[seq]->id]++;
                counted.insert(txs[seq]->id);
                // if (foo == txs[seq]->id) printf("%ld CALL %d: %s seq=%llu @%d : %u\n", ftell(in_fp), calls, cmd_str(last_cmd).c_str(), seq, i, txid_hits[txs[seq]->id]);
            }
            // i++;
        }
        if (last_cmd == BLOCK_CONF) {
            // check last block txid list
            std::shared_ptr<block> tip = active_chain.chain.back();
            for (uint256& u : tip->unknown) {
                if (counted.find(u) == counted.end()) {
                    txid_hits[u]++;
                    counted.insert(u);
                }
                // if (foo == u) printf("%s conf height #%u : %u\n", cmd_str(last_cmd).c_str(), active_chain.height, txid_hits[u]);
            }
        }
    }
    for (seq_t seq : last_seqs) {
        if (txs.count(seq) && txs[seq]->id == txid) return seq;
    }
    if (last_cmd == BLOCK_CONF) {
        // l("CONFIRMING BLOCK -- looking for %s\n", txid.ToString().c_str());
        // check last block txid list
        std::shared_ptr<block> tip = active_chain.chain.back();
        for (uint256& u : tip->unknown) {
            // l("- %s\n", u.ToString().c_str());
            if (u == txid) return true;
        }
        // l("DONE CONFIRMING BLOCK\n");
    }
    return 0;
}

template<int I>
uint256 mff_rseq<I>::get_replacement_txid() const {
    return replacement_seq && txs.count(replacement_seq) ? txs.at(replacement_seq)->id : replacement_txid;
}

template<int I>
uint256 mff_rseq<I>::get_invalidated_txid() const {
    return invalidated_seq && txs.count(invalidated_seq) ? txs.at(invalidated_seq)->id : invalidated_txid;
}

template<int I>
int64_t mff_rseq<I>::peek_time() {
    long csr = ftell(in_fp);
    uint8_t u8;
    uint8_t timerel;
    bool known;
    CMD cmd;
    int64_t t;
    try {
        read_cmd_time(u8, cmd, known, timerel, t);
    } catch (std::ios_base::failure& f) {
        return 0;
    }
    fseek(in_fp, csr, SEEK_SET);
    return t;
}

template<int I>
bool mff_rseq<I>::read_entry() {
    entry_counter++;
    uint8_t u8;
    CMD cmd;
    use_start = ftell(in_fp);
    bool known;
    uint8_t timerel;
    try {
        read_cmd_time(u8, cmd, known, timerel, last_time);
    } catch (std::ios_base::failure& f) {
        return false;
    }
    // try {
    last_cmd = cmd;
    last_seqs.clear();
    if (shared_time) *shared_time = last_time;
    switch (cmd) {
        case CMD::TIME_SET:
            // time updated after switch()
            mplinfo("TIME_SET()\n");
            break;

        case TX_REC: {
            mplinfo("TX_REC(): "); fflush(stdout);
            // static std::map<uint64_t,uint256> rec_revmap;
            long pos;
            seq_t seq;
            auto t = std::make_shared<tx>();
            if (known) {
                mplinfo_("known "); fflush(stdout);
                pos = ftell(in_fp); // for offset calculation
                seq = seq_read();
                uint64_t offset;
                in >> VARINT(offset);
                // printf("seek point = pos - offset = %ld - %llu = %llu\n", pos, offset, pos - offset);
                offset = pos - offset;
                pos = ftell(in_fp); // for seek-back post-deserialization
                fseek(in_fp, (long)offset, SEEK_SET);
                // if (rec_revmap.count(offset)) {
                //     printf("offset is for txid %s = %llu\n", rec_revmap[offset].ToString().c_str(), tx_recs[rec_revmap[offset]].seq);
                // } else {
                //     uint64_t closest = 0; --closest;
                //     uint64_t closest_pos = 0;
                //     for (auto& x : rec_revmap) {
                //         uint64_t diff = std::abs((int64_t)x.first - (int64_t)offset);
                //         if (diff < closest) {
                //             closest_pos = x.first;
                //             closest = diff;
                //         }
                //     }
                //     printf("command starting at %ld: offset not found. closest is for txid %s but\n- given pos = %llu\n- actual pos = %llu\n", use_start, rec_revmap[closest_pos].ToString().c_str(), offset, closest_pos);
                // }
            } else {
                mplinfo_("unknown "); fflush(stdout);
            }
            long rec_pos = ftell(in_fp);
            serializer.deserialize_tx(in, *t);
            // fix seq
            if (known) {
                t->seq = last_seq = seq;
            }
            if (seekable && tx_recs.count(t->id) == 0) {
                tx_recs[t->id] = {rec_pos, t->seq};
                // rec_revmap[rec_pos] = t->id;
                // printf("rec_revmap[%ld] = %s\n", pos, t->id.ToString().c_str());
            }
            last_recorded_tx = t;
            if (known) {
                // go back
                fseek(in_fp, pos, SEEK_SET);
            }
            DSL(t->seq, "TX_REC\n");
            if (txs.count(t->seq)) {
                if (t->id != txs[t->seq]->id) {
                    printf("force-thawing %llu as it is being replaced!!!!!!!!\n", t->seq);
                    printf("previous:\n%s\n", txs[t->seq]->to_string().c_str());
                    printf("next:\n%s\n", t->to_string().c_str());
                }
                tx_thaw(t->seq); // this removes the tx from chill/freeze lists
                seqs.erase(txs[t->seq]->id); // this unlinks the txid-seq rel
            }
            txs[t->seq] = t;
            seqs[t->id] = t->seq;

            if (!known) {
                if (known_txid.count(t->id)) {
                    rerecs++;
                    bool frozen = known_txid[t->id] < 0;
                    uint32_t discarded_block = (uint32_t)((frozen ? -1 : 1) * known_txid[t->id]);
                    uint32_t blocks_ago = active_chain.height - discarded_block;
                    discarded_x_blocks_ago[frozen].insert(blocks_ago);
                    uint32_t p[3];
                    auto it = discarded_x_blocks_ago[frozen].begin();
                    size_t steps = discarded_x_blocks_ago[frozen].size() / 4;
                    for (int j = 0; j < 3; j++) {
                        for (int i = 0; i < steps; i++) ++it;
                        p[j] = *it;
                    }
                    // nlprintf("*** [READ] REREC %llu (discarded (%s) %u blocks ago; +%u for 25%%, +%u for 50%%, +%u for 75%%) ***\n", rerecs, frozen ? "frozen" : "chilled", blocks_ago, p[0], p[1], p[2]);
                    if (frozen && frozen_max < blocks_ago) {
                        frozen_max = blocks_ago;
                        printf("****** NEW FROZEN MAX FOR TXID=%s ******\n", t->id.ToString().c_str());
                    }
                } else known_txid[t->id] = active_chain.height;
            }

            nextseq = std::max(nextseq, t->seq + 1);
            last_seqs.push_back(t->seq);
            for (const auto& prevout : t->vin) {
                if (prevout.is_known()) {
                    last_seqs.push_back(prevout.get_seq());
                }
            }
            mplinfo_("id=%s, seq=%llu\n", t->id.ToString().c_str(), t->seq);
            t->location = tx::location_in_mempool;
            if (listener) listener->tx_rec(this, *t);
            break;
        }

        case TX_IN: {
            mplinfo("TX_IN(): "); fflush(stdout);
            uint64_t seq = seq_read();
            DSL(seq, "TX_IN\n");
            if (!txs.count(seq)) {
                fprintf(stderr, "*** missing seq=%llu in txs\n", seq);
            }
            assert(txs.count(seq));
            last_recorded_tx = txs[seq];
            if (txs.count(seq)) {
                txs[seq]->location = tx::location_in_mempool;
                tx_thaw(seq);
            }
            mplinfo_("seq=%llu\n", seq);
            last_seqs.push_back(seq);
            if (listener) listener->tx_in(this, *txs[seq]);
            break;
        }

        case BLOCK_CONF: {
            // l1("BLOCK_CONF(%s): ", known ? "known" : "unknown");
            if (known) {
                // we know the block; just get the header info and find it, then apply
                block b(known);
                serializer.deserialize_block(in, b);
                assert(blocks.count(b.hash));
                // l1("block %u=%s\n", b.height, b.hash.ToString().c_str());
                apply_block(blocks[b.hash]);
                if (listener) listener->block_confirm(this, b);
            } else {
                auto b = std::make_shared<block>();
                serializer.deserialize_block(in, *b);
                blocks[b->hash] = b;
                // l1("block %u=%s\n", b->height, b->hash.ToString().c_str());
                apply_block(b);
                if (listener) listener->block_confirm(this, *b);
            }
            break;
        }

        case TX_OUT: {
            mplinfo("TX_OUT(): "); fflush(stdout);
            seq_t seq = 0;
            uint8_t reason;
            uint256 txid;
            read_txseq_keep(known, seq, txid);
            in >> reason;
            if (!known && seq == 0 && !txid.IsNull()) {
                // we don't care about these
                break;
            }
            if (!txs.count(seq)) {
                fprintf(stderr, "*** TX_OUT with seq=%llu (txid=%s), but txs[%llu] not found! ***   \n", seq, txid.ToString().c_str(), seq);
            }
            assert(txs.count(seq));
            last_seqs.push_back(seq);
            DSL(seq, "TX_OUT\n");
            last_out_reason = reason;
            auto t = txs[seq];
            t->location = tx::location_discarded;
            t->out_reason = (tx::out_reason_enum)reason;
            tx_chill(seq);
            mplinfo_("seq=%llu, reason=%s\n", seq, tx_out_reason_str(reason));
            if (listener) listener->tx_out(this, *txs[seq], t->out_reason);
            break;
        }

        case TX_INVALID: {
            // in.debugme(true);
            mplinfo("TX_INVALID(): "); fflush(stdout);
            replacement_seq = 0;
            replacement_txid.SetNull();
            mplinfo_("\n----- invalid deserialization begins -----\n");
            seq_t tx_invalid(0);
            uint8_t state;
            seq_t tx_cause(0);
            mplinfo_("--- read_txseq(%d, tx_invalid)\n", known);
            read_txseq_keep(known, tx_invalid, invalidated_txid);
            mplinfo_("--- tx_invalid = %llu\n", tx_invalid);
            assert(tx_invalid);
            invalidated_seq = tx_invalid;
            last_seqs.push_back(tx_invalid);
            mplinfo_("--- state\n");
            in >> state;
            bool cause_known = 0 != (state & CMD::TX_KNOWN_BIT_V2);
            state &= 0x1f;
            last_invalid_state = state;
            mplinfo_("--- state = %d (cause_known = %d)\n", state, cause_known);
            if (state != tx::invalid_unknown && state != tx::invalid_reorg) {
                read_txseq_keep(cause_known, tx_cause, replacement_txid);
                mplinfo_("--- read_txseq(%d, tx_cause) = %llu\n", cause_known, tx_cause);
                last_seqs.push_back(tx_cause);
                replacement_seq = tx_cause;
            }
            mplinfo_("----- tx deserialization begins -----\n");
            long txhex_start = ftell(in_fp);
            in >> last_invalidated_tx;
            long txhex_end = ftell(in_fp);
            fseek(in_fp, txhex_start, SEEK_SET);
            last_invalidated_txhex.resize(txhex_end - txhex_start);
            fread(last_invalidated_txhex.data(), 1, txhex_end - txhex_start, in_fp);
            assert(ftell(in_fp) == txhex_end);

            if (tx_invalid) {
                auto t = txs[tx_invalid];
                t->location = tx::location_invalid;
                t->invalid_reason = (tx::invalid_reason_enum)state;
                if (txs.count(tx_invalid)) {
                    if (last_invalidated_tx.hash != txs[tx_invalid]->id) {
                        printf("tx hash failure:\n\t%s\nvs\t%s\n", last_invalidated_tx.hash.ToString().c_str(), txs[tx_invalid]->id.ToString().c_str());
                        printf("tx hex = %s\n", HexStr(last_invalidated_txhex).c_str());
                    }
                    assert(last_invalidated_tx.hash == txs[tx_invalid]->id);
                }
            }
            mplinfo_("seq=%llu, state=%s, cause=%llu, tx_data=%s\n", tx_invalid, tx_invalid_state_str(state), tx_cause, last_invalidated_tx.ToString().c_str());
            mplinfo_("----- tx invalid deserialization ends -----\n");
            if (listener) listener->tx_invalid(this, *txs[tx_invalid], last_invalidated_txhex, txs[tx_invalid]->invalid_reason, replacement_seq ? &txs[replacement_seq]->id : replacement_txid.IsNull() ? nullptr : &replacement_txid);
            // in.debugme(false);
            break;
        }

        case BLOCK_UNCONF: {
            mplinfo("BLOCK_UNCONF(): "); fflush(stdout);
            uint32_t height;
            in >> height;
            if (known) {
                mplinfo_("known, height=%u\n", height);
                undo_block_at_height(height);
                if (listener) listener->block_unconfirm(this, height);
            } else {
                if (listener) assert(!"not implemented");
                mplinfo_("unknown, height=%u\n", height);
                uint64_t count = ReadCompactSize(in);
                mplinfo("%llu transactions\n", count);
                for (uint64_t i = 0; i < count; ++i) {
                    uint64_t seq = seq_read();
                    mplinfo("%llu: seq = %llu\n", i, seq);
                    if (seq) {
                        assert(txs.count(seq));
                        last_seqs.push_back(seq);
                        txs[seq]->location = tx::location_in_mempool;
                    }
                }
            }
            break;
        }

        default:
            mplerr("u8 = %u (%02x), cmd = %u @ %ld\n", u8, u8, cmd, ftell(in_fp));
            assert(!"unknown command"); // todo: exceptionize
    }
    // showinfo = false;
    // } catch (std::ios_base::failure& f) {
    //     return nullptr;
    // }
    if (!mff_piping) {
        long used_bytes = ftell(in_fp) - use_start;
        assert(used_bytes > 0);
        used(cmd, (uint64_t)used_bytes);
    }
    return true;
}

template<int I>
inline seq_t mff_rseq<I>::claim_seq(const uint256& txid) {
    if (seqs.count(txid)) return seqs[txid];
    return nextseq++;
}

template<int I>
inline seq_t mff_rseq<I>::seq_read() {
    int64_t rseq;
    in >> CVarInt<VarIntMode::SIGNED, int64_t>{rseq};
    last_seq += rseq;
    return last_seq;
}

template<int I>
inline void mff_rseq<I>::seq_write(seq_t seq) {
    int64_t rseq = (seq > last_seq ? seq - last_seq : -int64_t(last_seq - seq));
    in << CVarInt<VarIntMode::SIGNED, int64_t>(rseq);
    last_seq = seq;
}

template<int I>
inline void mff_rseq<I>::sync() {
    int64_t now = GetTime();
    if (lastflush + 10 < now) {
        // printf("*\b"); fflush(stdout);
        fflush(in_fp);
        lastflush = now;
    }
}

template<int I>
const std::shared_ptr<tx> mff_rseq<I>::register_entry(const tiny::mempool_entry& entry, bool known) {
    const tiny::tx& tref = *entry.x;
    auto t = std::make_shared<tx>();
    t->id = tref.hash;
    t->seq = known ? tx_recs[tref.hash].seq : nextseq++;
    t->weight = tref.GetWeight();
    t->fee = entry.fee();
    t->inputs = tref.vin.size();
    t->state.resize(t->inputs);
    t->vin.resize(t->inputs);
    for (uint64_t i = 0; i < t->inputs; ++i) {
        const auto& prevout = tref.vin[i].prevout;
        bool known = seqs.count(prevout.hash);
        seq_t seq = known ? seqs[prevout.hash] : 0;
        assert(!known || seq != 0);
        bool confirmed = known && txs[seq]->location == tx::location_confirmed;
        t->state[i] = confirmed ? outpoint::state_confirmed : known ? outpoint::state_known : outpoint::state_unknown;
        t->vin[i] = known ? outpoint(prevout.n, seq) : outpoint(prevout.n, prevout.hash);
        // printf("- vin %llu = %s n=%d, seq=%llu, hash=%s :: %s\n", i, known ? "known" : "unknown", prevout.n, seq, prevout.hash.ToString().c_str(), t->vin[i].to_string().c_str());
    }

    txs[t->seq] = t;
    seqs[t->id] = t->seq;

    if (!known) {
        if (known_txid.count(t->id)) {
            rerecs++;
            bool frozen = known_txid[t->id] < 0;
            uint32_t discarded_block = (uint32_t)((frozen ? -1 : 1) * known_txid[t->id]);
            uint32_t blocks_ago = active_chain.height - discarded_block;
            discarded_x_blocks_ago[frozen].insert(blocks_ago);
            uint32_t p[3];
            auto it = discarded_x_blocks_ago[frozen].begin();
            size_t steps = discarded_x_blocks_ago[frozen].size() / 4;
            for (int j = 0; j < 3; j++) {
                for (int i = 0; i < steps; i++) ++it;
                p[j] = *it;
            }
            nlprintf("*** [REG] REREC %llu (discarded (%s) %u blocks ago; +%u for 25%%, +%u for 50%%, +%u for 75%%) ***\n", rerecs, frozen ? "frozen" : "chilled", blocks_ago, p[0], p[1], p[2]);
            if (frozen && frozen_max < blocks_ago) {
                frozen_max = blocks_ago;
                printf("****** NEW FROZEN MAX FOR TXID=%s ******\n", t->id.ToString().c_str());
            }
        } else known_txid[t->id] = active_chain.height;
    }

    return t;
}

template<int I>
void mff_rseq<I>::add_entry(std::shared_ptr<const tiny::mempool_entry>& entry) {
    uint8_t b;
    // do we know this transaction?
    const auto& tref = entry->x;
    // mplinfo("insert %s %s\n", seqs.count(tref.GetHash()) ? "known" : "new", tref.GetHash().ToString().c_str());
    if (seqs.count(tref->hash)) {
        // we do: TX_IN
        DSL(seqs[tref->hash], "add_entry() known\n");
        DTX(tref->hash, "add_entry() known\n");
        b = prot_v2(CMD::TX_IN, true);
        start(b);
        seq_write(seqs[tref->hash]);
    } else {
        // we don't: TX_REC
        bool known = seekable && tx_recs.count(tref->hash);
        DTX(tref->hash, "add_entry() %s\n", known ? "offset-known" : "unknown");
        auto t = register_entry(*entry, known);
        DSL(t->seq, "add_entry() unknown\n");
        // bool debugme = known || ftell(in_fp) == 62344248;
        #define oij(args...) //if (debugme) printf(args)
        oij("serializing %s tx %s (%llu) at pos %ld\n", known ? "offset-known" : "unknown", tref->hash.ToString().c_str(), t->seq, ftell(in_fp));
        b = prot_v2(CMD::TX_REC, known);
        start(b);
        long pos = ftell(in_fp);
        oij("pos = %ld\n", pos);
        if (known) {
            seq_write(t->seq);
            uint64_t offset = pos - tx_recs[tref->hash].pos;
            oij("known. offset = %ld - %ld = %llu\n", pos, tx_recs[tref->hash].pos, offset);
            in << VARINT(offset);
        } else {
            oij("unknown. serializing tx\n");
            serializer.serialize_tx(in, *t);
            if (seekable) {
                oij("tx_recs[%s] = {%ld, %llu}\n", tref->hash.ToString().c_str(), pos, t->seq);
                tx_recs[tref->hash] = {pos, t->seq};
                // printf("%s @ %ld\n", tref->hash.ToString().c_str(), pos);
            }
        }
    }
}

template<int I>
inline void mff_rseq<I>::tx_out(bool known, seq_t seq, std::shared_ptr<tx> t, const uint256& txid, uint8_t reason) {
    mplinfo("TX_OUT %s %llu %s [%s]\n", known ? "known" : "new", seq, t->id.ToString().c_str(), tx_out_reason_str(reason));
    if (!known) {
        // an unknown transaction is being discarded, which tells us basically nothing so we skip it
        return;
    }
    uint8_t b = prot_v2(CMD::TX_OUT, known);
    if (known) {
        t->location = tx::location_discarded;
        t->out_reason = (tx::out_reason_enum)reason;
        tx_chill(seq);
    }
    start(b);
    write_txref(seq, txid);
    in << reason;
}

template<int I>
inline void mff_rseq<I>::tx_invalid(bool known, seq_t seq, std::shared_ptr<tx> t, const tiny::tx& tref, uint8_t state, const uint256* cause) {
    // in.debugme(22450 == seq);
    mplinfo("TX_INVALID %s %llu %s [%s]\n", known ? "known" : "new", seq, t ? t->id.ToString().c_str() : "???", tx_invalid_state_str(state));
    uint8_t b = prot_v2(CMD::TX_INVALID, known);
    if (known) {
        t->location = tx::location_invalid;
        t->invalid_reason = (tx::invalid_reason_enum)state;
        tx_freeze(seq);
    }
    start(b);
    mplinfo_("\n----- invalid serialization begins -----\n");
    mplinfo_("--- write_txseq(%llu, %s)\n", seq, tref.hash.ToString().c_str());
    write_txref(seq, tref.hash);
    uint8_t v = state | (cause && seqs.count(*cause) ? CMD::TX_KNOWN_BIT_V2 : 0);
    in << v;
    if (state != tx::invalid_unknown && state != tx::invalid_reorg) {
        assert(cause);
        seq_t causeseq = seqs.count(*cause) ? seqs[*cause] : 0;
        write_txref(causeseq, *cause);
    }
    in << tref;
    // in.debugme(false);
}

template<int I>
void mff_rseq<I>::remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause_tx) {
    uint256* cause = cause_tx ? &cause_tx->hash : nullptr;
    // do we know this transaction?
    const auto& tref = *entry->x;
    bool known = seqs.count(tref.hash);
    seq_t seq = known ? seqs[tref.hash] : 0;
    DSL(seq, "remove_entry()\n");
    DTX(tref.hash, "remove_entry()\n");
    auto t = known ? txs[seq] : nullptr;
    switch (reason) {
    case tiny::MemPoolRemovalReason::EXPIRY:    //! Expired from mempool
        // TX_OUT(REASON=1)
        return tx_out(known, seq, t, tref.hash, tx::out_reason_age_expiry);
    case tiny::MemPoolRemovalReason::SIZELIMIT: //! Removed in size limiting
        // TX_OUT(REASON=0)
        return tx_out(known, seq, t, tref.hash, tx::out_reason_low_fee);
    case tiny::MemPoolRemovalReason::REORG:     //! Removed for reorganization
        // TX_INVALID(STATE=2)
        return tx_invalid(known, seq, t, tref, tx::invalid_reorg, cause);
    case tiny::MemPoolRemovalReason::BLOCK:     //! Removed for block
        // BLOCK_CONF
        if (known) {
            DSL(seq, "confirmed in block (known)\n");
            DTX(tref.hash, "confirmed in block (known)\n");
            pending_conf_known.push_back(seq);
            t->location = tx::location_confirmed;
            tx_freeze(seq);
        } else {
            DSL(seq, "confirmed in block (unknown)\n");
            DTX(tref.hash, "confirmed in block (unknown)\n");
            pending_conf_unknown.push_back(tref.hash);
        }
        return;
    case tiny::MemPoolRemovalReason::CONFLICT:  //! Removed for conflict with in-block transaction
        // TX_INVALID(STATE=1)
        return tx_invalid(known, seq, t, tref, tx::invalid_doublespent, cause);
    case tiny::MemPoolRemovalReason::REPLACED:  //! Removed for replacement
        // TX_INVALID(STATE=0)
        return tx_invalid(known, seq, t, tref, tx::invalid_rbf_bumped, cause);
    case tiny::MemPoolRemovalReason::UNKNOWN:   //! Manually removed or unknown reason
    default:
        // TX_OUT(REASON=2) -or- TX_INVALID(REASON=3)
        // If there is a cause, we use invalid, otherwise out
        if (cause) {
            return tx_invalid(known, seq, t, tref, tx::invalid_unknown, cause);
        }
        return tx_out(known, seq, t, tref.hash, tx::out_reason_unknown);
    }
}

template<int I>
void mff_rseq<I>::push_block(int height, uint256 hash, const std::vector<tiny::tx>& vtx) {
    mplinfo("confirm block #%d (%s)\n", height, hash.ToString().c_str());
    if (height == active_chain.height && active_chain.chain.size() > 0 && active_chain.chain.back()->hash == hash) {
        printf("note: skipping push_block() call, as we are already on the block in question\n");
        return;
    }
    uint8_t b;
    std::shared_ptr<block> blk;
    while (active_chain.chain.size() > 0 && height < active_chain.height + 1) {
        mplinfo("unconfirming block #%u\n", active_chain.height);
        b = prot_v2(CMD::BLOCK_UNCONF, true);
        start(b);
        in << active_chain.height;
        undo_block_at_height(active_chain.height);
    }
    if (!(active_chain.chain.size() == 0 || height == active_chain.height + 1)) {
        fprintf(stderr, "*** gap in chain (active_chain.height = %u; pushed block height = %u; missing %u block(s))\n", active_chain.height, height, height - active_chain.height);
        b = prot_v2(CMD::GAP, false);
        start(b);
        uint32_t current_height = height - 1;
        in << current_height;
    }
    // assert(active_chain.chain.size() == 0 || height == active_chain.height + 1);
    if (blocks.count(hash)) {
        // known block
        blk = blocks[hash];
        blk->is_known = true;
        b = prot_v2(CMD::BLOCK_CONF, true);
    } else {
        // unknown block
        blk = std::make_shared<block>(height, hash);
        blk->count_known = pending_conf_known.size();
        blk->known = pending_conf_known;
        blk->unknown = pending_conf_unknown;
        b = prot_v2(CMD::BLOCK_CONF, false);
        blocks[hash] = blk;
    }
    assert(blk->height == height);
    if (vtx.size()) {
        std::set<uint256> in_block_txid;
        for (auto& x : vtx) {
            DTX(x.hash, "actually in block %u\n", height);
            in_block_txid.insert(x.hash);
        }
        for (auto& x : blk->known) {
            assert(txs.count(x));
            DSL(x, "supposedly in block %u (known)\n", height);
            DTX(txs[x]->id, "supposedly in block %u (known)\n", height);
            auto it = in_block_txid.find(txs[x]->id);
            if (!(it != in_block_txid.end())) {
                printf("%s (seq=%llu) not found in block %u, but listed in b for %u\n", txs[x]->id.ToString().c_str(), x, height, blk->height);
            }
            assert(it != in_block_txid.end());
            in_block_txid.erase(it);
        }
        for (auto& x : blk->unknown) {
            DTX(x, "supposedly in block %u (unknown)\n", height);
            auto it = in_block_txid.find(x);
            assert(it != in_block_txid.end());
            in_block_txid.erase(it);
        }
        if (in_block_txid.size() > 0) {
            nlprintf("missing %zu entries in internal block representation:\n", in_block_txid.size());
            for (auto& x : in_block_txid) printf("- %s\n", x.ToString().c_str());
            assert(!"block missing transactions");
        }
    }
    // printf("block %u ok\n", blk->height);
    apply_block(blk);
    // active_chain.height = height;
    // active_chain.chain.push_back(blk);
    start(b);
    serializer.serialize_block(in, *blk);
    pending_conf_known.clear();
    pending_conf_unknown.clear();
    // update_queues();
}

template<int I>
void mff_rseq<I>::pop_block(int height) {
    undo_block_at_height(height);
}

// listener callback

template<int I>
inline void mff_rseq<I>::tx_rec(seqdict_server* source, const tx& x) {
    long y = showinfo ? ftell(in_fp) : 0;
    mplinfo("%ld: tx_rec(txid=%s)", y, x.id.ToString().c_str());
    if (seqs.count(x.id)) {
        // we have this already, so no need to import anything; we do an 'in' though
        mplinfo_(": got it already, inserting\n");
        return tx_in(source, x);
    }

    // we need to create a new transaction based on x
    auto t = import_tx(source, x);
    if (!t.get()) {
        mplinfo_(": failure to import; ignoring\n");
        return;
    }
    mplinfo_(": seq=%llu\n", t->seq);
    DTX(x.id, "TX_REC with seq=%llu\n", t->seq);
    bool known = seekable && tx_recs.count(x.id);
    uint8_t b = prot_v2(CMD::TX_REC, known);
    start(b);
    long pos = ftell(in_fp);
    if (known) {
        assert(pos > tx_recs[x.id].pos);
        uint64_t offset = pos - tx_recs[x.id].pos;
        seq_write(t->seq);
        in << VARINT(offset);
        printf("%ld: known tx %s=%llu at pos %ld - %ld = %llu\n", pos, t->id.ToString().c_str(), t->seq, pos, tx_recs[x.id].pos, offset);
    } else {
        serializer.serialize_tx(in, *t);
        if (seekable) tx_recs[x.id] = {pos, t->seq};
    }
}

template<int I>
inline void mff_rseq<I>::tx_in(seqdict_server* source, const tx& x) {
    DTX(x.id, "TX_IN\n");
    long y = ftell(in_fp);
    // showinfo = y > 144275074 && y < 144275674;
    mplinfo("%ld: tx_in(txid=%s)", y, x.id.ToString().c_str());
    if (!seqs.count(x.id)) {
        mplinfo_(": unknown to us, recording\n");
        fprintf(stderr, "we do not allow inconsistencies between known and unknown between MFF-based conversions:\n"
            "LOCAL  seq=0\n"
            "SOURCE seq=%llu txid=%s\n", source->seqs[x.id], x.id.ToString().c_str()
        );
        assert(!"invalid source or buggy destination");
        // we do NOT know about this one. let's record it
        // printf("note: unknown transaction %s from source input, recording\n", x.id.ToString().c_str());
        return tx_rec(source, x);
    }

    auto t = txs[seqs[x.id]];
    mplinfo_(": seq=%llu; location=%u (->%u)\n", t->seq, t->location, tx::location_in_mempool);
    if (t->location != tx::location_in_mempool) {
        t->location = tx::location_in_mempool;
        tx_thaw(seqs[x.id]);
        uint8_t b = prot_v2(CMD::TX_IN, true);
        start(b);
        seq_write(seqs[x.id]);
    }
}

template<int I>
inline void mff_rseq<I>::tx_out(seqdict_server* source, const tx& x, tx::out_reason_enum reason) {
    DTX(x.id, "TX_OUT\n");
    long y = showinfo ? ftell(in_fp) : 0;
    mplinfo("%ld: tx_out(txid=%s, reason=%u)", y, x.id.ToString().c_str(), reason);
    if (!seqs.count(x.id)) {
        // we don't even konw about this transaction and it's being thrown out of the mempool so
        // we just ignore it
        mplinfo_(": unknown, ignoring\n");
        return;
    }

    mplinfo_(": seq=%llu\n", seqs[x.id]);
    tx_out(true, seqs[x.id], txs[seqs[x.id]], x.id, (uint8_t)reason);
    // uint8_t b = prot_v2(CMD::TX_OUT, true);
    // auto t = txs[seqs[x.id]];
    // t->location = tx::location_discarded;
    // t->out_reason = reason;
    // tx_chill(seqs[x.id]);
    // start(b);
    // write_txref(t->seq, t->id);
    // b = reason; // force uint8
    // in << b;
}

template<int I>
inline void mff_rseq<I>::tx_invalid(seqdict_server* source, const tx& x, std::vector<uint8_t> txdata, tx::invalid_reason_enum reason, const uint256* cause) {
    DTX(x.id, "TX_INVALID\n");
    bool known = seqs.count(x.id);
    seq_t seq = known ? seqs[x.id] : 0;
    auto t = known ? txs[seq] : nullptr;

    long y = showinfo ? ftell(in_fp) : 0;
    mplinfo("%ld: TX_INVALID %s %llu %s [%s]\n", y, known ? "known" : "new", seq, t ? t->id.ToString().c_str() : "???", tx_invalid_state_str(reason));

    uint8_t b = prot_v2(CMD::TX_INVALID, known);
    if (known) {
        t->location = tx::location_invalid;
        t->invalid_reason = reason;
        tx_freeze(seq);
    }
    start(b);
    write_txref(seq, x.id);
    uint8_t state = (uint8_t)reason;
    uint8_t v = state | (cause && seqs.count(*cause) ? CMD::TX_KNOWN_BIT_V2 : 0);
    in << v;
    if (state != tx::invalid_unknown && state != tx::invalid_reorg) {
        assert(cause);
        seq_t causeseq = seqs.count(*cause) ? seqs[*cause] : 0;
        write_txref(causeseq, *cause);
    }
    fwrite(txdata.data(), 1, txdata.size(), in_fp);
}

template<int I>
inline void mff_rseq<I>::block_confirm(seqdict_server* source, const block& b) {
    // showinfo = b.height == 506205;
    long y = showinfo ? ftell(in_fp) : 0;
    mplinfo("%ld: block_confirm(height=%u, hash=%s)\n", y, b.height, b.hash.ToString().c_str());
    for (seq_t s : b.known) {
        auto txid = source->txs[s]->id;
        DTX(txid, "confirm (known) in block #%u\n", b.height);
        if (seqs.count(txid)) {
            auto t = txs[seqs[txid]];
            t->location = tx::location_confirmed;
            tx_freeze(t->seq);
            pending_conf_known.push_back(seqs[txid]);
        } else {
            pending_conf_unknown.push_back(txid);
        }
    }
    for (auto& txid : b.unknown) {
        DTX(txid, "confirm (unknown) in block #%u\n", b.height);
        if (seqs.count(txid)) {
            auto t = txs[seqs[txid]];
            t->location = tx::location_confirmed;
            tx_freeze(t->seq);
            pending_conf_known.push_back(seqs[txid]);
        } else {
            pending_conf_unknown.push_back(txid);
        }
    }
    push_block(b.height, b.hash, std::vector<tiny::tx>());
}

template<int I>
inline void mff_rseq<I>::block_unconfirm(seqdict_server* source, uint32_t height) {
    long y = showinfo ? ftell(in_fp) : 0;
    mplinfo("%ld: block_unconfirm(height=%u)", y, height);
    undo_block_at_height(height);
}

// end of listener callback code

template<int I>
inline void mff_rseq<I>::tx_freeze(seq_t seq) {
    assert(seq > 0);
    // make seq eventually available
    if (txs.count(seq)) {
        if (txs[seq]->cool_height) tx_thaw(seq);
        assert(!txs[seq]->cool_height);
        txs[seq]->cool_height = active_chain.height;
        DSL(seq, "tx_freeze @ %u\n", active_chain.height);
        DTX(txs[seq]->id, "tx_freeze @ %u\n", active_chain.height);
    }
    frozen_queue[active_chain.height].push_back(seq);
}

template<int I>
inline void mff_rseq<I>::tx_chill(seq_t seq) {
    assert(seq > 0);
    // make seq eventually available (sometime in the future)
    if (txs.count(seq)) {
        if (txs[seq]->cool_height) return;
        assert(!txs[seq]->cool_height);
        txs[seq]->cool_height = active_chain.height;
        DSL(seq, "tx_chill @ %u\n", active_chain.height);
        DTX(txs[seq]->id, "tx_chill @ %u\n", active_chain.height);
    }
    chilled_queue[active_chain.height].push_back(seq);
}

template<int I>
inline void mff_rseq<I>::tx_thaw(seq_t seq) {
    assert(seq > 0);
    if (txs.count(seq) && txs[seq]->cool_height) {
        DTX(txs[seq]->id, "tx_thaw @ %u\n", txs[seq]->cool_height);
        DSL(seq, "tx_thaw @ %u\n", txs[seq]->cool_height);
        find_erase(frozen_queue[txs[seq]->cool_height], seq);
        find_erase(chilled_queue[txs[seq]->cool_height], seq);
        txs[seq]->cool_height = 0;
    }
}

template<int I>
inline void mff_rseq<I>::update_queues_for_height(uint32_t height) {
    // put seqs into pool
    uint32_t frozen_purge_height = height - 100;
    uint32_t chilled_purge_height = height - 200;

    if (frozen_queue.count(frozen_purge_height)) {
        if (frozen_queue[frozen_purge_height].size() > 0) {
            // nlprintf("<<<<<<<< %4zu frozen seqs from height=%u\n", frozen_queue[frozen_purge_height].size(), frozen_purge_height);
            for (seq_t seq : frozen_queue[frozen_purge_height]) {
                DSL(seq, "update_queues @ %u (frozen)\n", height);
                // seq_pool.insert(seq);
                if (txs.count(seq)) {
                    known_txid[txs[seq]->id] = -(int64_t)active_chain.height;
                    DTX(txs[seq]->id, "update_queues @ %u (frozen)\n", height);
                    seqs.erase(txs[seq]->id);
                    txs.erase(seq);
                }
            }
        }
        frozen_queue.erase(frozen_purge_height);
    }
    if (chilled_queue.count(chilled_purge_height)) {
        if (chilled_queue[chilled_purge_height].size() > 0) {
            // nlprintf("<<<<<<<<<<<<< %4zu chilled seqs from height=%u\n", chilled_queue[chilled_purge_height].size(), chilled_purge_height);
            for (seq_t seq : chilled_queue[chilled_purge_height]) {
                DSL(seq, "update_queues @ %u (chilled)\n", height);
                // seq_pool.insert(seq);
                if (txs.count(seq)) {
                    known_txid[txs[seq]->id] = active_chain.height;
                    DTX(txs[seq]->id, "update_queues @ %u (chilled)\n", height);
                    seqs.erase(txs[seq]->id);
                    txs.erase(seq);
                }
            }
        }
        chilled_queue.erase(chilled_purge_height);
    }
}

template<int I>
inline void mff_rseq<I>::update_queues() {
    uint32_t height = active_chain.height;
    uint32_t prev_height = active_chain.chain.size() > 1 ? active_chain.chain[active_chain.chain.size() - 2]->height : height - 1;
    for (uint32_t biter = prev_height; biter < height; ++biter) {
        update_queues_for_height(biter + 1);
    }

    // uint32_t frozen_purge_height = height - 100;
    // 
    // for (uint32_t h = frozen_purge_height; h <= height; ++h) {
    //     size_t f = frozen_queue.count(h) ? frozen_queue[h].size() : 0;
    //     size_t c = chilled_queue.count(h) ? chilled_queue[h].size() : 0;
    //     if (f + c && (h < frozen_purge_height + 5 || h > height - 5)) {
    //         printf("%-6u : %4zu %4zu\n", h, f, c);
    //     } else if (h == height - 5) printf(":::::: : :::: ::::\n");
    // }
    // printf("%zu/%llu (%.2f%%) known transactions in memory\n", txs.size(), nextseq, 100.0 * txs.size() / nextseq);
}

template void mff_rseq<0>::apply_block(std::shared_ptr<block> b);
template void mff_rseq<0>::undo_block_at_height(uint32_t height);
template seq_t mff_rseq<0>::touched_txid(const uint256& txid, bool count);
template mff_rseq<0>::mff_rseq(const std::string path, bool readonly);
template mff_rseq<0>::mff_rseq(FILE* fp, bool readonly);
template mff_rseq<0>::~mff_rseq();
template bool mff_rseq<0>::read_entry();
// template void mff_rseq<0>::write_entry(entry* e);
template seq_t mff_rseq<0>::claim_seq(const uint256& txid);
template uint256 mff_rseq<0>::get_replacement_txid() const;
template uint256 mff_rseq<0>::get_invalidated_txid() const;
template seq_t mff_rseq<0>::seq_read();
template void mff_rseq<0>::seq_write(seq_t seq);
// template bool mff_rseq<0>::test_entry(entry* e);
template const std::shared_ptr<tx> mff_rseq<0>::register_entry(const tiny::mempool_entry& entry, bool known);
template void mff_rseq<0>::add_entry(std::shared_ptr<const tiny::mempool_entry>& entry);
template void mff_rseq<0>::remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause);
template void mff_rseq<0>::push_block(int height, uint256 hash, const std::vector<tiny::tx>& txs);
template void mff_rseq<0>::pop_block(int height);
template int64_t mff_rseq<0>::peek_time();

template void mff_rseq<1>::apply_block(std::shared_ptr<block> b);
template void mff_rseq<1>::undo_block_at_height(uint32_t height);
template seq_t mff_rseq<1>::touched_txid(const uint256& txid, bool count);
template mff_rseq<1>::mff_rseq(const std::string path, bool readonly);
template mff_rseq<1>::mff_rseq(FILE* fp, bool readonly);
template mff_rseq<1>::~mff_rseq();
template bool mff_rseq<1>::read_entry();
// template void mff_rseq<1>::write_entry(entry* e);
template seq_t mff_rseq<1>::claim_seq(const uint256& txid);
template uint256 mff_rseq<1>::get_replacement_txid() const;
template uint256 mff_rseq<1>::get_invalidated_txid() const;
template seq_t mff_rseq<1>::seq_read();
template void mff_rseq<1>::seq_write(seq_t seq);
// template bool mff_rseq<1>::test_entry(entry* e);
template const std::shared_ptr<tx> mff_rseq<1>::register_entry(const tiny::mempool_entry& entry, bool known);
template void mff_rseq<1>::add_entry(std::shared_ptr<const tiny::mempool_entry>& entry);
template void mff_rseq<1>::remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause);
template void mff_rseq<1>::push_block(int height, uint256 hash, const std::vector<tiny::tx>& txs);
template void mff_rseq<1>::pop_block(int height);
template int64_t mff_rseq<1>::peek_time();

} // namespace mff
