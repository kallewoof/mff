// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MFF_H
#define BITCOIN_MFF_H

#include <vector>
#include <map>

#include <uint256.h>
#include <tinytx.h>
#include <utiltime.h>
#include <tinymempool.h>
#include <streams.h>

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

class outpoint;
class tx;
class block;

template <typename Stream>
class adapter {
public:
    virtual void serialize_outpoint(Stream& s, const outpoint& o) = 0;
    virtual void serialize_tx      (Stream& s, const tx& t)       = 0;
    virtual void serialize_block   (Stream& s, const block& b)    = 0;
    virtual void deserialize_outpoint(Stream& s, outpoint& o)     = 0;
    virtual void deserialize_tx      (Stream& s, tx& t)           = 0;
    virtual void deserialize_block   (Stream& s, block& b)        = 0;
};

class outpoint {
public:
    enum state: uint8_t {
        state_unknown   = 0,
        state_known     = 1,
        state_confirmed = 2,
        state_coinbase  = 3,
    };
    
    outpoint(bool known_in = false)                 : known(known_in), n(0),    seq(0),      txid(uint256()) {}
    outpoint(uint64_t n_in, seq_t seq_in)           : known(true),     n(n_in), seq(seq_in), txid(uint256()) {}
    outpoint(uint64_t n_in, const uint256& txid_in) : known(false),    n(n_in), seq(0),      txid(txid_in)   {}
    outpoint(const outpoint& o) : known(o.known), n(o.n), seq(o.seq), txid(o.txid) {}

    static inline outpoint coinbase() {
        return outpoint(0xffffffff, uint256());
    }

    bool operator==(const outpoint& other) const {
        return known == other.known && (
            known
            ? seq == other.seq
            : txid == other.txid
        );
    }

    bool is_known()           const { return known; }
    const uint256& get_txid() const { assert(!known); return txid; }
    seq_t get_seq()           const { assert( known); return seq; }
    uint64_t get_n()          const { return n; }

    std::string to_string() const {
        char s[1024];
        if (known) sprintf(s, "outpoint(known seq=%llu, n=%llu)", seq, n);
        else       sprintf(s, "outpoint(unknown txid=%s, n=%llu)", txid.ToString().c_str(), n);
        return s;
    }

    template <typename Stream>
    void serialize(Stream& s, adapter<Stream>* a) const { a->serialize_outpoint(s, *this); }
    template <typename Stream>
    void deserialize(Stream& s, adapter<Stream>* a) { a->deserialize_outpoint(s, *this); }

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
    std::vector<uint64_t> amounts; // memory only
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

    tx(const tx& t) : tx() {
        id = t.id;
        seq = t.seq;
        weight = t.weight;
        fee = t.fee;
        inputs = t.inputs;
        state = t.state;
        for (const outpoint& o : t.vin) {
            vin.emplace_back(o);
            const outpoint& ocopy = vin.back();
            assert(&o != &ocopy);
            assert(o == ocopy);
        }
        for (uint64_t a : t.amounts) {
            amounts.push_back(a);
        }
    }

    template <typename Stream>
    void serialize(Stream& s, adapter<Stream>* a) const { a->serialize_tx(s, *this); }
    template <typename Stream>
    void deserialize(Stream& s, adapter<Stream>* a) { a->deserialize_tx(s, *this); }
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

    block(const block& b) {
        height = b.height;
        hash = b.hash;
        count_known = b.count_known;
        known = b.known;
        unknown = b.unknown;
        is_known = b.is_known;
    }

    block(uint32_t height_in, const uint256& hash_in, bool is_known_in = false) {
        height = height_in;
        hash = hash_in;
        is_known = is_known_in;
    }

    template <typename Stream>
    void serialize(Stream& s, adapter<Stream>* a) const { a->serialize_block(s, *this); }
    template <typename Stream>
    void deserialize(Stream& s, adapter<Stream>* a) { a->deserialize_block(s, *this); }
};

typedef std::map<uint256, uint64_t> seqdict_t;
typedef std::map<uint64_t, std::shared_ptr<tx>> txs_t;
typedef std::map<uint256, std::shared_ptr<block>> blockdict_t;

struct chain {
    uint32_t height;
    std::vector<std::shared_ptr<block>> chain;
};

struct chain_delegate {
    virtual uint32_t expected_block_height() = 0;
};

// class seqdict_server {
// public:
//     seqdict_t seqs;
//     txs_t txs;
//     virtual seq_t claim_seq(const uint256& txid) = 0;
// };

// struct entry {
//     entry(seqdict_server* sds_in) : sds(sds_in) {}
//     seqdict_server* sds;
//     CMD cmd;
//     bool known;
//     int64_t time;
//     std::shared_ptr<tx> tx; // TX_REC, TX_IN, TX_OUT
//     std::shared_ptr<block> block_in; // TX_CONF
//     tx::out_reason_enum out_reason;
//     tx::invalid_reason_enum invalid_reason;
//     bool invalid_cause_known;
//     uint256 invalid_replacement; // TX_INVALID
//     const tiny::tx* invalid_tinytx; // TX_INVALID
//     std::vector<uint8_t>* invalid_txhex; // TX_INVALID
//     uint32_t unconf_height;
//     uint64_t unconf_count; // for known=false
//     std::vector<uint256> unconf_txids; // for known=false
// };

#define tx_out_reason_str(reason) (reason == ::mff::tx::out_reason_low_fee ? "low fee" :\
        reason == ::mff::tx::out_reason_age_expiry ? "age expiry" :\
        reason == ::mff::tx::out_reason_unknown ? "???" : "*DATA CORRUPTION*"\
    )

#define tx_invalid_state_str(state) (state == ::mff::tx::invalid_rbf_bumped ? "RBF-bumped" :\
        state == ::mff::tx::invalid_doublespent ? "conflict (double-spent)" :\
        state == ::mff::tx::invalid_reorg ? "reorg" :\
        state == ::mff::tx::invalid_unknown ? "???" : "*DATA CORRUPTION*"\
    )

class mff: public tiny::mempool_callback {
public:
    chain_delegate* chain_del = nullptr;
    virtual void link_source(mff* src) {}
    int64_t last_time;
    seqdict_t seqs;
    txs_t txs;
    uint64_t entry_counter = 0;
    int64_t* shared_time = nullptr;
    std::shared_ptr<tiny::mempool> mempool;
    void load_mempool(const std::string& path) {
        if (!mempool.get()) {
            mempool = std::make_shared<tiny::mempool>();
        }
        FILE* fp = fopen(path.c_str(), "rb");
        CAutoFile af(fp, SER_DISK, 0);
        af >> *mempool;
    }
    void save_mempool(const std::string& path) const {
        FILE* fp = fopen(path.c_str(), "wb");
        CAutoFile af(fp, SER_DISK, 0);
        af << *mempool;
    }

    virtual long tell() = 0;
    virtual void flush() = 0;
    virtual bool read_entry() = 0;
    virtual int64_t peek_time() = 0;
    // virtual void write_entry(entry* e) {
    //     assert(!"not implemented");
    // }
    // virtual seq_t claim_seq(const uint256& txid) override {
    //     assert(!"not implemented");
    // }
    uint8_t prot(CMD cmd, bool known) {
        // printf("- %s -\n", cmd_str(cmd).c_str());
        entry_counter++;
        int64_t time = shared_time ? *shared_time : GetTime();
        return cmd | (known ? CMD::TX_KNOWN_BIT : 0) | (time - last_time < 254 ? CMD::TIME_REL_BIT : 0);
    }

    // std::shared_ptr<tx> import_tx(seqdict_server* server, std::shared_ptr<tx> t) {
    //     // printf("converting %llu=%s == %s: ", server->seqs[t->id], t->id.ToString().c_str(), server->txs[server->seqs[t->id]]->id.ToString().c_str());
    //     // see if we have this tx
    //     if (seqs.count(t->id)) {
    //         // well then
    //         assert(txs.count(seqs[t->id]));
    //         // printf("got it already %s\n", txs[seqs[t->id]]->id.ToString().c_str());
    //         assert(txs[seqs[t->id]]->id == t->id);
    //         return txs[seqs[t->id]];
    //     }
    //     std::shared_ptr<tx> t2 = std::make_shared<tx>(*t);
    //     t2->seq = claim_seq(t2->id);
    //     // printf("prevouts ");
    //     // convert the inputs
    //     for (outpoint& o : t2->vin) {
    //         if (o.is_known()) {
    //             uint256 prevhash = server->txs[o.get_seq()]->id;
    //             assert(seqs.count(prevhash));
    //             o.seq = seqs[prevhash];
    //         }
    //     }
    //     // printf("got %llu=%s\n", seqs[t->id], t->id.ToString().c_str());
    //     return t2;
    // }
    // 
    // std::shared_ptr<block> import_block(seqdict_server* server, std::shared_ptr<block> blk) {
    //     std::shared_ptr<block> blk2 = std::make_shared<block>(*blk);
    //     // convert the known vector
    //     size_t size = blk2->known.size();
    //     for (size_t i = 0; i < size; ++i) {
    //         blk2->known[i] = seqs[server->txs[blk2->known[i]]->id];
    //     }
    //     return blk2;
    // }
};

class cluster {
private:
    std::vector<int64_t> times;
public:
    std::shared_ptr<tiny::mempool> mempool;
    std::vector<mff*> nodes;
    int64_t time;
    bool first_read = true;
    cluster() {
        mempool = std::make_shared<tiny::mempool>();
    }
    void add(mff* m) {
        assert(mempool.get());
        assert(!m->mempool.get());
        m->mempool = mempool;
        nodes.push_back(m);
        times.push_back(0);
    }
    bool read_entry() {
        if (nodes.size() == 1) {
            for (;;) {
                bool rv = nodes[0]->read_entry();
                if (rv && *nodes[0]->shared_time == 0) continue;
                if (rv) time = *nodes[0]->shared_time;
                return rv;
            }
        }
        if (first_read) {
            first_read = false;
            for (size_t i = 0; i < nodes.size(); ++i) {
                times[i] = nodes[i]->peek_time();
            }
        }
        // find earliest entry
        int64_t best_time = std::numeric_limits<int64_t>::max();
        int64_t best_i = -1;
        for (size_t i = 0; i < nodes.size(); i++) {
            if (times[i] && (best_i < 0 || times[i] < best_time)) {
                best_i = i;
                best_time = times[i];
            }
        }
        if (best_i >= 0) {
            bool rv = nodes[best_i]->read_entry();
            if (rv) times[best_i] = nodes[best_i]->peek_time();
            time = *nodes[best_i]->shared_time;
            return rv;
        }
        // we ran out of entries; guess this is good bye
        return false;
    }
};

} // namespace mff

#endif // BITCOIN_MFF_H
