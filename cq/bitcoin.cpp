#include <cq/bitcoin.h>

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
    m_hash.Serialize(*stream);
    // then weight, fee
    cq::varint(m_weight).serialize(stream);
    cq::varint(m_fee).serialize(stream);
    // finally, we serialize the inputs
    cq::varint(m_vin.size()).serialize(stream);
    for (auto& o : m_vin) {
        *stream << o.m_state;
        switch (o.m_state) {
        case outpoint::state_unknown:
            o.m_hash.Serialize(*stream);
            break;
        case outpoint::state_known:
            cq::varint(stream->tell() - o.m_sid).serialize(stream);
            break;
        case outpoint::state_coinbase:
        case outpoint::state_confirmed: // ????
            break;
        }
        o.serialize(stream);
    }
}

void tx::deserialize(cq::serializer* stream) {
    m_hash.Unserialize(*stream);
    m_weight = cq::varint::load(stream);
    m_fee = cq::varint::load(stream);
    size_t vin_sz = cq::varint::load(stream);
    m_vin.resize(vin_sz);
    for (size_t i = 0; i < vin_sz; ++i) {
        outpoint& o = m_vin[i];
        *stream >> o.m_state;
        switch (o.m_state) {
        case outpoint::state_unknown:
            o.m_hash.Unserialize(*stream);
            break;
        case outpoint::state_known:
            o.m_sid = stream->tell() - cq::varint::load(stream);
            break;
        case outpoint::state_coinbase:
        case outpoint::state_confirmed: // ????
            break;
        }
        o.deserialize(stream);
    }
}

void block::serialize(cq::serializer* stream) const {
    *stream << m_height;
    m_hash.Serialize(*stream);
}

void block::deserialize(cq::serializer* stream) {
    *stream >> m_height;
    m_hash.Unserialize(*stream);
}

std::vector<uint256> last_txids;
std::vector<std::shared_ptr<tx>> last_txs;
uint8_t last_command;
uint256 last_cause;

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

std::string mff_analyzer::to_string() const {
    char buf[1024];
    #define X(args...) sprintf(buf, args); return buf
    switch (last_command) {
    case mff::cmd_mempool_in:  X("mempool_in %s", last_txs.size() == 1 ? last_txs.back()->m_hash.ToString().c_str() : last_txids.size() == 1 ? last_txids.back().ToString().c_str() : "????");
    case mff::cmd_mempool_out: X("mempool_out %s", last_txids.back().ToString().c_str());
    case mff::cmd_mempool_invalidated: X("mempool_invalidated %s (reason=%s; offender=%s)", last_txids.back().ToString().c_str(), reason_string(last_reason).c_str(), last_cause.ToString().c_str());
    case mff::cmd_block_mined: X("mined %u (%s)", last_mined_block->m_height, last_mined_block->m_hash.ToString().c_str());
    case mff::cmd_block_unmined: X("unmined %u", last_unmined_height);
    default: return "!!!!??????";
    }
}

} // namespace bitcoin
