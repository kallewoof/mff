// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TINYMEMPOOL_H
#define BITCOIN_TINYMEMPOOL_H

#include <uint256.h>
#include <serialize.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <tinytx.h>

namespace tiny {

extern bool debug_ancestry;

/** Reason why a transaction was removed from the mempool,
 * this is passed to the notification signal.
 */
enum class MemPoolRemovalReason {
    UNKNOWN = 0, //! Manually removed or unknown reason
    EXPIRY,      //! Expired from mempool
    SIZELIMIT,   //! Removed in size limiting
    REORG,       //! Removed for reorganization
    BLOCK,       //! Removed for block
    CONFLICT,    //! Removed for conflict with in-block transaction
    REPLACED     //! Removed for replacement
};

struct mempool_entry {
    std::shared_ptr<const tx> x;
    uint64_t in_sum;

    mempool_entry() {}
    mempool_entry(std::shared_ptr<tx> x_in, uint64_t in_sum_in)
    : x(x_in), in_sum(in_sum_in) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(x);
        READWRITE(VARINT(in_sum));
    }

    template <typename Stream>
    mempool_entry(deserialize_type, Stream& s) {
        tx* xp;
        if (x.get()) xp = const_cast<tx*>(x.get()); else {
            xp = new tx();
            x.reset(xp);
        }
        s >> *xp >> VARINT(in_sum);
    }

    bool operator==(const mempool_entry& other) const       {
        if (debug_ancestry) printf("mempool_entry::operator==(const mempool_entry& other) const: this=%p, other=%p, x=%p, other.x=%p, x==other.x ? %d, *x==*other.x ? %d\n", this, &other, x.get(), other.x.get(), x == other.x, *x == *other.x);
        return *x == *other.x;
    }
    bool operator==(const std::shared_ptr<tx>& other) const {
        if (debug_ancestry) printf("mempool_entry::operator==(const std::shared_ptr<tx>& other) const: this=%p, other=%p, x=%p, x==other ? %d, *x==*other ? %d\n", this, other.get(), x.get(), x == other, *x == *other);
        return *x == *other;
    }
    bool operator==(const tx& other) const                  {
        if (debug_ancestry) printf("mempool_entry::operator==(const tx& other) const: this=%p, &other=%p, x=%p, *x==other ? %d\n", this, &other, x.get(), *x == other);
        return *x == other;
    }

    uint64_t fee() const {
        uint64_t fee = in_sum;
        for (const auto& vout : x->vout) {
            fee -= vout.value;
        }
        return fee;
    }
};

class mempool_callback {
public:
    virtual void add_entry(std::shared_ptr<const mempool_entry>& entry) { assert(0); }
    virtual void remove_entry(std::shared_ptr<const mempool_entry>& entry, MemPoolRemovalReason reason, std::shared_ptr<tx> cause) { assert(0); }
    virtual void push_block(int height, uint256 hash) { assert(0); }
    virtual void pop_block(int height) { assert(0); }
};

class mempool {
private:
    MemPoolRemovalReason determine_reason(std::shared_ptr<const mempool_entry> added, std::shared_ptr<const mempool_entry> removed);
public:
    mempool_callback* callback = nullptr;
    std::map<uint256, std::shared_ptr<const mempool_entry>> entry_map;
    std::map<uint256, std::vector<std::shared_ptr<const mempool_entry>>> ancestry;
    /**
     * Insert x into the mempool, removing any conflicting transactions.
     */
    void insert_tx(std::shared_ptr<tx> x);
    /**
     * Remove entry from mempool. Any transactions which depend on x as input are
     * also removed.
     */
    void remove_entry(std::shared_ptr<const mempool_entry> entry, MemPoolRemovalReason reason, std::shared_ptr<tx> cause = nullptr);
    /**
     * Process the given block, removing all transactions and registering
     * transactions that were previously unknown.
     */
    void process_block(int height, uint256 hash, const std::vector<tx>& txs);
    /**
     * Mark the given block as reorged, punting it from the chain tip.
     */
    void reorg_block(int height);
    /**
     * Determine if the transaction would result in any evictions
     */
    bool is_tx_conflicting(std::shared_ptr<tx> tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (entry_map.size() > 0) {
            // 
        }
        READWRITE(entry_map);
        READWRITE(ancestry);
        printf("(%zu entries in mempool, %zu ancestry records)\n", entry_map.size(), ancestry.size());
    }
};

}

#endif // BITCOIN_TINYMEMPOOL_H
