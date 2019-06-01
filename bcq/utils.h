#ifndef included_bcq_utils_h_
#define included_bcq_utils_h_

#include <bcq/bitcoin.h>

#include <tinymempool.h>

namespace tiny {

BITCOIN_SER(txin);
BITCOIN_SER(txout);

}

namespace bitcoin {

void load_mempool(std::shared_ptr<tiny::mempool>& mempool, const std::string& path);
void save_mempool(std::shared_ptr<tiny::mempool>& mempool, const std::string& path);

class mff_mempool_callback : public tiny::mempool_callback {
public:
    long& m_current_time;
    std::shared_ptr<mff> m_mff;
    std::set<std::shared_ptr<tx>> m_pending_btxs;
    mff_mempool_callback(long& current_time, std::shared_ptr<mff> mff) : m_current_time(current_time), m_mff(mff) {}
    virtual void add_entry(std::shared_ptr<const tiny::mempool_entry>& entry) override;
    virtual void skipping_mined_tx(std::shared_ptr<tiny::tx> tx) override;
    virtual void remove_entry(std::shared_ptr<const tiny::mempool_entry>& entry, tiny::MemPoolRemovalReason reason, std::shared_ptr<tiny::tx> cause) override;
    virtual void push_block(int height, uint256 hash, const std::vector<tiny::tx>& txs) override;
    virtual void pop_block(int height) override;

    void discard(std::shared_ptr<const tiny::mempool_entry>& entry, uint8_t reason, std::shared_ptr<tiny::tx>& cause);
};

} // namespace bitcoin

#endif // included_bcq_utils_h_
