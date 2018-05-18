// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include <tinytx.h>

#include <txmempool_format.h>

// #define DEBUG_SEQ 14937
#define DSL(s, fmt...) // if (s == DEBUG_SEQ) { printf("[SEQ] " fmt); }
// #define l(args...) if (active_chain.height == 521703) { printf(args); }
// #define l1(args...) if (active_chain.height == 521702) { printf(args); }

namespace mff {

rseq_container* g_rseq_ctr = nullptr;

template<typename T>
inline bool find_erase(std::vector<T>& v, const T& e) {
    auto it = std::find(std::begin(v), std::end(v), e);
    if (it == std::end(v)) return false;
    v.erase(it);
    return true;
}

// bool showinfo = false;
#define mplinfo_(args...) // if (showinfo) { printf(args); }
#define mplinfo(args...) // if (showinfo) { printf("[MPL::info] " args); }
#define mplwarn(args...) printf("[MPL::warn] " args)
#define mplerr(args...)  fprintf(stderr, "[MPL::err]  " args)

#define read_time(t) \
    if (timerel) {\
        uint8_t r; \
        in >> r; \
        t = last_time + r; \
    } else { \
        static_assert(sizeof(t) == 8, #t " is of wrong type! Must be int64_t!"); \
        in >> t; \
    }

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

inline FILE* setup_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr) {
        fprintf(stderr, "unable to open %s\n", path);
        assert(fp);
    }
    return fp;
}

reader::reader(const std::string path) : in_fp(setup_file(path.length() > 0 ? path.c_str() : (std::string(std::getenv("HOME")) + "/mff.out").c_str())), in(in_fp, SER_DISK, 0) {
    assert(g_rseq_ctr == nullptr);
    g_rseq_ctr = this;
    last_seq = 0;
    // out.debugme(true);
    mplinfo("start %s\n", path.c_str());
}

reader::~reader() {
    g_rseq_ctr = nullptr;
}

void reader::apply_block(std::shared_ptr<block> b) {
    // l1("apply block %u (%s)\n", b->height, b->hash.ToString().c_str());
    if (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
        mplwarn("dealing with TX_UNCONF missing bug 20180502153142\n");
        while (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
            mplinfo("unconfirming block #%u\n", active_chain.height);
            undo_block_at_height(active_chain.height);
        }
    }
    assert(active_chain.chain.size() == 0 || b->height == active_chain.height + 1);
    active_chain.height = b->height;
    active_chain.chain.push_back(b);
    assert(blocks[b->hash] == b);
    // l("block for #%u; %zu known, %zu unknown\n", b->height, b->known.size(), b->unknown.size());
    // we need to mark all transactions as confirmed
    for (auto seq : b->known) {
        DSL(seq, "block confirm\n");
        assert(txs.count(seq));
        txs[seq]->location = tx::location_confirmed;
        last_seqs.push_back(seq);
        // l("- %s\n", txs[seq]->id.ToString().c_str());
    }
}

void reader::undo_block_at_height(uint32_t height) {
    mplinfo("undo block %u\n", height);
    // we need to unmark confirmed transactions
    assert(height == active_chain.height);
    auto b = active_chain.chain.back();
    active_chain.chain.pop_back();
    active_chain.height--;
    for (auto seq : b->known) {
        DSL(seq, "block orphan\n");
        assert(txs.count(seq));
        txs[seq]->location = tx::location_in_mempool;
        last_seqs.push_back(seq);
    }
}

inline std::string bits(uint8_t u) {
    std::string s = "";
    for (uint8_t i = 128; i; i>>=1) {
        s += (u & i) ? "1" : "0";
    }
    return s;
}

// static uint256 foo = uint256S("163ee79aa165df2dbd552d41e89abb266cf76b0763504e6ef582cb6df2e0befc");

seq_t reader::touched_txid(const uint256& txid, bool count) {
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
        if (last_cmd == TX_CONF) {
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
    if (last_cmd == TX_CONF) {
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

uint256 reader::get_replacement_txid() const {
    return replacement_seq && txs.count(replacement_seq) ? txs.at(replacement_seq)->id : replacement_txid;
}

uint256 reader::get_invalidated_txid() const {
    return invalidated_seq && txs.count(invalidated_seq) ? txs.at(invalidated_seq)->id : invalidated_txid;
}

bool reader::read_entry() {
    // long x = ftell(in_fp); showinfo = false; x > 30304207 && x < 30305869;
    uint8_t u8;
    CMD cmd;
    bool known, timerel;
    try {
        in >> u8;
    } catch (std::ios_base::failure& f) {
        return false;
    }
    last_cmd = cmd = (CMD)(u8 & 0x1f); // 0b0001 1111
    known = (u8 >> 6) & 1;
    timerel = (u8 >> 7) & 1;
    last_seqs.clear();
    switch (cmd) {
        case CMD::TIME_SET:
            // time updated after switch()
            mplinfo("TIME_SET()\n");
            break;

        case TX_REC: {
            mplinfo("TX_REC(): "); fflush(stdout);
            auto t = std::make_shared<tx>();
            in >> *t;
            last_recorded_tx = t;
            DSL(t->seq, "TX_REC\n");
            if (txs.count(t->seq)) {
                seqs.erase(txs[t->seq]->id); // this unlinks the txid-seq rel
            }
            txs[t->seq] = t;
            seqs[t->id] = t->seq;
            last_seqs.push_back(t->seq);
            for (const auto& prevout : t->vin) {
                if (prevout.is_known()) {
                    last_seqs.push_back(prevout.get_seq());
                }
            }
            mplinfo_("id=%s, seq=%llu\n", t->id.ToString().c_str(), t->seq);
            t->location = tx::location_in_mempool;
            break;
        }

        case TX_IN: {
            mplinfo("TX_IN(): "); fflush(stdout);
            uint64_t seq = seq_read();
            DSL(seq, "TX_IN\n");
            if (txs.count(seq)) {
                txs[seq]->location = tx::location_in_mempool;
            }
            mplinfo_("seq=%llu\n", seq);
            last_seqs.push_back(seq);
            break;
        }

        case TX_CONF: {
            // l1("TX_CONF(%s): ", known ? "known" : "unknown");
            if (known) {
                // we know the block; just get the header info and find it, then apply
                block b(known);
                in >> b;
                assert(blocks.count(b.hash));
                // l1("block %u=%s\n", b.height, b.hash.ToString().c_str());
                apply_block(blocks[b.hash]);
            } else {
                auto b = std::make_shared<block>();
                in >> *b;
                blocks[b->hash] = b;
                // l1("block %u=%s\n", b->height, b->hash.ToString().c_str());
                apply_block(b);
            }
            break;
        }

        case TX_OUT: {
            mplinfo("TX_OUT(): "); fflush(stdout);
            seq_t seq = 0;
            uint8_t reason;
            read_txseq(known, seq);
            last_seqs.push_back(seq);
            DSL(seq, "TX_OUT\n");
            in >> reason;
            assert(txs.count(seq));
            last_out_reason = reason;
            auto t = txs[seq];
            t->location = tx::location_discarded;
            t->out_reason = (tx::out_reason_enum)reason;
            mplinfo_("seq=%llu, reason=%s\n", seq, tx_out_reason_str(reason));
            break;
        }

        case TX_INVALID: {
            mplinfo("TX_INVALID(): "); fflush(stdout);
            // out.debugme(true);
            replacement_seq = 0;
            replacement_txid.SetNull();
            mplinfo_("\n----- invalid deserialization begins -----\n");
            seq_t tx_invalid(0);
            uint8_t state;
            seq_t tx_cause(0);
            mplinfo_("--- read_txseq(%d, tx_invalid)\n", known);
            read_txseq_keep(known, tx_invalid, invalidated_txid);
            invalidated_seq = tx_invalid;
            last_seqs.push_back(tx_invalid);
            mplinfo_("--- state\n");
            in >> state;
            bool cause_known = (state >> 6) & 1;
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
            // out.debugme(false);
            break;
        }

        case TX_UNCONF: {
            mplinfo("TX_UNCONF(): "); fflush(stdout);
            uint32_t height;
            in >> height;
            if (known) {
                mplinfo_("known, height=%u\n", height);
                undo_block_at_height(height);
            } else {
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
            mplerr("u8 = %u, cmd = %u\n", u8, cmd);
            assert(!"unknown command"); // todo: exceptionize
    }
    read_time(last_time);
    // showinfo = false;
    return true;
}

inline seq_t reader::seq_read() {
    int64_t rseq;
    in >> CVarInt<VarIntMode::SIGNED, int64_t>{rseq};
    last_seq += rseq;
    return last_seq;
}

inline void reader::seq_write(seq_t seq) {
    assert(!"writing disallowed");
}

} // namespace mff
