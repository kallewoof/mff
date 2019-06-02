#include <bcq/bitcoin.h>
// #include <streams.h>

extern "C" { void libbcq_is_present(void) {} } // hello autotools, pleased to meat you

namespace bitcoin {

const uint8_t mff::cmd_time_set;
const uint8_t mff::cmd_mempool_in;
const uint8_t mff::cmd_mempool_out;
const uint8_t mff::cmd_mempool_invalidated;
const uint8_t mff::cmd_block_mined;
const uint8_t mff::cmd_block_unmined;
const uint8_t mff::cmd_flag_offender_present;
const uint8_t mff::cmd_flag_offender_known;

const uint8_t mff::reason_unknown;
const uint8_t mff::reason_expired;
const uint8_t mff::reason_sizelimit;
const uint8_t mff::reason_reorg;
const uint8_t mff::reason_conflict;
const uint8_t mff::reason_replaced;

void tx::serialize(cq::serializer* stream) const {
    // we do in fact serialize the txid here
    *stream << m_hash << cq::varint(m_weight) << cq::varint(m_fee);
    {
        std::vector<uint256> vin_txids;
        for (const auto& in : m_vin) vin_txids.push_back(in.m_txid);
        m_compressor->compress(stream, vin_txids);
    }
    for (auto& o : m_vin) {
        *stream << o;
    }
    cq::varint(m_vout.size()).serialize(stream);
    for (auto& o : m_vout) cq::varint(o).serialize(stream);
}

void tx::deserialize(cq::serializer* stream) {
    m_hash.Unserialize(*stream);
    m_weight = cq::varint::load(stream);
    m_fee = cq::varint::load(stream);
    std::vector<uint256> vin_txids;
    m_compressor->decompress(stream, vin_txids);
    size_t vin_sz = vin_txids.size();
    m_vin.resize(vin_sz);
    for (size_t i = 0; i < vin_sz; ++i) {
        outpoint& o = m_vin[i];
        o.m_txid = vin_txids[i];
        *stream >> o;
    }
    size_t vout_sz = cq::varint::load(stream);
    m_vout.resize(vout_sz);
    for (size_t i = 0; i < vout_sz; ++i) m_vout[i] = cq::varint::load(stream);
}

void block::serialize(cq::serializer* stream) const {
    *stream << m_height;
    m_hash.Serialize(*stream);
}

void block::deserialize(cq::serializer* stream) {
    *stream >> m_height;
    m_hash.Unserialize(*stream);
}

std::string mff::detect_prefix(const std::string& dbpath) {
    std::vector<std::string> list;
    if (cq::listdir(dbpath, list)) {
        for (const std::string& f : list) {
            if (f.size() > 8 && f.substr(f.size() - 3) == ".cq") {
                // <prefix>NNNNN.cq
                std::string prefix = f.substr(0, f.size() - 8);
                return prefix;
            }
        }
    }
    return "mff";
}

void mff_analyzer::receive_transaction(std::shared_ptr<tx> x) { set(mff::cmd_mempool_in, x); }

void mff_analyzer::receive_transaction_with_txid(const uint256& txid) { set(mff::cmd_mempool_in, txid); }

void mff_analyzer::forget_transaction_with_txid(const uint256& txid, uint8_t reason) {
    set(mff::cmd_mempool_out, txid);
    last_reason = reason;
}

void mff_analyzer::discard_transaction_with_txid(const uint256& txid, const std::vector<uint8_t>& rawtx, uint8_t reason, const uint256* cause) {
    set(mff::cmd_mempool_invalidated, txid);
    if (cause) last_cause = *cause;
    last_reason = reason;
    last_rawtx = rawtx;
}

void mff_analyzer::block_confirmed(const block& b) {
    set(mff::cmd_block_mined);
    last_mined_block = &b;
}

void mff_analyzer::block_reorged(uint32_t height) {
    set(mff::cmd_block_unmined);
    last_unmined_height = height;
}

void mff_analyzer::iterated(long starting_pos, long resulting_pos) {
    count[last_command]++;
    long used = resulting_pos - starting_pos;
    total_bytes += used;
    usage[last_command] += used;
    if (last_command == mff::cmd_mempool_in && last_txs.size()) {
        total_txrecs++;
        total_txrec_bytes += used;
    }
}

void mff_analyzer::populate_touched_txids(std::set<uint256>& txids) const {
    txids.clear();
    txids.insert(last_txids.begin(), last_txids.end());
    for (const auto& tx : last_txs) {
        txids.insert(tx->m_hash);
        for (const auto& i : tx->m_vin) {
            if (!i.m_txid.IsNull()) txids.insert(i.m_txid);
        }
    }
    if (!last_cause.IsNull()) txids.insert(last_cause);
    if (last_mined_block) txids.insert(last_mined_block->m_txids.begin(), last_mined_block->m_txids.end());
    if (enable_touchmap) {
        for (const auto& u : txids) ++touchmap[u];
    }
}

} // namespace bitcoin
