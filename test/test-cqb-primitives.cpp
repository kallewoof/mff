#include "catch.hpp"

#include <bcq/bitcoin.h>

#include "helpers.h"

TEST_CASE("Outpoint", "[outpoints]") {
    using bitcoin::outpoint;
    // struct outpoint : public cq::object {
    //     enum state_t: uint8_t {
    //         state_unknown   = 0,
    //         state_known     = 1,
    //         state_confirmed = 2,
    //         state_coinbase  = 3,
    //     };

    //     state_t m_state;
    //     uint64_t m_n;

    //     outpoint()                                : cq::object()                  { m_n = 0; }
    //     outpoint(uint64_t n, cq::id sid)          : cq::object(sid)               { m_n = n; }
    //     outpoint(uint64_t n, const uint256& txid) : cq::object(txid)              { m_n = n; }
    //     outpoint(const outpoint& o)               : cq::object(o.m_sid, o.m_hash) { m_n = o.m_n; }

    SECTION("construction") {
        {
            outpoint op(123, 456);
            REQUIRE(op.m_n == 123);
            REQUIRE(op.m_sid == 456);
            REQUIRE(op.m_hash.IsNull());
        }
        {
            auto hash = random_hash();
            outpoint op(123, hash);
            REQUIRE(op.m_n == 123);
            REQUIRE(op.m_sid == cq::unknownid);
            REQUIRE(op.m_hash == hash);
            outpoint op2(op);
            REQUIRE(op == op2);
        }
    }

    //     void set(const uint256& txid) {
    //         m_sid = cq::nullid;
    //         m_hash = txid;
    //     }

    //     void set(cq::id sid) {
    //         m_sid = sid;
    //         m_hash.SetNull();
    //     }

    SECTION("setting") {
        {
            auto hash = random_hash();
            outpoint op(123, 456);
            op.set(789);
            REQUIRE(op.m_sid == 789);
            REQUIRE(op.m_hash.IsNull());
            op.set(hash);
            REQUIRE(op.m_sid == cq::unknownid);
            REQUIRE(op.m_hash == hash);
        }
        {
            auto hash = random_hash();
            outpoint op(123, hash);
            op.set(789);
            REQUIRE(op.m_sid == 789);
            REQUIRE(op.m_hash.IsNull());
            op.set(hash);
            REQUIRE(op.m_sid == cq::unknownid);
            REQUIRE(op.m_hash == hash);
        }
    }

    //     static inline outpoint coinbase() { return outpoint(0xffffffff, uint256()); }

    SECTION("coinbase") {
        outpoint cbop = outpoint::coinbase();
        REQUIRE(cbop.m_hash.IsNull());
        REQUIRE(cbop.m_n == 0xffffffff);
        REQUIRE(cbop.m_sid == cq::unknownid);
        REQUIRE(cbop == outpoint::coinbase());
    }

    //     uint64_t get_n()          const { return m_n; }

    SECTION("get_n()") {
        outpoint op(123, 456);
        const outpoint const_op(op);
        REQUIRE(const_op.get_n() == 123);
    }

    //     std::string to_string() const {
    //         char s[1024];
    //         if (m_sid != cq::nullid) sprintf(s, "outpoint(known seq=%" PRIid ", n=%" PRIu64 ")", m_sid, m_n);
    //         else                     sprintf(s, "outpoint(unknown txid=%s, n=%" PRIu64 ")", m_hash.ToString().c_str(), m_n);
    //         return s;
    //     }

    //     virtual void serialize(cq::serializer* stream) const override {
    //         cq::varint(m_n).serialize(stream);
    //     }

    //     virtual void deserialize(cq::serializer* stream) override {
    //         m_n = cq::varint::load(stream);
    //     }

    SECTION("serialization") {
        auto hash = random_hash();
        outpoint op1(123, 2);
        cq::chv_stream stream;
        op1.serialize(&stream);
        stream.seek(0, SEEK_SET);
        outpoint op1x(0, 2);
        op1x.deserialize(&stream);
        REQUIRE(op1.to_string() == op1x.to_string());
        REQUIRE(op1 == op1x);
    }

    // };
}

TEST_CASE("Transactions", "[txs]") {

    // struct tx : public cq::object {
    //     static inline std::set<uint256> hashset(const std::set<std::shared_ptr<tx>>& txs) {
    //         std::set<uint256> rval;
    //         for (const auto& tx : txs) rval.insert(tx->m_hash);
    //         return rval;
    //     }

    SECTION("hashset method") {
        auto ob1 = make_random_tx();
        auto ob2 = make_random_tx();
        std::set<uint256> hashes{ob1->m_hash, ob2->m_hash};
        REQUIRE(std::set<uint256>{} == bitcoin::tx::hashset(std::set<std::shared_ptr<bitcoin::tx>>{}));
        REQUIRE(hashes == bitcoin::tx::hashset(std::set<std::shared_ptr<bitcoin::tx>>{ob1, ob2}));
        REQUIRE(hashes == bitcoin::tx::hashset(std::set<std::shared_ptr<bitcoin::tx>>{ob2, ob1}));
    }

    //     // static void identify(const std::map<uint256,cq::id>& map, const std::set<uint256>& source, std::set<uint256>& unknown, std::set<cq::id>& known) {
    //     //     for (const uint256& txid : source) {
    //     //         if (map.count(txid)) known.insert(map.at(txid)); else unknown.insert(txid);
    //     //     }
    //     // }

    //     enum location_enum: uint8_t {
    //         location_in_mempool = 0,
    //         location_confirmed  = 1,
    //         location_discarded  = 2,
    //         location_invalid    = 3,
    //     } location;

    //     enum out_reason_enum: uint8_t {
    //         out_reason_low_fee    = 0,
    //         out_reason_age_expiry = 1,
    //         out_reason_unknown    = 2,
    //     } out_reason;

    //     enum invalid_reason_enum: uint8_t {
    //         invalid_rbf_bumped = 0,
    //         invalid_doublespent = 1,
    //         invalid_reorg = 2,
    //         invalid_unknown = 3,
    //     } invalid_reason;

    //     uint64_t m_weight;
    //     uint64_t m_fee;

    //     std::vector<outpoint> m_vin;
    //     std::vector<uint64_t> m_amounts; // memory only

    //     inline double feerate() const { return double(m_fee)/double(vsize()); }
    //     inline uint64_t vsize() const { return (m_weight + 3)/4; }
    //     inline bool spends(const uint256& txid, const cq::id seq, uint32_t& index_out) const {
    //         for (const auto& prevout : m_vin) {
    //             if (prevout.m_sid == seq || prevout.m_hash == txid) {
    //                 index_out = prevout.get_n();
    //                 return true;
    //             }
    //         }
    //         return false;
    //     }

    //     std::string to_string() const {
    //         std::string s = strprintf("tx(%s):", m_hash.ToString());
    //         for (auto& o : m_vin) {
    //             s += "\n\t" + (o.m_state == outpoint::state_confirmed ? "<found in block>" : o.to_string());
    //         }
    //         return s;
    //     }

    //     tx() : cq::object() { location = location_in_mempool; }

    //     tx(const tx& t) : tx() {
    //         m_sid = t.m_sid;
    //         m_hash = t.m_hash;
    //         m_weight = t.m_weight;
    //         m_fee = t.m_fee;
    //         for (const outpoint& o : t.m_vin) {
    //             m_vin.emplace_back(o);
    //             const outpoint& ocopy = m_vin.back();
    //             assert(&o != &ocopy);
    //             assert(o == ocopy);
    //         }
    //         for (uint64_t a : t.m_amounts) {
    //             m_amounts.push_back(a);
    //         }
    //     }

    //     prepare_for_serialization();

    SECTION("serializing") {
        cq::chv_stream stream;
        auto ob = make_random_tx();
        ob->serialize(&stream);
        stream.seek(0, SEEK_SET);
        bitcoin::tx ob2;
        ob2.deserialize(&stream);
        REQUIRE(*ob == ob2);
    }

    // };
}

TEST_CASE("Blocks", "[blocks]") {
    // class block : cq::serializable {
    // public:
    //     uint32_t m_height;
    //     uint256 m_hash;
    //     std::set<uint256> m_txids;
    //     prepare_for_serialization();
    //     block(uint32_t height, const uint256& hash, const std::set<uint256>& txids)
    //         : m_height(height), m_hash(hash), m_txids(txids) {}
    //     block(uint32_t height, const uint256& hash, const std::set<std::shared_ptr<tx>>& txs)
    //         : block(height, hash, tx::hashset(txs)) {}
    // };

    SECTION("construction") {
        auto ob = make_random_tx();
        {
            bitcoin::block b(123, some_hash, std::set<uint256>{ob->m_hash});
            REQUIRE(b.m_height == 123);
            REQUIRE(b.m_hash == some_hash);
            REQUIRE(b.m_txids.size() == 1);
            REQUIRE(*b.m_txids.begin() == ob->m_hash);
        }
        {
            bitcoin::block b(123, some_hash, std::set<std::shared_ptr<bitcoin::tx>>{ob});
            REQUIRE(b.m_height == 123);
            REQUIRE(b.m_hash == some_hash);
            REQUIRE(b.m_txids.size() == 1);
            REQUIRE(*b.m_txids.begin() == ob->m_hash);
        }
    }
}

TEST_CASE("Chain", "[chain]") {
    // class chain {
    // public:
    //     ~chain() {
    //         for (block* b : m_blocks) delete b;
    //     }
    //     uint32_t m_tip{0};
    //     std::vector<block*> m_blocks;
    //     void did_confirm(block* blk) {
    //         m_tip = blk->m_height;
    //         m_blocks.push_back(blk);
    //     }

    SECTION("construction") {
        bitcoin::chain* chain = new bitcoin::chain();
        REQUIRE(chain->m_tip == 0);
        REQUIRE(chain->get_blocks().size() == 0);
        chain->did_confirm(new bitcoin::block(123, some_hash, random_txs()));
        REQUIRE(chain->m_tip == 123);
        delete chain;
    }

    //     void pop_tip() {
    //         block* tip = m_blocks.size() ? m_blocks.back() : nullptr;
    //         if (!tip) return;
    //         delete tip;
    //         m_blocks.pop_back();
    //         m_tip = m_blocks.size() ? m_blocks.back()->m_height : 0;
    //     }
    // };

    SECTION("reorgs should be dealt with appropriately") {
        bitcoin::chain chain;
        std::vector<bitcoin::block*> blocks;
        for (int i = 500000; i < 500010; ++i) {
            auto b = new bitcoin::block(i, random_hash(), random_txs());
            blocks.push_back(b);
            chain.did_confirm(b);
            REQUIRE(chain.m_tip == i);
            REQUIRE(*b == *chain.get_blocks().back());
        }
        for (int i = 500008; i >= 500000; --i) {
            chain.pop_tip();
            blocks.pop_back();
            REQUIRE(chain.m_tip == i);
            REQUIRE(*blocks.back() == *chain.get_blocks().back());
        }
        chain.pop_tip();
        blocks.pop_back();
        REQUIRE(blocks.size() == 0);
        REQUIRE(chain.m_tip == 0);
        for (int reorg_cap = 1; reorg_cap < 10; ++reorg_cap) {
            for (int i = 500000; i < 500010; ++i) {
                int rcap = i - reorg_cap;
                if (rcap < 500000) rcap = 500000;
                {
                    auto b = new bitcoin::block(i, random_hash(), random_txs());
                    blocks.push_back(b);
                    chain.did_confirm(b);
                    REQUIRE(chain.m_tip == i);
                    REQUIRE(*b == *chain.get_blocks().back());
                }
                for (int j = i - 1; j >= rcap; --j) {
                    chain.pop_tip();
                    blocks.pop_back();
                    REQUIRE(chain.m_tip == j);
                    REQUIRE(*blocks.back() == *chain.get_blocks().back());
                }
                for (int j = rcap; j <= i; ++j) {
                    auto b = new bitcoin::block(j, random_hash(), random_txs());
                    blocks.push_back(b);
                    chain.did_confirm(b);
                    REQUIRE(chain.m_tip == j);
                    REQUIRE(*b == *chain.get_blocks().back());
                }
                REQUIRE(chain.m_tip == i);
            }
        }
    }

}
