#include <bcq/bitcoin.h>
#include <streams.h>

namespace bitcoin {

void load_mempool(std::shared_ptr<tiny::mempool>& mempool, const std::string& path) {
    if (!mempool.get()) {
        mempool = std::make_shared<tiny::mempool>();
    }
    FILE* fp = fopen(path.c_str(), "rb");
    CAutoFile af(fp, SER_DISK, 0);
    af >> *mempool;
}

void save_mempool(std::shared_ptr<tiny::mempool>& mempool, const std::string& path) {
    FILE* fp = fopen(path.c_str(), "wb");
    CAutoFile af(fp, SER_DISK, 0);
    af << *mempool;
}

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

tx::tx(const tiny::mempool_entry& entry) {
    auto tref = *entry.x;
    m_sid = cq::unknownid;
    m_hash = tref.hash;
    m_weight = tref.GetWeight();
    m_fee = entry.fee();
    m_vin.clear();
    m_vout.clear();
    for (const auto& vin : tref.vin) {
        const auto& prevout = vin.prevout;
        m_vin.push_back(bitcoin::outpoint(prevout));
    }
    for (const auto& vout : tref.vout) {
        m_vout.push_back(vout.value);
    }
}

tx::tx(const tiny::tx& x) {
    m_sid = cq::unknownid;
    m_hash = x.hash;
    m_weight = x.GetWeight();
    m_fee = 0;
    m_vin.clear();
    m_vout.clear();
    for (const auto& vin : x.vin) {
        const auto& prevout = vin.prevout;
        m_vin.push_back(bitcoin::outpoint(prevout));
    }
    for (const auto& vout : x.vout) {
        m_vout.push_back(vout.value);
    }
}

void tx::serialize(cq::serializer* stream) const {
    // we do in fact serialize the txid here
    *stream << m_hash << cq::varint(m_weight) << cq::varint(m_fee);
    {
        std::vector<uint256> vin_txids;
        for (const auto& in : m_vin) vin_txids.push_back(in.m_txid);
        stream->m_compressor->compress(stream, vin_txids);
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
    stream->m_compressor->decompress(stream, vin_txids);
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
    for (const auto& tx : last_txs) txids.insert(tx->m_hash);
    if (!last_cause.IsNull()) txids.insert(last_cause);
    if (last_mined_block) txids.insert(last_mined_block->m_txids.begin(), last_mined_block->m_txids.end());
}

void mff_mempool_callback::add_entry(std::shared_ptr<const tiny::mempool_entry>& entry) {
    const auto& tref = entry->x;
    auto ex = m_mff->tretch(tref->hash);
    if (!ex) ex = std::make_shared<tx>(*entry);
    m_mff->tx_entered(m_current_time, ex);
}

void mff_mempool_callback::skipping_mined_tx(std::shared_ptr<tiny::tx> x) {
    auto ex = m_mff->tretch(x->hash);
    if (!ex) ex = std::make_shared<tx>(*x);
    m_pending_btxs.insert(ex);
}

void mff_mempool_callback::discard(std::shared_ptr<const tiny::mempool_entry>& entry, uint8_t reason, std::shared_ptr<tiny::tx>& cause) {
    cq::chv_stream s;
    const auto& tref = *entry->x;
    tref.Serialize(s);
    auto ex = m_mff->tretch(tref.hash);
    if (!ex) ex = std::make_shared<tx>(*entry);
    m_mff->tx_discarded(m_current_time, ex, s.get_chv(), reason, cause ? m_mff->tretch(cause->hash) : nullptr);
}

void mff_mempool_callback::remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause) {
    // do we know this transaction?
    const auto& tref = *entry->x;
    bool known = m_mff->m_references.count(tref.hash);

    switch (reason) {
    case tiny::MemPoolRemovalReason::EXPIRY:    //! Expired from mempool
        if (!known) return;
        return m_mff->tx_left(m_current_time, m_mff->tretch(tref.hash), mff::reason_expired, cause ? m_mff->tretch(cause->hash) : nullptr);
    case tiny::MemPoolRemovalReason::SIZELIMIT: //! Removed in size limiting
        if (!known) return;
        return m_mff->tx_left(m_current_time, m_mff->tretch(tref.hash), mff::reason_sizelimit, cause ? m_mff->tretch(cause->hash) : nullptr);
    case tiny::MemPoolRemovalReason::REORG:     //! Removed for reorganization
        return discard(entry, mff::reason_reorg, cause);
    case tiny::MemPoolRemovalReason::BLOCK:     //! Removed for block
        {
            auto ex = m_mff->tretch(tref.hash);
            if (!ex) ex = std::make_shared<tx>(*entry);
            m_pending_btxs.insert(ex);
        }
        return;
    case tiny::MemPoolRemovalReason::CONFLICT:  //! Removed for conflict with in-block transaction
        return discard(entry, mff::reason_conflict, cause);
    case tiny::MemPoolRemovalReason::REPLACED:  //! Removed for replacement
        return discard(entry, mff::reason_replaced, cause);
    case tiny::MemPoolRemovalReason::UNKNOWN:   //! Manually removed or unknown reason
    default:
        // TX_OUT(REASON=2) -or- TX_INVALID(REASON=3)
        // If there is a cause, we use invalid, otherwise out
        if (cause) {
            return discard(entry, mff::reason_unknown, cause);
        }
        return m_mff->tx_left(m_current_time, m_mff->tretch(tref.hash), mff::reason_unknown, nullptr);
    }
}

void mff_mempool_callback::push_block(int height, uint256 hash, const std::vector<tiny::tx>& txs) {
    m_mff->confirm_block(m_current_time, height, hash, m_pending_btxs);
    m_pending_btxs.clear();
}

void mff_mempool_callback::pop_block(int height) {
    while (m_mff->get_height() >= height) m_mff->unconfirm_tip(m_current_time);
}

} // namespace bitcoin
