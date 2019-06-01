#include <bcq/utils.h>
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

outpoint::outpoint(const tiny::outpoint& o) : outpoint(o.n, o.hash) {}

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
        if (!known) return;
        return m_mff->tx_left(m_current_time, m_mff->tretch(tref.hash), mff::reason_reorg, cause ? m_mff->tretch(cause->hash) : nullptr);
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
