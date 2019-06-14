#ifndef included_bcq_bitcoin_h_
#define included_bcq_bitcoin_h_

#include <map>

#include <inttypes.h>

#include <cqdb/cq.h>
#include <uint256.h>

#define BITCOIN_SER(T) \
    template<typename Stream> void serialize(Stream& stm, const T& t) { t.Serialize(stm); } \
    template<typename Stream> void deserialize(Stream& stm, T& t) { t.Unserialize(stm); }

extern "C" { void libbcq_is_present(void); } // hello autotools, pleased to meat you

namespace cq {

    BITCOIN_SER(uint256);

} // namespace cq

namespace tiny {

struct mempool_entry;
struct tx;
struct outpoint;

}

namespace bitcoin {

struct outpoint : public cq::serializable {
    uint256 m_txid;
    uint64_t m_n;

    outpoint()                                {                    m_n = 0; }
    outpoint(uint64_t n, const uint256& txid) { m_txid = txid;     m_n = n; }
    outpoint(const outpoint& o)               { m_txid = o.m_txid; m_n = o.m_n; }
    outpoint(const tiny::outpoint& o); // only available with utils.cpp

    static inline outpoint coinbase() { return outpoint(0xffffffff, uint256()); }

    bool operator==(const outpoint& other) const {
        return m_txid == other.m_txid && m_n == other.m_n;
    }

    std::string to_string() const {
        char s[1024];
        // if (m_sid != cq::unknownid) sprintf(s, "outpoint(known seq=%" PRIid ", n=%" PRIu64 ")", m_sid, m_n);
        // else                        sprintf(s, "outpoint(unknown txid=%s, n=%" PRIu64 ")", m_hash.ToString().c_str(), m_n);
        sprintf(s, "outpoint(txid=%s, n=%" PRIu64 ")", m_txid.ToString().c_str(), m_n);
        return s;
    }

    virtual void serialize(cq::serializer* stream) const override {
        *stream << cq::varint(m_n);
    }

    virtual void deserialize(cq::serializer* stream) override {
        m_n = cq::varint::load(stream);
    }
};

struct tx : public cq::object<uint256> {
    static inline std::set<uint256> hashset(const std::set<std::shared_ptr<tx>>& txs) {
        std::set<uint256> rval;
        for (const auto& tx : txs) rval.insert(tx->m_hash);
        return rval;
    }

    // static void identify(const std::map<uint256,cq::id>& map, const std::set<uint256>& source, std::set<uint256>& unknown, std::set<cq::id>& known) {
    //     for (const uint256& txid : source) {
    //         if (map.count(txid)) known.insert(map.at(txid)); else unknown.insert(txid);
    //     }
    // }

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

    uint64_t m_weight;
    uint64_t m_fee;

    std::vector<outpoint> m_vin;
    std::vector<uint64_t> m_vout;

    inline double feerate() const { return double(m_fee)/double(vsize()); }
    inline uint64_t vsize() const { return (m_weight + 3)/4; }
    inline bool spends(const uint256& txid, const cq::id seq, uint32_t& index_out) const {
        for (const auto& prevout : m_vin) {
            if (prevout.m_txid == txid) {
                index_out = prevout.m_n;
                return true;
            }
        }
        return false;
    }

    std::string to_string() const {
        std::string s = std::string("tx(") + m_hash.ToString() + "):";
        for (auto& o : m_vin) {
            s += "\n\t" + o.to_string();
        }
        return s;
    }

    tx(cq::compressor<uint256>* compressor) : cq::object<uint256>(compressor) { location = location_in_mempool; }

    tx(const tx& t) : tx(t.m_compressor) {
        m_sid = t.m_sid;
        m_hash = t.m_hash;
        m_weight = t.m_weight;
        m_fee = t.m_fee;
        for (const outpoint& o : t.m_vin) {
            m_vin.emplace_back(o);
            const outpoint& ocopy = m_vin.back();
            assert(&o != &ocopy);
            assert(o == ocopy);
        }
        m_vout.clear();
        m_vout.insert(m_vout.begin(), t.m_vout.begin(), t.m_vout.end());
    }

    tx(cq::compressor<uint256>* compressor, const tiny::mempool_entry& t);  // only available with utils.cpp
    tx(cq::compressor<uint256>* compressor, const tiny::tx& t);             // only available with utils.cpp

    prepare_for_serialization();
};

class block : public cq::serializable {
public:
    uint32_t m_height;
    uint256 m_hash;
    std::set<uint256> m_txids;
    block(uint32_t height, const uint256& hash, const std::set<uint256>& txids)
    :   m_height(height)
    ,   m_hash(hash)
    ,   m_txids(txids)
    {}

    block(uint32_t height, const uint256& hash, const std::set<std::shared_ptr<tx>>& txs)
    :   block(height, hash, tx::hashset(txs))
    {}

    bool operator==(const block& other) const {
        return m_height == other.m_height
            && m_hash == other.m_hash
            && m_txids == other.m_txids;
    }

    prepare_for_serialization();
};

class chain {
private:
    std::vector<block*> m_blocks;
public:
    ~chain() {
        for (block* b : m_blocks) delete b;
    }
    const block* get_block_for_height(uint32_t height) {
        if (m_tip < height) return nullptr;
        if (m_tip - height >= m_blocks.size()) return nullptr;
        block* b = m_blocks.at(m_tip - height);
        if (b->m_height == height) return b;
        // fallback to binsearch
        bool toohigh = b->m_height > height;
        size_t m = m_tip - height;
        size_t l = toohigh ? 0 : m + 1;
        size_t r = toohigh ? m : m_blocks.size();
        while (r > l) {
            m = l + ((r - l) >> 1);
            b = m_blocks.at(m);
            if (b->m_height > height) {
                r = m;
            } else if (b->m_height < height) {
                l = m + 1;
            } else return b;
        }
        return nullptr;
    }
    const std::vector<block*>& get_blocks() const { return m_blocks; }
    uint32_t m_tip{0};
    void did_confirm(block* blk) {
        m_tip = blk->m_height;
        m_blocks.push_back(blk);
        while (m_blocks.size() > 100) {
            delete m_blocks[0];
            m_blocks.erase(m_blocks.begin());
        }
    }
    void pop_tip() {
        block* tip = m_blocks.size() ? m_blocks.back() : nullptr;
        if (!tip) return;
        delete tip;
        m_blocks.pop_back();
        m_tip = m_blocks.size() ? m_blocks.back()->m_height : 0;
    }
};

/**
 * The MFF delegate is the equivalent of a full node connected to a simulated bitcoin network
 * that receives transactions and blocks from "peers" around it.
 * The exception is that the full node may choose to not purge transactions, as recommendations
 * are made to the delegate directly.
 */
class mff_delegate {
public:
    /**
     * Receive a new (or forgotten) transaction.
     *
     * The transaction is considered to be in the mempool until abandoned or confirmed.
     */
    virtual void receive_transaction(std::shared_ptr<tx> x) =0;

    /**
     * Receive a transition defined by its hash.
     *
     * The transaction is considered to be in the mempool until abandoned or confirmed.
     * The transaction is assumed to be known to the delegate, as it has recently
     * been "received" using the alternate method above.
     */
    virtual void receive_transaction_with_txid(const uint256& txid) =0;

    /**
     * Forget about a transaction corresponding to the given hash.
     *
     * The transaction no longer needs to be in the mempool, and will be
     * considered forgotten; if it is ever addressed again (e.g. by re-adding it
     * to the mempool), it will be given in full.
     * 
     * The reason is one of the reason_ values given in the mff class.
     */
    virtual void forget_transaction_with_txid(const uint256& txid, uint8_t reason) =0;

    /**
     * Discard a transaction corresponding to the given hash.
     * 
     * This differs from the forget counterpart above, in that the transaction is permanently rendered invalid, such as due to a
     * double-spend. (That still means it can be confirmed in the future, however.)

     * Aside from a reason, the raw transaction data as well as an optional cause (offender) is given.
     */
    virtual void discard_transaction_with_txid(const uint256& txid, const std::vector<uint8_t>& rawtx, uint8_t reason, const uint256* cause = nullptr) =0;

    /**
     * The given block was confirmed, and is the new chain tip.
     */
    virtual void block_confirmed(const block& b) =0;

    /**
     * A reorg occurred for the block at the given height; the block one height below the given height is the new chain tip.
     */
    virtual void block_reorged(uint32_t height) =0;

    virtual void iterated(long starting_pos, long resulting_pos) =0;
};

class mff : public cq::chronology<uint256, tx> {
public:
    static const uint8_t cmd_time_set               = 0x00;  // 0b00000
    static const uint8_t cmd_mempool_in             = 0x01;  // 0b00001
    static const uint8_t cmd_mempool_out            = 0x02;  // 0b00010
    static const uint8_t cmd_mempool_invalidated    = 0x03;  // 0b00011
    static const uint8_t cmd_block_mined            = 0x04;  // 0b00100
    static const uint8_t cmd_block_unmined          = 0x05;  // 0b00101
    //                                                            ^^
    //                               "offender known" bit -------'  '------- "offender present" bit
    static const uint8_t cmd_flag_offender_present  = 1 <<3; // 0b01000
    static const uint8_t cmd_flag_offender_known    = 1 <<4; // 0b10000

    static const uint8_t reason_unknown = 0x00;
    static const uint8_t reason_expired = 0x01;
    static const uint8_t reason_sizelimit = 0x02;
    static const uint8_t reason_reorg = 0x03;
    static const uint8_t reason_conflict = 0x04;
    static const uint8_t reason_replaced = 0x05;

    uint64_t m_entries{0};
    chain m_chain;
    mff_delegate* m_delegate;

    mff(const std::string& dbpath, const std::string& prefix = "mff", uint32_t cluster_size = 2016, bool readonly = false)
    : chronology<uint256, tx>(dbpath, prefix, cluster_size, readonly) {
        m_delegate = nullptr;
    }

    static std::string detect_prefix(const std::string& dbpath);

    uint32_t get_height() const { return m_chain.m_tip; }

    //////////////////////////////////////////////////////////////////////////////////////
    // Writing
    //

    void unconfirm_tip(long timestamp) {
        push_event(timestamp, cmd_block_unmined);
        *m_file << m_chain.m_tip;
        m_chain.pop_tip();
    }

    void confirm_block(long timestamp, uint32_t height, const uint256& hash, const std::set<std::shared_ptr<tx>>& txs) {
        ++m_entries;
        if (m_reg.m_tip < height - 1) begin_segment(height - 1);
        while (m_chain.m_tip && m_chain.m_tip >= height) unconfirm_tip(timestamp);
        // note: this does not deal with invalidating txs which are double spends; that has to be handled
        // by the caller
        push_event(timestamp, cmd_block_mined, txs);
        hash.Serialize(*m_file);
        *m_file << height;
        m_chain.did_confirm(new block(height, hash, txs));
        if (m_reg.m_tip < height) begin_segment(height);
        CHRON_DOT(this);
    }

    void tx_entered(long timestamp, std::shared_ptr<tx> x) {
        ++m_entries;
        push_event(timestamp, cmd_mempool_in, x, false /* do not refer -- record entire object, not its hash, if unknown */);
        CHRON_DOT(this);
    }

    void tx_left(long timestamp, std::shared_ptr<tx> x, uint8_t reason, std::shared_ptr<tx> offender = nullptr) {
        ++m_entries;
        bool offender_known = offender.get() && m_references.count(offender->m_hash);
        uint8_t cmd = cmd_mempool_out | (offender.get() ? cmd_flag_offender_present : 0) | (offender_known ? cmd_flag_offender_known : 0);
        push_event(timestamp, cmd, x);
        *m_file << reason;
        OBREF(offender_known, offender);
        CHRON_DOT(this);
    }

    void tx_discarded(long timestamp, std::shared_ptr<tx> x, const std::vector<uint8_t>& rawtx, uint8_t reason, std::shared_ptr<tx> offender = nullptr) {
        ++m_entries;
        bool offender_known = offender.get() && m_references.count(offender->m_hash);
        uint8_t cmd = cmd_mempool_invalidated | (offender.get() ? cmd_flag_offender_present : 0) | (offender_known ? cmd_flag_offender_known : 0);
        push_event(timestamp, cmd, x);
        *m_file << reason;
        OBREF(offender_known, offender);
        *m_file << rawtx;
        CHRON_DOT(this);
    }

    //////////////////////////////////////////////////////////////////////////////////////
    // Reading
    //

    inline bool iterate() { return registry_iterate(m_file); }

    bool registry_iterate(cq::file* file) override {
        uint8_t cmd;
        bool known;
        auto pos = m_file->tell();
        std::string f = m_file->get_path();
        if (!pop_event(cmd, known)) return false;
        if (m_current_time > 1600000000) {
            fprintf(stderr, "invalid time!\n");
            assert(0);
        }
        if (f != m_file->get_path()) { pos = m_file->tell() - 1; }
        uint8_t no_offender_cmd = cmd & 0x07;

        std::shared_ptr<tx> x = std::make_shared<tx>(this);

        try {
            switch (no_offender_cmd) {
            case cmd_time_set: break; // nothing needs to be done; the time update has already happened

            case cmd_mempool_in: {
                if (known) {
                    auto ref = pop_reference();
                    const uint256& txid = m_dictionary.at(ref)->m_hash;
                    if (m_delegate) m_delegate->receive_transaction_with_txid(txid);
                } else {
                    auto o = pop_object();
                    if (m_delegate) m_delegate->receive_transaction(o);
                }
            } break;

            case cmd_mempool_out: {
                bool offender_present = (cmd & cmd_flag_offender_present) > 0;
                bool offender_known = (cmd & cmd_flag_offender_known) > 0;
                uint256 txid = FERBO(known, txid);
                uint8_t reason;
                *m_file >> reason;
                // uint256* offender_hash = nullptr;
                uint256 offender_hash_rv;
                if (offender_present) {
                    offender_hash_rv = FERBO(offender_known, offender_hash_rv);
                    // offender_hash = &offender_hash_rv;
                }
                if (m_delegate) m_delegate->forget_transaction_with_txid(txid, reason);
            } break;

            case cmd_mempool_invalidated: {
                bool offender_present = (cmd & cmd_flag_offender_present) > 0;
                bool offender_known = (cmd & cmd_flag_offender_known) > 0;
                uint256 txid = FERBO(known, txid);
                uint8_t reason;
                std::vector<uint8_t> rawtx;
                *m_file >> reason;
                uint256* offender_hash = nullptr;
                uint256 offender_hash_rv;
                if (offender_present) {
                    offender_hash_rv = FERBO(offender_known, offender_hash_rv);
                    offender_hash = &offender_hash_rv;
                }
                *m_file >> rawtx;
                if (m_delegate) m_delegate->discard_transaction_with_txid(txid, rawtx, reason, offender_hash);
            } break;

            case cmd_block_mined: {
                uint256 hash;
                auto pos = m_file->tell();
                uint32_t height;
                std::set<uint256> tx_hashes;
                pop_reference_hashes(tx_hashes);
                hash.Unserialize(*m_file);
                *m_file >> height;
                block* b = new block(height, hash, tx_hashes);
                m_chain.did_confirm(b);
                // fprintf(stderr, "- %ld: mined %u [%zu]\n", pos, height, m_chain.get_blocks().size());
                if (m_delegate) m_delegate->block_confirmed(*b);
            } break;

            case cmd_block_unmined: {
                uint32_t unmined_height;
                auto pos = m_file->tell();
                *m_file >> unmined_height;
                // fprintf(stderr, "- %ld: unmining %u [%zu]\n", pos, unmined_height, m_chain.get_blocks().size());
                // the assert below is not valid in cases where the reorg'd block is before the recording began
                // assert(unmined_height == m_chain.m_tip);
                m_chain.pop_tip();
                if (m_delegate) m_delegate->block_reorged(unmined_height);
            } break;

            default:
                fprintf(stderr, "invalid command: %02x\n", no_offender_cmd);
                throw std::runtime_error("invalid command");
            }
        } catch (const cq::io_error& err) {
            if (m_readonly) {
                // for readonly mode, we don't mind if the last entry is broken, as it may be written to
                // so we just return false here
                return false;
            }
            // if readwrite mode, though, we want to die
            throw err;
        }
        assert(pos < m_file->tell());
        if (m_delegate) m_delegate->iterated(pos, m_file->tell());
        return true;
    }
};

static const std::string reasons[] = {"unknown", "expired", "sizelimit", "reorg", "conflict", "replaced", "???????????????????"};
inline const std::string& reason_string(uint8_t reason) {
    return reasons[reason < 6 ? reason : 6];
}

static const std::string cmds[] = {"time_set", "mempool_in", "mempool_out", "mempool_invalidated", "block_mined", "block_unmined", "?????????????????????"};
inline const std::string& cmd_string(uint8_t cmd) {
    cmd &= 0x07;
    return cmds[cmd < 6 ? cmd : 6];
}

class mff_analyzer : public mff_delegate {
public:
    bool enable_touchmap = false;
    mutable std::map<uint256,uint32_t> touchmap; // requires above bool set to true and calls to populate_touched_txids() after every iteration
    uint64_t total_bytes{0};
    uint64_t total_txrecs{0};
    uint64_t total_txrec_bytes{0};
    std::map<uint8_t,size_t> usage;
    std::map<uint8_t,size_t> count;
    std::vector<uint256> last_txids;
    std::vector<std::shared_ptr<tx>> last_txs;
    std::vector<uint8_t> last_rawtx;
    uint8_t last_command;
    uint8_t last_reason;
    uint256 last_cause;
    const block* last_mined_block;
    uint32_t last_unmined_height;
    inline void set(uint8_t new_command) { last_command = new_command; last_txids.clear(); last_txs.clear(); last_rawtx.clear(); last_cause.SetNull(); last_mined_block = nullptr; }
    inline void set(uint8_t new_command, std::shared_ptr<tx> new_tx) { set(new_command); last_txs.push_back(new_tx); last_txids.push_back(new_tx->m_hash); }
    inline void set(uint8_t new_command, const uint256& new_txid) { set(new_command); last_txids.push_back(new_txid); }

    virtual void receive_transaction(std::shared_ptr<tx> x) override;
    virtual void receive_transaction_with_txid(const uint256& txid) override;
    virtual void forget_transaction_with_txid(const uint256& txid, uint8_t reason) override;
    virtual void discard_transaction_with_txid(const uint256& txid, const std::vector<uint8_t>& rawtx, uint8_t reason, const uint256* cause = nullptr) override;
    virtual void block_confirmed(const block& b) override;
    virtual void block_reorged(uint32_t height) override;

    virtual void iterated(long starting_pos, long resulting_pos) override;

    void populate_touched_txids(std::set<uint256>& txids) const;

};

} // namespace bitcoin

#endif // included_bcq_bitcoin_h_
