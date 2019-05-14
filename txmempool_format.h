// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXMEMPOOL_FORMAT_H
#define BITCOIN_TXMEMPOOL_FORMAT_H

#include <thread>
#include <mutex>
#include <atomic>

#include <mff.h>
#include <serialize.h>
#include <streams.h>

extern uint64_t skipped_recs;

namespace mff {

class fr_container {
public:
    virtual long tell() = 0;
    virtual seq_t seq_read(bool known) = 0;
    virtual void seq_write(seq_t seq, bool known) = 0;
    virtual void prep_block(uint32_t height) = 0;
};

#define MAX_FR_CONTAINERS 3
extern fr_container* g_fr_ctr[MAX_FR_CONTAINERS];

template <typename Stream, int I>
class fr_adapter: public adapter<Stream> {
public:
    #define DEBUG_SER(args...) // printf(args)
    void serialize_outpoint(Stream& s, const outpoint& o) {
        DEBUG_SER("serializing %s outpoint\n", o.known ? "known" : "unknown");
        DEBUG_SER("o.n=%" PRIu64 "\n", o.n);
        Serialize(s, VARINT(o.n));
        if (o.known) {
            DEBUG_SER("o.seq = %" PRIu64 "\n", o.seq);
            g_fr_ctr[I]->seq_write(o.seq, true);
        } else {
            DEBUG_SER("o.txid = %s\n", o.txid.ToString().c_str());
            Serialize(s, o.txid);
        }
    }
    void deserialize_outpoint(Stream& s, outpoint& o) {
        DEBUG_SER("deserializing %s outpoint\n", o.known ? "known" : "unknown");
        Unserialize(s, VARINT(o.n));
        DEBUG_SER("o.n=%" PRIu64 "\n", o.n);
        if (o.known) {
            o.seq = g_fr_ctr[I]->seq_read(true);
            DEBUG_SER("o.seq = %" PRIu64 "\n", o.seq);
        } else {
            Unserialize(s, o.txid);
            DEBUG_SER("o.txid = %s\n", o.txid.ToString().c_str());
        }
    }
    void serialize_tx(Stream& s, const tx& t) {
        // fprintf(stderr, "serialize %" PRIseq " = %s\n", t.seq, t.id.ToString().c_str());
        long l = g_fr_ctr[I]->tell();
        if (l != (long)t.seq) fprintf(stderr, "\n*** %ld != %" PRIseq "!\n", l, t.seq);
        assert(l == (long)t.seq);
        #undef DEBUG_SER
        #define DEBUG_SER(args...) if (s.debugging) printf(args)
        DEBUG_SER("serializing tx\n");
        DEBUG_SER("- id: %s\n", t.id.ToString().c_str());   // 32
        Serialize(s, t.id);
        DEBUG_SER("- seq: %" PRIu64 "\n", t.seq);
        g_fr_ctr[I]->seq_write(t.seq, false);
        DEBUG_SER("- weight: %" PRIu64 "\n", t.weight);            // 33   40
        Serialize(s, VARINT(t.weight));
        DEBUG_SER("- fee: %" PRIu64 "\n", t.fee);                  // 34   48
        Serialize(s, VARINT(t.fee));
        if (t.fee > 100000000ULL) {
            fprintf(stderr, "unusually high fee %" PRIu64 " for txid %s\n", t.fee, t.id.ToString().c_str());
            fprintf(stderr, "tx = %s\n", t.to_string().c_str());
        }
        DEBUG_SER("- inputs: %" PRIu64 "\n", t.inputs);            // 35   50
        Serialize(s, VARINT(t.inputs));
        for (uint64_t i = 0; i < t.inputs; ++i) {
            DEBUG_SER("--- input #%" PRIu64 "/%" PRIu64 "\n", i+1, t.inputs);
            DEBUG_SER("--- state: %u\n", t.state[i]);
            Serialize(s, t.state[i]);                       // 1
            if (t.state[i] == outpoint::state_coinbase) {
                DEBUG_SER("--- vin: (coinbase)\n");
            }
            if (t.state[i] != outpoint::state_confirmed && t.state[i] != outpoint::state_coinbase) {
                // if (!ser_action.ForRead()) printf("--- vin: %s\n", vin[i].to_string().c_str());
                DEBUG_SER("--- vin: %s\n", t.vin[i].to_string().c_str());
                t.vin[i].serialize(s, this);                // 2    36
            }                                               // 0    36
        }                                                   // 1    37
        // fprintf(stderr, "serialized %" PRIseq " = %s\n", t.seq, t.id.ToString().c_str());
    }
    void deserialize_tx(Stream& s, tx& t) {
        long l = g_fr_ctr[I]->tell();
        DEBUG_SER("deserializing tx\n");
        Unserialize(s, t.id);
        DEBUG_SER("- id: %s\n", t.id.ToString().c_str());
        t.seq = g_fr_ctr[I]->seq_read(false);
        DEBUG_SER("- seq: %" PRIu64 "\n", t.seq);
        Unserialize(s, VARINT(t.weight));
        DEBUG_SER("- weight: %" PRIu64 "\n", t.weight);
        Unserialize(s, VARINT(t.fee));
        DEBUG_SER("- fee: %" PRIu64 "\n", t.fee);
        if (t.fee > 100000000ULL) {
            fprintf(stderr, "unusually high fee %" PRIu64 " for txid %s\n", t.fee, t.id.ToString().c_str());
        }
        Unserialize(s, VARINT(t.inputs));
        DEBUG_SER("- inputs: %" PRIu64 "\n", t.inputs);
        t.state.resize(t.inputs);
        t.vin.resize(t.inputs);
        for (uint64_t i = 0; i < t.inputs; ++i) {
            DEBUG_SER("--- input #%" PRIu64 "/%" PRIu64 "\n", i+1, t.inputs);
            Unserialize(s, t.state[i]);
            DEBUG_SER("--- state: %u\n", t.state[i]);
            if (!(t.state[i] <= outpoint::state_coinbase)) {
                fprintf(stderr, "\nbyte position %ld: t.state[i] is broken for tx %s (seq=%" PRIu64 ")\n", l, t.id.ToString().c_str(), t.seq);
            }
            assert(t.state[i] <= outpoint::state_coinbase);
            if (t.state[i] == outpoint::state_coinbase) {
                t.vin[i] = outpoint::coinbase();
                DEBUG_SER("--- vin: (coinbase) %s\n", t.vin[i].to_string().c_str());
            } else if (t.state[i] != outpoint::state_confirmed) {
                t.vin[i] = outpoint(t.state[i] == outpoint::state_known);
                t.vin[i].deserialize(s, this);
                DEBUG_SER("--- vin: %s\n", t.vin[i].to_string().c_str());
            }
        }
    }
    void serialize_block(Stream& s, const block& b) {
        Serialize(s, b.height);
        g_fr_ctr[I]->prep_block(b.height);
        Serialize(s, b.hash);
        if (b.is_known) return;
        Serialize(s, VARINT(b.count_known));
        for (uint64_t i = 0; i < b.count_known; ++i) {
            g_fr_ctr[I]->seq_write(b.known[i], true);
            // printf("- [%ld] wrote block known %" PRIseq "\n", g_fr_ctr[I]->tell(), b.known[i]);
        }
        Serialize(s, b.unknown);
    }
    void deserialize_block(Stream& s, block& b) {
        Unserialize(s, b.height);
        g_fr_ctr[I]->prep_block(b.height);
        Unserialize(s, b.hash);
        if (b.is_known) return;
        Unserialize(s, VARINT(b.count_known));
        b.known.resize(b.count_known);
        for (uint64_t i = 0; i < b.count_known; ++i) {
            b.known[i] = g_fr_ctr[I]->seq_read(true);
            // printf("- [%ld] read block known %" PRIseq "\n", g_fr_ctr[I]->tell(), b.known[i]);
        }
        Unserialize(s, b.unknown);
    }
};

struct fr_queue {
    bool done = false;
    std::string tag;
    std::atomic<uint32_t> queue_height_goal{0};
    std::atomic<uint32_t> queue_height_done{0};
    std::map<uint32_t, std::vector<seq_t>> frozen_queue;  // invalidated or confirmed queue
    std::map<uint32_t, std::vector<seq_t>> chilled_queue; // discarded (valid) queue
    std::vector<seq_t> purge_queue;                       // to-be-purged sequences
    std::mutex purge_mutex;                               // purge queue mutex
    txs_t* txs;                                           // ref to txs dictionary
};

void fr_queue_processor_f(fr_queue* q);

template<int I>
class mff_fr: public mff, public fr_container, public chain_delegate, public listener_callback {
private:
    fr_queue q;
    std::thread* queue_processor{nullptr};

    constexpr static size_t MAX_BLOCKS = 6; // keep this many blocks
    int64_t lastflush;
    FILE* in_fp;
    CAutoFile in;
    fr_adapter<CAutoFile, I> serializer;
    std::vector<uint256> pending_conf_unknown;
    std::vector<seq_t> pending_conf_known;
    std::set<uint256> pending_import;

    void apply_block(std::shared_ptr<block> b);
    void undo_block_at_height(uint32_t height);
    inline void sync();

    inline void tx_out(bool known, seq_t seq, std::shared_ptr<tx> t, const uint256& txid, uint8_t reason);
    inline void tx_invalid(bool known, seq_t seq, std::shared_ptr<tx> t, const tiny::tx& tref, uint8_t state, const uint256* cause);

    inline void tx_freeze(seq_t seq);
    inline void tx_chill(seq_t seq);
    inline void tx_thaw(seq_t seq);
    inline void update_queues();
    inline void update_queues_for_height(uint32_t height);

    long use_start = 0;
public:
    uint8_t version;
    std::map<uint256,seekable_record> tx_recs;
    std::map<uint256,int64_t> known_txid;
    std::set<uint32_t> discarded_x_blocks_ago[2];
    uint64_t rerecs = 0;
    void link_source(mff* src) override {
        src->chain_del = this;
        src->listener = this;
    }
    uint32_t expected_block_height() override {
        return active_chain.chain.size() == 0 ? 0 : active_chain.height + 1;
    }
    uint32_t blk_tell() override { return expected_block_height(); }

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

    mff_fr(const std::string path = "", bool readonly = true);
    mff_fr(FILE* fp, bool readonly = true);
    ~mff_fr();
    bool read_entry() override;
    int64_t peek_time() override;
    // void write_entry(entry* e) override;
    seq_t claim_seq(const uint256& txid) override;
    void verify_seq(const uint256& txid, seq_t seq) override;
    bool verifying_seq = false;
    void prep_block(uint32_t height) override;
    long tell() override { return in.told; }
    uint256 get_replacement_txid() const;
    uint256 get_invalidated_txid() const;

    seq_t seq_read(bool known) override;
    void seq_write(seq_t seq, bool known) override;

    void flush() override { fflush(in_fp); }

    const std::shared_ptr<tx> register_entry(const tiny::mempool_entry& entry, bool known);

    void add_entry(std::shared_ptr<const tiny::mempool_entry>& entry) override;
    void remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause) override;
    void push_block(int height, uint256 hash, const std::vector<tiny::tx>& txs) override;
    void pop_block(int height) override;

    inline void tx_rec(seqdict_server* source, const tx& x) override;
    inline void tx_in(seqdict_server* source, const tx& x) override;
    inline void tx_out(seqdict_server* source, const tx& x, tx::out_reason_enum reason) override;
    inline void tx_invalid(seqdict_server* source, const tx& x, std::vector<uint8_t> txdata, tx::invalid_reason_enum reason, const uint256* cause = nullptr) override;
    inline void block_confirm(seqdict_server* source, const block& b) override;
    inline void block_unconfirm(seqdict_server* source, uint32_t height) override;
};

} // namespace mff

#endif // BITCOIN_TXMEMPOOL_FORMAT_H
