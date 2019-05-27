#include <memory>
#include <bcq/bitcoin.h>
#include <cqdb/uint256.h>

#ifndef REQUIRE
#   define REQUIRE assert
#endif

static inline uint256 random_hash() {
    uint256 hash;
    cq::randomize(hash.begin(), 32);
    return hash;
}

static inline uint8_t random_byte()  { uint8_t u8; cq::randomize(&u8, 1); return u8; }
static inline uint16_t random_word() { uint16_t u16; cq::randomize(&u16, 2); return u16; }

static inline std::shared_ptr<bitcoin::tx> make_random_tx() {
    auto t = std::make_shared<bitcoin::tx>();
    t->m_hash = random_hash();
    auto vin_sz = random_byte() % 10;
    for (auto i = 0; i < vin_sz; ++i) {
        t->m_vin.emplace_back(random_byte(), random_hash());
    }
    auto vout_sz = random_byte() % 10;
    for (auto i = 0; i < vout_sz; ++i) {
        t->m_vout.push_back(random_word());
    }
    return t;
}

static inline std::set<std::shared_ptr<bitcoin::tx>> random_txs() {
    uint8_t u8 = random_byte();
    std::set<std::shared_ptr<bitcoin::tx>> rv;
    for (uint8_t i = 0; i < u8; ++i) {
        rv.insert(make_random_tx());
    }
    return rv;
}

static const std::string default_dbpath = "/tmp/cq-bitcoin-mff";
static const uint256 some_hash = uint256S("0102030405060708090a0b0c0d0e0f1011121314151617181920212223242526");

inline std::shared_ptr<bitcoin::mff> open_mff(bitcoin::mff_delegate* delegate, const std::string& dbpath = default_dbpath, bool reset = false) {
    if (reset) cq::rmdir_r(dbpath);
    auto rv = std::make_shared<bitcoin::mff>(delegate, dbpath);
    rv->load();
    return rv;
}

inline std::shared_ptr<bitcoin::mff> new_mff(bitcoin::mff_delegate* delegate, const std::string& dbpath = default_dbpath) {
    return open_mff(delegate, dbpath, true);
}

inline size_t mff_file_count(const std::string& dbpath = default_dbpath) {
    std::vector<std::string> l;
    cq::listdir(dbpath, l);
    return l.size();
}

struct tracker {
    std::shared_ptr<bitcoin::mff> mff;
    std::vector<bitcoin::block*> blocks;
    std::vector<std::shared_ptr<bitcoin::tx>> txs;
    std::map<uint256,std::shared_ptr<bitcoin::tx>> dict;
    tracker(std::shared_ptr<bitcoin::mff> mff_) : mff(mff_) {}
    tracker& operator+=(const std::shared_ptr<bitcoin::tx>& tx) {
        txs.push_back(tx);
        dict[tx->m_hash] = tx;
        return *this;
    }
    tracker& operator-=(const std::shared_ptr<bitcoin::tx>& tx) {
        auto it = std::find(txs.begin(), txs.end(), tx);
        if (it != txs.end()) txs.erase(it);
        // dict.erase(tx->m_hash); // we do not erase from dict, because we want the txs to remain forever (we use at reorgs)
        return *this;
    }
    size_t size() const { return txs.size(); }
    std::shared_ptr<bitcoin::tx>& operator[](size_t index) {
        return txs.at(index);
    }
    std::shared_ptr<bitcoin::tx>& operator[](const uint256& hash) {
        return dict.at(hash);
    }
    std::shared_ptr<bitcoin::tx> sample_pop() {
        assert(txs.size() > 0);
        size_t sz = random_word() % txs.size();
        auto tx = txs.at(sz);
        operator-=(tx);
        return tx;
    }
    std::shared_ptr<bitcoin::tx>& sample(std::shared_ptr<bitcoin::tx>* except = nullptr) {
        assert(txs.size() > 0);
        for (;;) {
            auto& v = txs.at(random_word() % txs.size());
            if (except && v == *except) continue;
            return v;
        }
    }
    bitcoin::block* mine_block(long timestamp, uint32_t height) {
        std::set<std::shared_ptr<bitcoin::tx>> confirmed;
        size_t count = random_word() % 1000; // block tx count
        if (count > txs.size()) count = txs.size();
        for (size_t j = 0; j < count; ++j) {
            if (random_byte() < 200) {
                confirmed.insert(sample_pop());
            } else {
                confirmed.insert(make_random_tx());
            }
        }
        bitcoin::block* b = new bitcoin::block(height, random_hash(), confirmed);
        blocks.push_back(b);
        mff->confirm_block(timestamp, height, b->m_hash, confirmed);
        return b;
    }
    void unconfirm_tip(long timestamp) {
        auto b = blocks.back();
        for (auto x : b->m_txids) {
            if (dict.count(x)) txs.push_back(dict.at(x));
        }
        blocks.pop_back();
        mff->unconfirm_tip(timestamp);
        delete b;
    }
};

struct record {
    std::vector<record*>* records_ptr{nullptr};
    record* m_next{nullptr};
    uint8_t m_command{0xff};
    virtual std::string to_string() const {
        char buf[3];
        sprintf(buf, "%02x", m_command);
        return std::string("record ") + buf;
    }
    virtual void check(bitcoin::mff_analyzer* azr) {
        REQUIRE(azr->last_command == m_command);
    }
    record* push(record* next) {
        next->records_ptr = records_ptr;
        records_ptr->push_back(next);
        m_next = next;
        return m_next;
    }
};

struct record_mempool_in : public record {
    std::shared_ptr<bitcoin::tx> m_tx;
    record_mempool_in(std::shared_ptr<bitcoin::tx> tx) : m_tx(tx) {
        m_command = bitcoin::mff::cmd_mempool_in;
    }
    virtual std::string to_string() const override {
        return "record mempool_in(" + m_tx->m_hash.ToString() + ")";
    }
    virtual void check(bitcoin::mff_analyzer* azr) override {
        record::check(azr);
        REQUIRE(azr->last_txs.size() == 1);
        REQUIRE(*azr->last_txs.back() == *m_tx);
    }
};

struct record_mempool_out : public record {
    uint256 m_txid;
    uint8_t m_reason;
    record_mempool_out(uint256 txid, uint8_t reason) : m_txid(txid), m_reason(reason) {
        m_command = bitcoin::mff::cmd_mempool_out;
    }
    virtual std::string to_string() const override {
        return "record mempool_out(" + bitcoin::reason_string(m_reason) + " " + m_txid.ToString() + ")";
    }
    virtual void check(bitcoin::mff_analyzer* azr) override {
        record::check(azr);
        REQUIRE(azr->last_txids.size() == 1);
        REQUIRE(azr->last_txids.back() == m_txid);
        REQUIRE(azr->last_reason == m_reason);
    }
};

struct record_mempool_invalidated : public record {
    uint256 m_txid, m_offender_txid;
    uint8_t m_reason;
    bool m_offender_present, m_offender_known;
    std::vector<uint8_t> m_rawtx;
    record_mempool_invalidated(uint256 txid, std::vector<uint8_t> rawtx, uint8_t reason) : m_txid(txid), m_reason(reason), m_offender_known(false), m_rawtx(rawtx) {
        m_command = bitcoin::mff::cmd_mempool_invalidated;
    }
    record_mempool_invalidated(uint256 txid, std::vector<uint8_t> rawtx, uint256 offender_txid, uint8_t reason, bool offender_known)
    :   m_txid(txid)
    ,   m_offender_txid(offender_txid)
    ,   m_reason(reason)
    ,   m_offender_present(true)
    ,   m_offender_known(offender_known)
    ,   m_rawtx(rawtx) {
        m_command = bitcoin::mff::cmd_mempool_invalidated;
    }
    virtual std::string to_string() const override {
        return
            "record mempool_invalidated("
        +   bitcoin::reason_string(m_reason)
        +   " "
        +   m_txid.ToString()
        +   (m_offender_present ? ", offender = " + m_offender_txid.ToString() : "")
        +   ")";
    }

    virtual void check(bitcoin::mff_analyzer* azr) override {
        record::check(azr);
        REQUIRE(azr->last_rawtx == m_rawtx);
        REQUIRE(azr->last_txids.size() == 1);
        REQUIRE(azr->last_txids.back() == m_txid); // TODO: offender?
        if (m_offender_present) {
            REQUIRE(azr->last_cause == m_offender_txid);
        }
        REQUIRE(azr->last_reason == m_reason);
        // TODO: m_offender_known validation
    }
};

struct record_block_mined : public record {
    uint256 m_hash;
    uint32_t m_height;
    std::set<uint256> m_txids;
    record_block_mined(uint256 hash, uint32_t height, const std::set<uint256>& txids)
    :   m_hash(hash)
    ,   m_height(height)
    ,   m_txids(txids) {
        m_command = bitcoin::mff::cmd_block_mined;
    }
    virtual std::string to_string() const override {
        return "record block_mined(" + std::to_string(m_height) + "=" + m_hash.ToString() + ")";
    }

    virtual void check(bitcoin::mff_analyzer* azr) override {
        record::check(azr);
        REQUIRE(azr->last_mined_block->m_hash == m_hash);
        REQUIRE(azr->last_mined_block->m_height == m_height);
        REQUIRE(azr->last_mined_block->m_txids == m_txids);
    }
};

struct record_block_unmined : public record {
    uint32_t m_height;
    record_block_unmined(uint32_t height) : m_height(height) {
        m_command = bitcoin::mff::cmd_block_unmined;
    }
    virtual std::string to_string() const override {
        return "record block_unmined(" + std::to_string(m_height) + ")";
    }

    virtual void check(bitcoin::mff_analyzer* azr) override {
        record::check(azr);
        REQUIRE(azr->last_unmined_height == m_height);
    }
};

#define REC(args...) rec = rec->push(new args)
// #define REC(args...) { rec = rec->push(new args); fprintf(stderr, "\t%s\n", rec->to_string().c_str()); }
