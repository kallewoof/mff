// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_FORMAT_RS_H
#define BITCOIN_TXMEMPOOL_FORMAT_RS_H

#include <serialize.h>
#include <streams.h>

#include <mff.h>

namespace mff {

template <typename Stream>
class reused_sequence_adapter: public adapter<Stream> {
public:
    void serialize_outpoint(Stream& s, const outpoint& o) {
        Serialize(s, COMPACTSIZE(o.n));
        if (o.known) {
            Serialize(s, o.seq); // TODO: bug but leaving as is for now
        } else {
            Serialize(s, o.txid);
        }
    }

    void deserialize_outpoint(Stream& s, outpoint& o) {
        Unserialize(s, COMPACTSIZE(o.n));
        if (o.known) {
            Unserialize(s, o.seq); // TODO: bug but leaving as is for now
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
        Serialize(s, COMPACTSIZE(t.seq));
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
            if (t.state[i] != outpoint::state_confirmed) {
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
        Unserialize(s, COMPACTSIZE(t.seq));
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
            assert(t.state[i] <= outpoint::state_confirmed);
            if (t.state[i] != outpoint::state_confirmed) {
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
            Serialize(s, COMPACTSIZE(b.known[i]));
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
            Unserialize(s, COMPACTSIZE(b.known[i]));
        }
        Unserialize(s, b.unknown);
    }
};

class mff_rs: public mff {
private:
    FILE* in_fp;
    CAutoFile in;
    reused_sequence_adapter<CAutoFile> serializer;

    void apply_block(std::shared_ptr<block> b);
    void undo_block_at_height(uint32_t height);
public:
    std::map<uint256,uint32_t> txid_hits;
    blockdict_t blocks;
    chain active_chain;
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

    mff_rs(const std::string path = "", bool readonly = true);
    bool read_entry() override;
    int64_t peek_time() override;
    // void write_entry(entry* e) override;
    long tell() override { return ftell(in_fp); }
    uint256 get_replacement_txid() const;
    uint256 get_invalidated_txid() const;
    void flush() override { fflush(in_fp); }
};

} // namespace mff

#endif // BITCOIN_TXMEMPOOL_FORMAT_RS_H
