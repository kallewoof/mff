// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_FORMAT_AJ_H
#define BITCOIN_TXMEMPOOL_FORMAT_AJ_H

#include <mff.h>
#include <serialize.h>
#include <streams.h>
#include <tinytx.h>
#include <tinyblock.h>

namespace mff {

extern std::string aj_rpc_call;

class aj_container {
public:
    virtual seq_t seq_read() = 0;
    virtual void seq_write(seq_t seq) = 0;
};

extern aj_container* g_aj_ctr;

template <typename Stream>
class aj_adapter: public adapter<Stream> {
public:
    void serialize_outpoint(Stream& s, const outpoint& o) {
        Serialize(s, COMPACTSIZE(o.n));
        if (o.known) {
            g_aj_ctr->seq_write(o.seq);
        } else {
            Serialize(s, o.txid);
        }
    }
    void deserialize_outpoint(Stream& s, outpoint& o) {
        Unserialize(s, COMPACTSIZE(o.n));
        if (o.known) {
            o.seq = g_aj_ctr->seq_read();
        } else {
            Unserialize(s, o.txid);
        }
    }
    void serialize_tx(Stream& s, const tx& t) {
        // printf("serializing tx\n");
        // if (!ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        Serialize(s, t.id);
        // if (ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        // if (!ser_action.ForRead()) printf("- seq: %llu\n", seq);
        g_aj_ctr->seq_write(t.seq);
        // if (ser_action.ForRead()) printf("- seq: %llu\n", seq);
        // if (!ser_action.ForRead()) printf("- weight: %llu\n", weight);
        Serialize(s, COMPACTSIZE(t.weight));
        // if (ser_action.ForRead()) printf("- weight: %llu\n", weight);
        // if (!ser_action.ForRead()) printf("- fee: %llu\n", fee);
        Serialize(s, COMPACTSIZE(t.fee));
        // if (ser_action.ForRead()) printf("- fee: %llu\n", fee);
        // if (!ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        Serialize(s, COMPACTSIZE(t.inputs));
        // if (ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        for (uint64_t i = 0; i < t.inputs; ++i) {
            // printf("--- input #%llu/%llu\n", i+1, inputs);
            // if (!ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            Serialize(s, t.state[i]);
            // if (ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            if (t.state[i] != outpoint::state_confirmed && t.state[i] != outpoint::state_coinbase) {
                // if (!ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
                t.vin[i].serialize(s, this);
                // if (ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
            }
        }
    }
    void deserialize_tx(Stream& s, tx& t) {
        // printf("serializing tx\n");
        // if (!ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        Unserialize(s, t.id);
        // if (ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        // if (!ser_action.ForRead()) printf("- seq: %llu\n", seq);
        t.seq = g_aj_ctr->seq_read();
        // if (ser_action.ForRead()) printf("- seq: %llu\n", seq);
        // if (!ser_action.ForRead()) printf("- weight: %llu\n", weight);
        Unserialize(s, COMPACTSIZE(t.weight));
        // if (ser_action.ForRead()) printf("- weight: %llu\n", weight);
        // if (!ser_action.ForRead()) printf("- fee: %llu\n", fee);
        Unserialize(s, COMPACTSIZE(t.fee));
        // if (ser_action.ForRead()) printf("- fee: %llu\n", fee);
        // if (!ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        Unserialize(s, COMPACTSIZE(t.inputs));
        // if (ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        t.state.resize(t.inputs);
        t.vin.resize(t.inputs);
        for (uint64_t i = 0; i < t.inputs; ++i) {
            // printf("--- input #%llu/%llu\n", i+1, inputs);
            // if (!ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            Unserialize(s, t.state[i]);
            // if (ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            assert(t.state[i] <= outpoint::state_coinbase);
            if (t.state[i] == outpoint::state_coinbase) {
                t.vin[i] = outpoint::coinbase();
            } else if (t.state[i] != outpoint::state_confirmed) {
                t.vin[i] = outpoint(t.state[i] == outpoint::state_known);
                // if (!ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
                t.vin[i].deserialize(s, this);
                // if (ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
            }
        }
    }
    void serialize_block(Stream& s, const block& b) {
        Serialize(s, b.height);
        Serialize(s, b.hash);
        if (b.is_known) return;
        Serialize(s, COMPACTSIZE(b.count_known));
        for (uint64_t i = 0; i < b.count_known; ++i) {
            g_aj_ctr->seq_write(b.known[i]);
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
            b.known[i] = g_aj_ctr->seq_read();
        }
        Unserialize(s, b.unknown);
    }
};

class mff_aj: public mff, public aj_container {
private:
    int64_t lastflush;
    FILE* in_fp;
    CAutoFile in;
    aj_adapter<CAutoFile> serializer;

    uint64_t nextseq = 1;
    void apply_block(std::shared_ptr<block> b);
    void undo_block_at_height(uint32_t height);
    inline void sync();

    // RPC stuff
    void rpc_get_block(const uint256& blockhex, tiny::block& b, uint32_t& height);
    void rpc_get_tx(const uint256& txhex, tiny::tx& tx, size_t retries = 0);
    tiny::amount rpc_get_tx_input_amount(tiny::tx& tx);

    // AMAP stuff
    int64_t amap_get_output_value(const uint256& txid, int n);

    // RPC/AMAP combinator
    int64_t get_output_value(const uint256& txid, int n);
    int64_t get_tx_input_amount(tiny::tx& tx);

    char* buffer;
    size_t buffer_cap;
    uint256 hashtx;
    bool read_hashtx = false;
    void insert(tiny::tx& t);
    void confirm(uint32_t height, const uint256& hash, tiny::block& b);
    std::shared_ptr<tx> register_tx(tiny::tx& t);
    std::map<seq_t, std::vector<std::shared_ptr<tx>>> deps;
    std::vector<std::shared_ptr<entry>> backlog;
    void tx_invalid(bool known, seq_t seq, std::shared_ptr<tx> t, const tiny::tx& tref, uint8_t state, const uint256* cause);
    void check_deps(tiny::tx* cause, const uint256& txid, const uint32_t* index_or_all = nullptr);
    void check_deps(tiny::tx& newcomer);
    void push_entry(std::shared_ptr<entry> e);
    std::vector<std::shared_ptr<tiny::tx>> backlog_ttxs;
    std::shared_ptr<entry> previous_entry; // used to prevent from destroying before use
public:
    std::map<uint256,uint32_t> txid_hits;
    blockdict_t blocks;
    chain active_chain;
    int64_t read_time;
    uint64_t last_seq;

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

    mff_aj(const std::string path = "", bool readonly = true);
    ~mff_aj();

    entry* read_entry() override;
    long tell() override { return ftell(in_fp); }

    seq_t seq_read() override;
    void seq_write(seq_t seq) override;

    void flush() override { fflush(in_fp); }

    uint256 get_replacement_txid() const;
    uint256 get_invalidated_txid() const;
};

} // namespace mff

#endif // BITCOIN_TXMEMPOOL_FORMAT_AJ_H
