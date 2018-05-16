// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_FORMAT_H
#define BITCOIN_TXMEMPOOL_FORMAT_H

#include <vector>
#include <map>

#include <uint256.h>
#include <serialize.h>
#include <streams.h>
#include <tinytx.h>
// #include <txmempool.h>

namespace mff {

enum CMD: uint8_t {
    TIME_SET   = 0x00,
    TX_REC     = 0x01,
    TX_IN      = 0x02,
    TX_CONF    = 0x03,
    TX_OUT     = 0x04,
    TX_INVALID = 0x05,
    TX_UNCONF  = 0x06,

    TX_KNOWN_BIT = 0x40,    // 0b0100 0000
    TIME_REL_BIT = 0x80,    // 0b1000 0000
};

inline std::string cmd_str(CMD c) {
    static const char* str[] = {
        "TIME_SET",
        "TX_REC",
        "TX_IN",
        "TX_CONF",
        "TX_OUT",
        "TX_INVALID",
        "TX_UNCONF",
    };
    return c < 7 ? str[c] : "????";
}

typedef uint64_t seq_t;

class outpoint {
public:
    enum state: uint8_t {
        state_unknown   = 0,
        state_known     = 1,
        state_confirmed = 2,
    };
    
    outpoint(bool known_in = false)                 : known(known_in), n(0),    seq(0),      txid(uint256()) {}
    outpoint(uint64_t n_in, seq_t seq_in)           : known(true),     n(n_in), seq(seq_in), txid(uint256()) {}
    outpoint(uint64_t n_in, const uint256& txid_in) : known(false),    n(n_in), seq(0),      txid(txid_in)   {}

    bool is_known()           const { return known; }
    const uint256& get_txid() const { assert(!known); return txid; }
    seq_t get_seq()           const { assert( known); return seq; }
    uint64_t get_n()          const { return n; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(COMPACTSIZE(n));
        if (known) {
            READWRITE(seq);
        } else {
            READWRITE(txid);
        }
    }
    std::string to_string() const {
        char s[1024];
        if (known) sprintf(s, "outpoint(known seq=%llu, n=%llu)", seq, n);
        else       sprintf(s, "outpoint(unknown txid=%s, n=%llu)", txid.ToString().c_str(), n);
        return s;
    }

private:
    bool known;
    uint64_t n;
    seq_t seq;
    uint256 txid;
};

struct tx {
    enum location_enum: uint8_t {
        location_in_mempool = 0,
        location_confirmed  = 1,
        location_discarded  = 2,
        location_invalid    = 3,
    } location;

    enum out_reason_enum: uint8_t {
        out_reason_low_fee    = 0,
        out_reason_age_expiry = 1,
        out_reason_unknown    = 2,
    } out_reason;

    enum invalid_reason_enum: uint8_t {
        invalid_rbf_bumped = 0,
        invalid_doublespent = 1,
        invalid_reorg = 2,
        invalid_unknown = 3,
    } invalid_reason;

    uint256 id;
    uint64_t seq;
    uint64_t weight;
    uint64_t fee;
    uint64_t inputs;
    std::vector<uint8_t> state;
    std::vector<outpoint> vin;
    uint32_t cool_height;

    inline double feerate() const { return double(fee)/double(vsize()); }
    inline uint64_t vsize() const { return (weight + 3)/4; }
    inline bool is(const uint256& txid) const { return txid == id; }
    inline bool spends(const uint256& txid, const seq_t seq, uint32_t& index_out) const {
        for (const auto& prevout : vin) {
            if (prevout.is_known() ? prevout.get_seq() == seq : prevout.get_txid() == txid) {
                index_out = prevout.get_n();
                return true;
            }
        }
        return false;
    }
    std::string to_string() const {
        std::string s = strprintf("tx(%s):", id.ToString());
        for (uint64_t i = 0; i < inputs; i++) {
            s += "\n\t" + (state[i] == outpoint::state_confirmed ? "<found in block>" : vin[i].to_string());
        }
        return s;
    }

    tx() {
        location = location_in_mempool;
        cool_height = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        // printf("serializing tx\n");
        // if (!ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        READWRITE(id);
        // if (ser_action.ForRead()) printf("- id: %s\n", id.ToString().c_str());
        // if (!ser_action.ForRead()) printf("- seq: %llu\n", seq);
        READWRITE(COMPACTSIZE(seq));
        // if (ser_action.ForRead()) printf("- seq: %llu\n", seq);
        // if (!ser_action.ForRead()) printf("- weight: %llu\n", weight);
        READWRITE(COMPACTSIZE(weight));
        // if (ser_action.ForRead()) printf("- weight: %llu\n", weight);
        // if (!ser_action.ForRead()) printf("- fee: %llu\n", fee);
        READWRITE(COMPACTSIZE(fee));
        // if (ser_action.ForRead()) printf("- fee: %llu\n", fee);
        // if (!ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        READWRITE(COMPACTSIZE(inputs));
        // if (ser_action.ForRead()) printf("- inputs: %llu\n", inputs);
        if (ser_action.ForRead()) {
            state.resize(inputs);
            vin.resize(inputs);
        }
        for (uint64_t i = 0; i < inputs; ++i) {
            // printf("--- input #%llu/%llu\n", i+1, inputs);
            // if (!ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            READWRITE(state[i]);
            // if (ser_action.ForRead()) printf("--- state: %u\n", state[i]);
            assert(state[i] <= outpoint::state_confirmed);
            if (state[i] != outpoint::state_confirmed) {
                if (ser_action.ForRead()) {
                    vin[i] = outpoint(state[i] == outpoint::state_known);
                }
                // if (!ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
                READWRITE(vin[i]);
                // if (ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
            }
        }
    }
};

struct block {
    uint32_t height;
    uint256 hash;
    uint64_t count_known;
    std::vector<seq_t> known;
    std::vector<uint256> unknown;
    bool is_known;

    block(bool is_known_in = false) {
        is_known = is_known_in;
    }

    block(uint32_t height_in, const uint256& hash_in, bool is_known_in = false) {
        height = height_in;
        hash = hash_in;
        is_known = is_known_in;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(height);
        READWRITE(hash);
        if (is_known) return;
        READWRITE(COMPACTSIZE(count_known));
        if (ser_action.ForRead()) {
            known.resize(count_known);
        }
        for (uint64_t i = 0; i < count_known; ++i) {
            READWRITE(COMPACTSIZE(known[i]));
        }
        READWRITE(unknown);
    }
};

typedef std::map<uint256, uint64_t> seqdict_t;
typedef std::map<uint64_t, std::shared_ptr<tx>> txs_t;
typedef std::map<uint256, std::shared_ptr<block>> blockdict_t;

struct chain {
    uint32_t height;
    std::vector<std::shared_ptr<block>> chain;
};


#define tx_out_reason_str(reason) (reason == mff::tx::out_reason_low_fee ? "low fee" :\
        reason == mff::tx::out_reason_age_expiry ? "age expiry" :\
        reason == mff::tx::out_reason_unknown ? "???" : "*DATA CORRUPTION*"\
    )

#define tx_invalid_state_str(state) (state == mff::tx::invalid_rbf_bumped ? "RBF-bumped" :\
        state == mff::tx::invalid_doublespent ? "conflict (double-spent)" :\
        state == mff::tx::invalid_reorg ? "reorg" :\
        state == mff::tx::invalid_unknown ? "???" : "*DATA CORRUPTION*"\
    )

class reader {
private:
    FILE* in_fp;
    CAutoFile in;

    void apply_block(std::shared_ptr<block> b);
    void undo_block_at_height(uint32_t height);
public:
    std::map<uint256,uint32_t> txid_hits;
    seqdict_t seqs;
    txs_t txs;
    blockdict_t blocks;
    chain active_chain;
    int64_t last_time;
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

    reader(const std::string path = "");
    bool read_entry();
    uint256 get_replacement_txid() const;
    uint256 get_invalidated_txid() const;
};

} // namespace mff

#endif // BITCOIN_TXMEMPOOL_FORMAT_H
