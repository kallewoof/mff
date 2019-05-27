#include "catch.hpp"

#include <cq/bitcoin.h>

#include "helpers.h"

TEST_CASE("mff", "[mff]") {

    // class mff : public cq::chronology<tx> {
    // public:
    //     static const uint8_t cmd_time_set               = 0x00;  // 0b00000
    //     static const uint8_t cmd_mempool_in             = 0x01;  // 0b00001
    //     static const uint8_t cmd_mempool_out            = 0x02;  // 0b00010
    //     static const uint8_t cmd_mempool_invalidated    = 0x03;  // 0b00011
    //     static const uint8_t cmd_block_mined            = 0x04;  // 0b00100
    //     static const uint8_t cmd_block_unmined          = 0x05;  // 0b00101
    //     //                                                            ^^
    //     //                               "offender known" bit -------'  '------- "offender present" bit
    //     static const uint8_t cmd_flag_offender_present  = 1 <<3; // 0b01000
    //     static const uint8_t cmd_flag_offender_known    = 1 <<4; // 0b10000

    //     static const uint8_t reason_unknown = 0x00;
    //     static const uint8_t reason_expired = 0x01;
    //     static const uint8_t reason_sizelimit = 0x02;
    //     static const uint8_t reason_reorg = 0x03;
    //     static const uint8_t reason_conflict = 0x04;
    //     static const uint8_t reason_replaced = 0x05;

    //     chain m_chain;
    //     mff_delegate* m_delegate{nullptr};

    //     mff(const std::string& dbpath, const std::string& prefix = "mff", uint32_t cluster_size = 2016)
    //     : chronology<tx>(dbpath, prefix, cluster_size) {}

    SECTION("construction") {
        bitcoin::mff_analyzer azr;
        auto mff = new_mff(&azr);
        REQUIRE(mff->m_delegate == &azr);
        REQUIRE(mff->m_chain.m_tip == 0);
    }

    //     void tx_entered(long timestamp, std::shared_ptr<tx> x) {
    //         push_event(timestamp, cmd_mempool_in, x, false /* do not refer -- record entire object, not its hash, if unknown */);
    //     }

    SECTION("tx_entered") {
        long pos;
        bitcoin::mff_analyzer azr;
        auto ob = make_random_tx();
        {
            auto mff = new_mff(&azr);
            mff->begin_segment(500000);
            pos = mff->m_file->tell();
            mff->tx_entered(1558067026, ob);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(ob->m_sid == mff->m_references.at(ob->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(*ob == *mff->m_dictionary.at(ob->m_sid));
        }
        {
            auto mff = open_mff(&azr);
            // opening should automatically read up to the end and load in any objects
            // in the process, which means we should from the get-go have 'ob' again
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(ob->m_sid == mff->m_references.at(ob->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(*ob == *mff->m_dictionary.at(ob->m_sid));
            // now rewind and read again
            mff->m_file->seek(pos, SEEK_SET);
            mff->current_time = 0;
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob);
            REQUIRE(!mff->iterate());
        }
    }

    //     void tx_left(long timestamp, std::shared_ptr<tx> x, uint8_t reason, std::shared_ptr<tx> offender = nullptr) {
    //         bool offender_known = offender.get() && m_references.count(offender->m_hash);
    //         uint8_t cmd = cmd_mempool_out | (offender.get() ? cmd_flag_offender_present : 0) | (offender_known ? cmd_flag_offender_known : 0);
    //         push_event(timestamp, cmd, x);
    //         *m_file << reason;
    //         OBREF(offender_known, offender);
    //     }

    SECTION("tx_left") {
        long pos;
        bitcoin::mff_analyzer azr;
        auto ob = make_random_tx();
        auto ob2 = make_random_tx();
        {
            auto mff = new_mff(&azr);
            mff->begin_segment(500000);
            pos = mff->m_file->tell();
            mff->tx_entered(1558067026, ob);
            REQUIRE(mff->current_time == 1558067026);
            mff->tx_entered(1558067026, ob2);
            REQUIRE(mff->current_time == 1558067026);
            mff->tx_left(1558067027, ob, bitcoin::mff::reason_sizelimit);
            REQUIRE(mff->current_time == 1558067027);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(mff->m_references.count(ob2->m_hash));
            REQUIRE(ob->m_sid  == mff->m_references.at(ob->m_hash));
            REQUIRE(ob2->m_sid == mff->m_references.at(ob2->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(mff->m_dictionary.count(ob2->m_sid));
            REQUIRE(*ob  == *mff->m_dictionary.at(ob->m_sid));
            REQUIRE(*ob2 == *mff->m_dictionary.at(ob2->m_sid));
            // re-entering; it is known now, which should be reflected in serialization
            auto prein = mff->m_file->tell();
            mff->tx_entered(1558067028, ob);
            auto len = mff->m_file->tell() - prein;
            REQUIRE(len <= 3); // a maximum of 3 bytes to refer to ob, as it is relatively recent
        }
        {
            auto mff = open_mff(&azr);
            REQUIRE(mff->current_time == 1558067028);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(mff->m_references.count(ob2->m_hash));
            REQUIRE(ob->m_sid  == mff->m_references.at(ob->m_hash));
            REQUIRE(ob2->m_sid == mff->m_references.at(ob2->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(mff->m_dictionary.count(ob2->m_sid));
            REQUIRE(*ob  == *mff->m_dictionary.at(ob->m_sid));
            REQUIRE(*ob2 == *mff->m_dictionary.at(ob2->m_sid));
            // now rewind and read again
            mff->m_file->seek(pos, SEEK_SET);
            mff->current_time = 0;
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob2);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_out);
            REQUIRE(mff->current_time == 1558067027);
            REQUIRE(azr.last_txs.size() == 0);
            REQUIRE(azr.last_txids.back() == ob->m_hash);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067028);
            REQUIRE(azr.last_txids.size() == 1);
            REQUIRE(azr.last_txids.back() == ob->m_hash);
            REQUIRE(!mff->iterate());
        }
    }

    //     void tx_discarded(long timestamp, std::shared_ptr<tx> x, const std::vector<uint8_t>& rawtx, uint8_t reason, std::shared_ptr<tx> offender = nullptr) {
    //         bool offender_known = offender.get() && m_references.count(offender->m_hash);
    //         uint8_t cmd = cmd_mempool_invalidated | (offender.get() ? cmd_flag_offender_present : 0) | (offender_known ? cmd_flag_offender_known : 0);
    //         push_event(timestamp, cmd, x);
    //         *m_file << reason;
    //         OBREF(offender_known, offender);
    //         *m_file << rawtx;
    //     }

    SECTION("tx_discarded") {
        long pos;
        bitcoin::mff_analyzer azr;
        auto ob = make_random_tx();
        auto ob2 = make_random_tx();
        auto ob3 = make_random_tx();
        {
            auto mff = new_mff(&azr);
            mff->begin_segment(500000);
            pos = mff->m_file->tell();
            mff->tx_entered(1558067026, ob);
            mff->tx_entered(1558067026, ob2);
            mff->tx_entered(1558067027, ob3);
            std::vector<uint8_t> a{0x01, 0x02, 0x03};
            mff->tx_discarded(1558067028, ob2, (a), bitcoin::mff::reason_replaced, ob3);
            REQUIRE(mff->current_time == 1558067028);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(mff->m_references.count(ob2->m_hash));
            REQUIRE(mff->m_references.count(ob3->m_hash));
            REQUIRE(ob->m_sid  == mff->m_references.at(ob->m_hash));
            REQUIRE(ob2->m_sid == mff->m_references.at(ob2->m_hash));
            REQUIRE(ob3->m_sid == mff->m_references.at(ob3->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(mff->m_dictionary.count(ob2->m_sid));
            REQUIRE(mff->m_dictionary.count(ob3->m_sid));
            REQUIRE(*ob  == *mff->m_dictionary.at(ob->m_sid));
            REQUIRE(*ob2 == *mff->m_dictionary.at(ob2->m_sid));
            REQUIRE(*ob3 == *mff->m_dictionary.at(ob3->m_sid));
        }
        {
            auto mff = open_mff(&azr);
            REQUIRE(mff->current_time == 1558067028);
            REQUIRE(mff->m_references.count(ob->m_hash));
            REQUIRE(mff->m_references.count(ob2->m_hash));
            REQUIRE(mff->m_references.count(ob3->m_hash));
            REQUIRE(ob->m_sid  == mff->m_references.at(ob->m_hash));
            REQUIRE(ob2->m_sid == mff->m_references.at(ob2->m_hash));
            REQUIRE(ob3->m_sid == mff->m_references.at(ob3->m_hash));
            REQUIRE(mff->m_dictionary.count(ob->m_sid));
            REQUIRE(mff->m_dictionary.count(ob2->m_sid));
            REQUIRE(mff->m_dictionary.count(ob3->m_sid));
            REQUIRE(*ob  == *mff->m_dictionary.at(ob->m_sid));
            REQUIRE(*ob2 == *mff->m_dictionary.at(ob2->m_sid));
            REQUIRE(*ob3 == *mff->m_dictionary.at(ob3->m_sid));
            // now rewind and read again
            mff->m_file->seek(pos, SEEK_SET);
            mff->current_time = 0;
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067026);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob2);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_in);
            REQUIRE(mff->current_time == 1558067027);
            REQUIRE(azr.last_txs.size() == 1);
            REQUIRE(*azr.last_txs.back() == *ob3);
            REQUIRE(mff->iterate());
            REQUIRE(azr.last_command == bitcoin::mff::cmd_mempool_invalidated);
            REQUIRE(mff->current_time == 1558067028);
            REQUIRE(azr.last_txs.size() == 0);
            REQUIRE(azr.last_txids.size() == 1);
            REQUIRE(azr.last_txids.back() == ob2->m_hash);
            REQUIRE(azr.last_cause == ob3->m_hash);
            REQUIRE(!mff->iterate());
        }
    }


    //     void confirm_block(long timestamp, uint32_t height, const uint256& hash, const std::set<std::shared_ptr<tx>>& txs) {
    //         if (m_reg.m_tip < height - 1) begin_segment(height - 1);
    //         while (m_chain.m_tip && m_chain.m_tip >= height) unconfirm_tip(timestamp);
    //         push_event(timestamp, cmd_block_mined, txs);
    //         *m_file << hash << height;
    //         m_chain.did_confirm(new block(height, hash, txs));
    //         if (m_reg.m_tip < height) begin_segment(height);
    //     }

    //     void unconfirm_tip(long timestamp) {
    //         push_event(timestamp, cmd_block_unmined);
    //         *m_file << m_chain.m_tip;
    //         m_chain.pop_tip();
    //     }

    //     //////////////////////////////////////////////////////////////////////////////////////
    //     // Reading
    //     //
}

TEST_CASE("randomized sequence", "[random-sequence]") {
    SECTION("sequence 1 (10k)") {
        record head;
        std::vector<record*> rex;
        head.records_ptr = &rex;
        record* rec = &head;
        {
            bitcoin::mff_analyzer azr;
            auto mff = new_mff(&azr);
            tracker t(mff);
            mff->begin_segment(500000);
            uint32_t height = 500000;
            std::shared_ptr<bitcoin::tx> tx, offender;
            size_t sz;
            uint8_t reason;
            // txs from before recording began
            size_t pretxs = random_word() % 100;
            for (size_t i = 0; i < pretxs; ++i) t += make_random_tx();
            for (size_t i = 0; i < 10000; ++i) {
                auto action = random_byte() % 9;
                // fprintf(stderr, ": %s\n", ((const char*[]){"tx in", "tx in", "tx in", "tx in", "tx out", "tx out", "tx invalid", "block confirm", "block reorg"})[action]);
                switch (action) {
                case 0: // tx in
                case 1:
                case 2:
                case 3:
                    // fprintf(stderr, "tx in\n");
                    tx = make_random_tx();
                    t += tx;
                    mff->tx_entered(mff->current_time + 1, tx);
                    REC(record_mempool_in(tx));
                    break;
                case 4: // tx out
                case 5:
                    // fprintf(stderr, "tx out\n");
                    if (t.size() == 0) continue;
                    tx = t.sample();
                    reason = random_byte() % 6;
                    mff->tx_left(mff->current_time + 1, tx, reason);
                    t -= tx;
                    REC(record_mempool_out(tx->m_hash, reason));
                    break;
                case 6: // tx invalid
                    if (t.size() == 0) continue;
                    // fprintf(stderr, "tx invalid\n");
                    if (random_byte() & 1) {
                        // we have an offender
                        if (t.size() > 1 && (random_byte() & 1)) {
                            // known offender
                            offender = t.sample();
                        } else {
                            // unknown offender
                            offender = make_random_tx();
                        }
                        tx = t.sample(&offender);
                    } else {
                        offender = nullptr;
                        tx = t.sample();
                    }
                    reason = random_byte() % 6;
                    mff->tx_discarded(mff->current_time + 1, tx, std::vector<uint8_t>{1,2,3}, reason, offender);
                    t -= tx;
                    if (offender.get()) {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, offender->m_hash, reason, true /* todo: offender is always known */));
                    } else {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, reason));
                    }
                    break;
                case 7: // block confirm
                    // fprintf(stderr, "block confirm (%u)\n", height);
                    {
                        auto b = t.mine_block(mff->current_time + 1, ++height);
                        // if (i < 100) fprintf(stderr, "mine block %u\n", height);
                        REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                    }
                    break;
                case 8: // block reorged
                    {
                        size_t max_reorgs = height - 500000;
                        if (max_reorgs == 0) continue;
                        uint8_t b = random_byte();
                        size_t reorgs = 1;
                        if (b == 0)      reorgs = 6;
                        else if (b < 5)  reorgs = 5;
                        else if (b < 15) reorgs = 4;
                        else if (b < 40) reorgs = 3;
                        else if (b < 80) reorgs = 2;
                        else             reorgs = 1;
                        if (reorgs > max_reorgs) reorgs = max_reorgs;
                        // fprintf(stderr, "block reorg (max=%zu, count=%zu)\n", max_reorgs, reorgs);
                        long timestamp = mff->current_time + 1;
                        for (size_t i = 0; i < reorgs; ++i) {
                            REC(record_block_unmined(height));
                            t.unconfirm_tip(timestamp);
                            --height;
                        }
                        // if (i < 100) fprintf(stderr, "reorg down to block %u\n", height);
                        // reorgs only occur if we are also seeing a better chain, so make reorgs + 1 blocks
                        for (size_t i = 0; i <= reorgs; ++i) {
                            auto b = t.mine_block(timestamp, ++height);
                            REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                        }
                        // if (i < 100) fprintf(stderr, "re-mine to block %u\n", height);
                        break;
                    }
                }
            }
            // fprintf(stderr, "final stell() = %s\n", mff->stell().c_str());
        }
        // fprintf(stderr, "\n\n\n* * * * REPLAYING * * * *\n\n\n");
        {
            bitcoin::mff_analyzer azr;
            auto mff = open_mff(&azr);
            // now rewind and read again
            mff->goto_segment(500000);
            mff->current_time = 0;
            // replay should be identical to record
            for (rec = head.m_next; rec; rec = rec->m_next) {
                // should have another entry, as long as rec is !null
                bool b = mff->iterate();
                REQUIRE(b);
                // fprintf(stderr, "① %s\n", azr.to_string().c_str());
                // fprintf(stderr, "② %s\n", rec->to_string().c_str());
                rec->check(&azr);
            }
        }
        for (record* r : rex) delete r;
    }

    SECTION("sequence 2 (100k)") {
        record head;
        std::vector<record*> rex;
        head.records_ptr = &rex;
        record* rec = &head;
        {
            bitcoin::mff_analyzer azr;
            auto mff = new_mff(&azr);
            tracker t(mff);
            mff->begin_segment(500000);
            uint32_t height = 500000;
            std::shared_ptr<bitcoin::tx> tx, offender;
            size_t sz;
            uint8_t reason;
            // txs from before recording began
            size_t pretxs = random_word() % 100;
            for (size_t i = 0; i < pretxs; ++i) t += make_random_tx();
            for (size_t i = 0; i < 100000; ++i) {
                auto action = random_byte() % 9;
                // fprintf(stderr, ": %s\n", ((const char*[]){"tx in", "tx in", "tx in", "tx in", "tx out", "tx out", "tx invalid", "block confirm", "block reorg"})[action]);
                switch (action) {
                case 0: // tx in
                case 1:
                case 2:
                case 3:
                    // fprintf(stderr, "tx in\n");
                    tx = make_random_tx();
                    t += tx;
                    mff->tx_entered(mff->current_time + 1, tx);
                    REC(record_mempool_in(tx));
                    break;
                case 4: // tx out
                case 5:
                    // fprintf(stderr, "tx out\n");
                    if (t.size() == 0) continue;
                    tx = t.sample();
                    reason = random_byte() % 6;
                    mff->tx_left(mff->current_time + 1, tx, reason);
                    t -= tx;
                    REC(record_mempool_out(tx->m_hash, reason));
                    break;
                case 6: // tx invalid
                    if (t.size() == 0) continue;
                    // fprintf(stderr, "tx invalid\n");
                    if (random_byte() & 1) {
                        // we have an offender
                        if (t.size() > 1 && (random_byte() & 1)) {
                            // known offender
                            offender = t.sample();
                        } else {
                            // unknown offender
                            offender = make_random_tx();
                        }
                        tx = t.sample(&offender);
                    } else {
                        offender = nullptr;
                        tx = t.sample();
                    }
                    reason = random_byte() % 6;
                    mff->tx_discarded(mff->current_time + 1, tx, std::vector<uint8_t>{1,2,3}, reason, offender);
                    t -= tx;
                    if (offender.get()) {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, offender->m_hash, reason, true /* todo: offender is always known */));
                    } else {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, reason));
                    }
                    break;
                case 7: // block confirm
                    // fprintf(stderr, "block confirm (%u)\n", height);
                    {
                        auto b = t.mine_block(mff->current_time + 1, ++height);
                        // if (i < 100) fprintf(stderr, "mine block %u\n", height);
                        REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                    }
                    break;
                case 8: // block reorged
                    {
                        size_t max_reorgs = height - 500000;
                        if (max_reorgs == 0) continue;
                        uint8_t b = random_byte();
                        size_t reorgs = 1;
                        if (b == 0)      reorgs = 6;
                        else if (b < 5)  reorgs = 5;
                        else if (b < 15) reorgs = 4;
                        else if (b < 40) reorgs = 3;
                        else if (b < 80) reorgs = 2;
                        else             reorgs = 1;
                        if (reorgs > max_reorgs) reorgs = max_reorgs;
                        // fprintf(stderr, "block reorg (max=%zu, count=%zu)\n", max_reorgs, reorgs);
                        long timestamp = mff->current_time + 1;
                        for (size_t i = 0; i < reorgs; ++i) {
                            REC(record_block_unmined(height));
                            t.unconfirm_tip(timestamp);
                            --height;
                        }
                        // if (i < 100) fprintf(stderr, "reorg down to block %u\n", height);
                        // reorgs only occur if we are also seeing a better chain, so make reorgs + 1 blocks
                        for (size_t i = 0; i <= reorgs; ++i) {
                            auto b = t.mine_block(timestamp, ++height);
                            REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                        }
                        // if (i < 100) fprintf(stderr, "re-mine to block %u\n", height);
                        break;
                    }
                }
            }
            // fprintf(stderr, "final stell() = %s\n", mff->stell().c_str());
        }
        // fprintf(stderr, "\n\n\n* * * * REPLAYING * * * *\n\n\n");
        {
            bitcoin::mff_analyzer azr;
            auto mff = open_mff(&azr);
            // now rewind and read again
            mff->goto_segment(500000);
            mff->current_time = 0;
            // replay should be identical to record
            for (rec = head.m_next; rec; rec = rec->m_next) {
                // should have another entry, as long as rec is !null
                bool b = mff->iterate();
                REQUIRE(b);
                // fprintf(stderr, "① %s\n", azr.to_string().c_str());
                // fprintf(stderr, "② %s\n", rec->to_string().c_str());
                rec->check(&azr);
            }
        }
        for (record* r : rex) delete r;
    }

    SECTION("sequence 3 (250k)") {
        record head;
        std::vector<record*> rex;
        head.records_ptr = &rex;
        record* rec = &head;
        {
            bitcoin::mff_analyzer azr;
            auto mff = new_mff(&azr);
            tracker t(mff);
            mff->begin_segment(500000);
            uint32_t height = 500000;
            std::shared_ptr<bitcoin::tx> tx, offender;
            size_t sz;
            uint8_t reason;
            // txs from before recording began
            size_t pretxs = random_word() % 100;
            for (size_t i = 0; i < pretxs; ++i) t += make_random_tx();
            for (size_t i = 0; i < 250000; ++i) {
                auto action = random_byte() % 9;
                // fprintf(stderr, ": %s\n", ((const char*[]){"tx in", "tx in", "tx in", "tx in", "tx out", "tx out", "tx invalid", "block confirm", "block reorg"})[action]);
                switch (action) {
                case 0: // tx in
                case 1:
                case 2:
                case 3:
                    // fprintf(stderr, "tx in\n");
                    tx = make_random_tx();
                    t += tx;
                    mff->tx_entered(mff->current_time + 1, tx);
                    REC(record_mempool_in(tx));
                    break;
                case 4: // tx out
                case 5:
                    // fprintf(stderr, "tx out\n");
                    if (t.size() == 0) continue;
                    tx = t.sample();
                    reason = random_byte() % 6;
                    mff->tx_left(mff->current_time + 1, tx, reason);
                    t -= tx;
                    REC(record_mempool_out(tx->m_hash, reason));
                    break;
                case 6: // tx invalid
                    if (t.size() == 0) continue;
                    // fprintf(stderr, "tx invalid\n");
                    if (random_byte() & 1) {
                        // we have an offender
                        if (t.size() > 1 && (random_byte() & 1)) {
                            // known offender
                            offender = t.sample();
                        } else {
                            // unknown offender
                            offender = make_random_tx();
                        }
                        tx = t.sample(&offender);
                    } else {
                        offender = nullptr;
                        tx = t.sample();
                    }
                    reason = random_byte() % 6;
                    mff->tx_discarded(mff->current_time + 1, tx, std::vector<uint8_t>{1,2,3}, reason, offender);
                    t -= tx;
                    if (offender.get()) {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, offender->m_hash, reason, true /* todo: offender is always known */));
                    } else {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, reason));
                    }
                    break;
                case 7: // block confirm
                    // fprintf(stderr, "block confirm (%u)\n", height);
                    {
                        auto b = t.mine_block(mff->current_time + 1, ++height);
                        // if (i < 100) fprintf(stderr, "mine block %u\n", height);
                        REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                    }
                    break;
                case 8: // block reorged
                    {
                        size_t max_reorgs = height - 500000;
                        if (max_reorgs == 0) continue;
                        uint8_t b = random_byte();
                        size_t reorgs = 1;
                        if (b == 0)      reorgs = 6;
                        else if (b < 5)  reorgs = 5;
                        else if (b < 15) reorgs = 4;
                        else if (b < 40) reorgs = 3;
                        else if (b < 80) reorgs = 2;
                        else             reorgs = 1;
                        if (reorgs > max_reorgs) reorgs = max_reorgs;
                        // fprintf(stderr, "block reorg (max=%zu, count=%zu)\n", max_reorgs, reorgs);
                        long timestamp = mff->current_time + 1;
                        for (size_t i = 0; i < reorgs; ++i) {
                            REC(record_block_unmined(height));
                            t.unconfirm_tip(timestamp);
                            --height;
                        }
                        // if (i < 100) fprintf(stderr, "reorg down to block %u\n", height);
                        // reorgs only occur if we are also seeing a better chain, so make reorgs + 1 blocks
                        for (size_t i = 0; i <= reorgs; ++i) {
                            auto b = t.mine_block(timestamp, ++height);
                            REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                        }
                        // if (i < 100) fprintf(stderr, "re-mine to block %u\n", height);
                        break;
                    }
                }
            }
            // fprintf(stderr, "final stell() = %s\n", mff->stell().c_str());
        }
        // fprintf(stderr, "\n\n\n* * * * REPLAYING * * * *\n\n\n");
        {
            bitcoin::mff_analyzer azr;
            auto mff = open_mff(&azr);
            // now rewind and read again
            mff->goto_segment(500000);
            mff->current_time = 0;
            // replay should be identical to record
            for (rec = head.m_next; rec; rec = rec->m_next) {
                // should have another entry, as long as rec is !null
                bool b = mff->iterate();
                REQUIRE(b);
                // fprintf(stderr, "① %s\n", azr.to_string().c_str());
                // fprintf(stderr, "② %s\n", rec->to_string().c_str());
                rec->check(&azr);
            }
        }
        for (record* r : rex) delete r;
    }

    SECTION("sequence 4 (500k)") {
        record head;
        std::vector<record*> rex;
        head.records_ptr = &rex;
        record* rec = &head;
        {
            bitcoin::mff_analyzer azr;
            auto mff = new_mff(&azr);
            tracker t(mff);
            mff->begin_segment(500000);
            uint32_t height = 500000;
            std::shared_ptr<bitcoin::tx> tx, offender;
            size_t sz;
            uint8_t reason;
            // txs from before recording began
            size_t pretxs = random_word() % 100;
            for (size_t i = 0; i < pretxs; ++i) t += make_random_tx();
            for (size_t i = 0; i < 500000; ++i) {
                auto action = random_byte() % 9;
                // fprintf(stderr, ": %s\n", ((const char*[]){"tx in", "tx in", "tx in", "tx in", "tx out", "tx out", "tx invalid", "block confirm", "block reorg"})[action]);
                switch (action) {
                case 0: // tx in
                case 1:
                case 2:
                case 3:
                    // fprintf(stderr, "tx in\n");
                    tx = make_random_tx();
                    t += tx;
                    mff->tx_entered(mff->current_time + 1, tx);
                    REC(record_mempool_in(tx));
                    break;
                case 4: // tx out
                case 5:
                    // fprintf(stderr, "tx out\n");
                    if (t.size() == 0) continue;
                    tx = t.sample();
                    reason = random_byte() % 6;
                    mff->tx_left(mff->current_time + 1, tx, reason);
                    t -= tx;
                    REC(record_mempool_out(tx->m_hash, reason));
                    break;
                case 6: // tx invalid
                    if (t.size() == 0) continue;
                    // fprintf(stderr, "tx invalid\n");
                    if (random_byte() & 1) {
                        // we have an offender
                        if (t.size() > 1 && (random_byte() & 1)) {
                            // known offender
                            offender = t.sample();
                        } else {
                            // unknown offender
                            offender = make_random_tx();
                        }
                        tx = t.sample(&offender);
                    } else {
                        offender = nullptr;
                        tx = t.sample();
                    }
                    reason = random_byte() % 6;
                    mff->tx_discarded(mff->current_time + 1, tx, std::vector<uint8_t>{1,2,3}, reason, offender);
                    t -= tx;
                    if (offender.get()) {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, offender->m_hash, reason, true /* todo: offender is always known */));
                    } else {
                        REC(record_mempool_invalidated(tx->m_hash, std::vector<uint8_t>{1,2,3}, reason));
                    }
                    break;
                case 7: // block confirm
                    // fprintf(stderr, "block confirm (%u)\n", height);
                    {
                        auto b = t.mine_block(mff->current_time + 1, ++height);
                        // if (i < 100) fprintf(stderr, "mine block %u\n", height);
                        REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                    }
                    break;
                case 8: // block reorged
                    {
                        size_t max_reorgs = height - 500000;
                        if (max_reorgs == 0) continue;
                        uint8_t b = random_byte();
                        size_t reorgs = 1;
                        if (b == 0)      reorgs = 6;
                        else if (b < 5)  reorgs = 5;
                        else if (b < 15) reorgs = 4;
                        else if (b < 40) reorgs = 3;
                        else if (b < 80) reorgs = 2;
                        else             reorgs = 1;
                        if (reorgs > max_reorgs) reorgs = max_reorgs;
                        // fprintf(stderr, "block reorg (max=%zu, count=%zu)\n", max_reorgs, reorgs);
                        long timestamp = mff->current_time + 1;
                        for (size_t i = 0; i < reorgs; ++i) {
                            REC(record_block_unmined(height));
                            t.unconfirm_tip(timestamp);
                            --height;
                        }
                        // if (i < 100) fprintf(stderr, "reorg down to block %u\n", height);
                        // reorgs only occur if we are also seeing a better chain, so make reorgs + 1 blocks
                        for (size_t i = 0; i <= reorgs; ++i) {
                            auto b = t.mine_block(timestamp, ++height);
                            REC(record_block_mined(b->m_hash, b->m_height, b->m_txids));
                        }
                        // if (i < 100) fprintf(stderr, "re-mine to block %u\n", height);
                        break;
                    }
                }
            }
            // fprintf(stderr, "final stell() = %s\n", mff->stell().c_str());
        }
        // fprintf(stderr, "\n\n\n* * * * REPLAYING * * * *\n\n\n");
        {
            bitcoin::mff_analyzer azr;
            auto mff = open_mff(&azr);
            // now rewind and read again
            mff->goto_segment(500000);
            mff->current_time = 0;
            // replay should be identical to record
            for (rec = head.m_next; rec; rec = rec->m_next) {
                // should have another entry, as long as rec is !null
                bool b = mff->iterate();
                REQUIRE(b);
                // fprintf(stderr, "① %s\n", azr.to_string().c_str());
                // fprintf(stderr, "② %s\n", rec->to_string().c_str());
                rec->check(&azr);
            }
        }
        for (record* r : rex) delete r;
    }
}
