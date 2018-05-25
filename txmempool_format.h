// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_FORMAT_H
#define BITCOIN_TXMEMPOOL_FORMAT_H

#include <mff.h>
#include <serialize.h>
#include <streams.h>

namespace mff {

class rseq_container {
public:
    virtual seq_t seq_read() = 0;
    virtual void seq_write(seq_t seq) = 0;
};

#define MAX_RSEQ_CONTAINERS 2
extern rseq_container* g_rseq_ctr[MAX_RSEQ_CONTAINERS];

template <typename Stream, int I>
class rseq_adapter: public adapter<Stream> {
public:
    #define DEBUG_SER(args...) // printf(args)
    void serialize_outpoint(Stream& s, const outpoint& o) {
        DEBUG_SER("serializing %s outpoint\n", o.known ? "known" : "unknown");
        DEBUG_SER("o.n=%llu\n", o.n);
        Serialize(s, COMPACTSIZE(o.n));
        if (o.known) {
            DEBUG_SER("o.seq = %llu\n", o.seq);
            g_rseq_ctr[I]->seq_write(o.seq);
        } else {
            DEBUG_SER("o.txid = %s\n", o.txid.ToString().c_str());
            Serialize(s, o.txid);
        }
    }
    void deserialize_outpoint(Stream& s, outpoint& o) {
        DEBUG_SER("deserializing %s outpoint\n", o.known ? "known" : "unknown");
        Unserialize(s, COMPACTSIZE(o.n));
        DEBUG_SER("o.n=%llu\n", o.n);
        if (o.known) {
            o.seq = g_rseq_ctr[I]->seq_read();
            DEBUG_SER("o.seq = %llu\n", o.seq);
        } else {
            Unserialize(s, o.txid);
            DEBUG_SER("o.txid = %s\n", o.txid.ToString().c_str());
        }
    }
    void serialize_tx(Stream& s, const tx& t) {
        DEBUG_SER("serializing tx\n");
        DEBUG_SER("- id: %s\n", t.id.ToString().c_str());
        Serialize(s, t.id);
        DEBUG_SER("- seq: %llu\n", t.seq);
        g_rseq_ctr[I]->seq_write(t.seq);
        DEBUG_SER("- weight: %llu\n", t.weight);
        Serialize(s, COMPACTSIZE(t.weight));
        DEBUG_SER("- fee: %llu\n", t.fee);
        Serialize(s, COMPACTSIZE(t.fee));
        DEBUG_SER("- inputs: %llu\n", t.inputs);
        Serialize(s, COMPACTSIZE(t.inputs));
        for (uint64_t i = 0; i < t.inputs; ++i) {
            DEBUG_SER("--- input #%llu/%llu\n", i+1, t.inputs);
            DEBUG_SER("--- state: %u\n", t.state[i]);
            Serialize(s, t.state[i]);
            if (t.state[i] & outpoint::state_coinbase_flag) {
                DEBUG_SER("--- vin: (coinbase)\n");
            }
            if ((t.state[i] & 3) != outpoint::state_confirmed && !(t.state[i] & outpoint::state_coinbase_flag)) {
                // if (!ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
                DEBUG_SER("--- vin: %s\n", t.vin[i].to_string().c_str());
                t.vin[i].serialize(s, this);
            }
        }
    }
    void deserialize_tx(Stream& s, tx& t) {
        DEBUG_SER("deserializing tx\n");
        Unserialize(s, t.id);
        DEBUG_SER("- id: %s\n", t.id.ToString().c_str());
        t.seq = g_rseq_ctr[I]->seq_read();
        DEBUG_SER("- seq: %llu\n", t.seq);
        Unserialize(s, COMPACTSIZE(t.weight));
        DEBUG_SER("- weight: %llu\n", t.weight);
        Unserialize(s, COMPACTSIZE(t.fee));
        DEBUG_SER("- fee: %llu\n", t.fee);
        Unserialize(s, COMPACTSIZE(t.inputs));
        DEBUG_SER("- inputs: %llu\n", t.inputs);
        t.state.resize(t.inputs);
        t.vin.resize(t.inputs);
        for (uint64_t i = 0; i < t.inputs; ++i) {
            DEBUG_SER("--- input #%llu/%llu\n", i+1, t.inputs);
            Unserialize(s, t.state[i]);
            DEBUG_SER("--- state: %u\n", t.state[i]);
            assert(t.state[i] <= (outpoint::state_coinbase_flag | outpoint::state_confirmed));
            if (t.state[i] & outpoint::state_coinbase_flag) {
                t.vin[i] = outpoint::coinbase();
                DEBUG_SER("--- vin: (coinbase) %s\n", t.vin[i].to_string().c_str());
            } else if ((t.state[i] & 3) != outpoint::state_confirmed) {
                t.vin[i] = outpoint(t.state[i] == outpoint::state_known);
                t.vin[i].deserialize(s, this);
                DEBUG_SER("--- vin: %s\n", t.vin[i].to_string().c_str());
            }
        }
    }
    void serialize_block(Stream& s, const block& b) {
        Serialize(s, b.height);
        Serialize(s, b.hash);
        if (b.is_known) return;
        Serialize(s, COMPACTSIZE(b.count_known));
        for (uint64_t i = 0; i < b.count_known; ++i) {
            g_rseq_ctr[I]->seq_write(b.known[i]);
        }
        Serialize(s, b.unknown);
    }
    void deserialize_block(Stream& s, block& b) {
        Unserialize(s, b.height);
        Unserialize(s, b.hash);
        if (b.is_known) return;
        Unserialize(s, COMPACTSIZE(b.count_known));
        b.known.resize(b.count_known);
        for (uint64_t i = 0; i < b.count_known; ++i) {
            b.known[i] = g_rseq_ctr[I]->seq_read();
        }
        Unserialize(s, b.unknown);
    }
};

template<int I>
class mff_rseq: public mff, public rseq_container {
private:
    int64_t lastflush;
    FILE* in_fp;
    CAutoFile in;
    rseq_adapter<CAutoFile, I> serializer;

    void apply_block(std::shared_ptr<block> b);
    void undo_block_at_height(uint32_t height);
    entry last_entry;
    inline void sync();
public:
    std::map<uint256,uint32_t> txid_hits;
    blockdict_t blocks;
    chain active_chain;
    uint64_t last_seq;
    uint64_t nextseq;

    CMD last_cmd;
    std::vector<seq_t> last_seqs;
    seq_t replacement_seq; // for TX_INVALID; points to the new txid
    uint256 replacement_txid; // for TX_INVALID
    seq_t invalidated_seq; // for TX_INVALID; points to the txid being invalidated
    uint256 invalidated_txid; // for TX_INVALID
    uint8_t last_invalid_state; // for TX_INVALID
    uint8_t last_out_reason; // for TX_OUT
    tiny::tx last_invalidated_tx; // for TX_INVALID
    std::shared_ptr<tx> last_recorded_tx; // for TX_REC
    std::vector<uint8_t> last_invalidated_txhex; // for TX_INVALID
    seq_t touched_txid(const uint256& txid, bool count); // returns seq for txid or 0 if not touched

    mff_rseq(const std::string path = "", bool readonly = true);
    ~mff_rseq();
    entry* read_entry() override;
    void write_entry(entry* e) override;
    seq_t claim_seq(const uint256& txid) override;
    long tell() override { return ftell(in_fp); }
    uint256 get_replacement_txid() const;
    uint256 get_invalidated_txid() const;

    seq_t seq_read() override;
    void seq_write(seq_t seq) override;

    void flush() override { fflush(in_fp); }
};

} // namespace mff

#endif // BITCOIN_TXMEMPOOL_FORMAT_H
