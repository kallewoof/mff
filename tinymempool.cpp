#include <tinymempool.h>
#include <amap.h>
#include <cmath>

namespace tiny {

bool debug_ancestry = false;

template<typename T>
typename T::const_iterator find_shared_entry(const T& v, const std::shared_ptr<const mempool_entry>& entry) {
    return std::find_if(v.begin(), v.end(), [entry](std::shared_ptr<const mempool_entry> const& e) {
        return *e == *entry;
    });
}

void mempool::insert_tx(std::shared_ptr<tx> x) {
    // avoid duplicate insertions
    if (entry_map.count(x->hash)) return;

    // find and evict transactions that conflict with x
    std::set<std::shared_ptr<const mempool_entry>> evictees;
    if (!x->IsCoinBase()) {
        for (const auto& in : x->vin) {
            bool found = false;
            auto prevout = in.prevout;
            if (ancestry.count(prevout.hash)) {
                for (const auto& candidate : ancestry[prevout.hash]) {
                    for (const auto& c_in : candidate->x->vin) {
                        auto c_prevout = c_in.prevout;
                        if (c_prevout.hash == prevout.hash && c_prevout.n == prevout.n) {
                            // found a match
                            assert(candidate->x->hash != x->hash);
                            evictees.insert(candidate);
                            found = true;
                            break; // c_in
                        }
                    }
                    if (found) break; // candidate
                }
            }
        }
    }

    // fetch input amounts
    uint64_t in_sum = 0;
    if (!x->IsCoinBase()) {
        for (const auto& in : x->vin) {
            if (entry_map.count(in.prevout.hash)) {
                in_sum += entry_map[in.prevout.hash]->x->vout[in.prevout.n].value;
            } else {
                in_sum += amap::output_amount(in.prevout.hash, in.prevout.n);
            }
        }
    }

    // create new entry
    std::shared_ptr<const mempool_entry> entry = std::make_shared<const mempool_entry>(x, in_sum);
    entry_map[x->hash] = entry;

    // perform evictions
    for (const auto& e : evictees) {
        remove_entry(e, determine_reason(entry, e), x);
    }
    
    // link ancestry
    if (!x->IsCoinBase()) {
        for (const auto& in : x->vin) {
            ancestry[in.prevout.hash].push_back(entry);
        }
    }

    if (callback) {
        callback->add_entry(entry);
    }

    // enqueue for potential removal
    enqueue(entry);
}

void mempool::remove_entry(std::shared_ptr<const mempool_entry> entry, MemPoolRemovalReason reason, std::shared_ptr<tx> cause) {
    if (!entry_map.count(entry->x->hash)) return;

    // evict any tx dependent on x
    while (ancestry.count(entry->x->hash)) {
        remove_entry(ancestry[entry->x->hash].back(), reason, cause);
    }

    if (callback) {
        callback->remove_entry(entry, reason, cause);
    }

    // unlink ancestry
    if (!entry->x->IsCoinBase()) {
        for (const auto& in : entry->x->vin) {
            assert(ancestry.count(in.prevout.hash));
            auto it = find_shared_entry(ancestry[in.prevout.hash], entry);
            //  std::find_if(ancestry[in.prevout.hash].begin(), ancestry[in.prevout.hash].end(), 
            // [entry](std::shared_ptr<const mempool_entry> const& e) {
            //     return *e == *entry;
            // });
            if (it == ancestry[in.prevout.hash].end()) {
                printf("cannot find %s in ancestry[%s]:\n", entry->x->hash.ToString().c_str(), in.prevout.hash.ToString().c_str());
                debug_ancestry = true;
                find_shared_entry(ancestry[in.prevout.hash], entry);
                // std::find_if(ancestry[in.prevout.hash].begin(), ancestry[in.prevout.hash].end(), 
                // [entry](std::shared_ptr<const mempool_entry> const& e) {
                //     return *e == *entry;
                // });
                for (auto it : ancestry[in.prevout.hash]) {
                    printf("- %s: %s==%s ? %d; e1=e2 ? %d\n", it->x->hash.ToString().c_str(), entry->x->hash.ToString().c_str(), it->x->hash.ToString().c_str(), entry->x->hash == it->x->hash, entry == it);
                }
                debug_ancestry = false;
                printf("(END)\n");
            }
            assert(it != ancestry[in.prevout.hash].end());
            ancestry[in.prevout.hash].erase(it);
            if (ancestry[in.prevout.hash].size() == 0) {
                ancestry.erase(in.prevout.hash);
            }
        }
    }

    // remove from entry map
    entry_map.erase(entry->x->hash);

    // remove from entry queue
    auto it = find_shared_entry(entry_queue, entry);
    assert(it != entry_queue.end());
    entry_queue.erase(it);
    // entry_queue.erase(std::find(entry_queue.begin(), entry_queue.end(), entry));
}

void mempool::process_block(int height, uint256 hash, const std::vector<tx>& txs) {
    for (const auto& x : txs) {
        if (entry_map.count(x.hash)) {
            remove_entry(entry_map[x.hash], MemPoolRemovalReason::BLOCK);
        }
    }
    if (callback) callback->push_block(height, hash);
}

void mempool::reorg_block(int height) {
    if (callback) callback->pop_block(height);
}

bool mempool::is_tx_conflicting(std::shared_ptr<tx> x) {
    // find transactions that conflict with x
    for (const auto& in : x->vin) {
        auto prevout = in.prevout;
        if (ancestry.count(prevout.hash)) {
            for (const auto& candidate : ancestry[prevout.hash]) {
                for (const auto& c_in : candidate->x->vin) {
                    auto c_prevout = c_in.prevout;
                    if (c_prevout.hash == prevout.hash && c_prevout.n == prevout.n) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

MemPoolRemovalReason mempool::determine_reason(std::shared_ptr<const mempool_entry> added, std::shared_ptr<const mempool_entry> removed) {
    // RBF if added has higher fee and spends all inputs spent by removed
    // Strictly speaking, this is not perfect but it's a reasonable estimate
    int64_t added_w = added->x->GetWeight();
    int64_t removed_w = removed->x->GetWeight();
    uint64_t added_fee = added->fee();
    uint64_t removed_fee = removed->fee();
    if (added_fee <= removed_fee) return MemPoolRemovalReason::CONFLICT;

    double added_feerate = (double)added_fee / added_w;
    double removed_feerate = (double)removed_fee / removed_w;
    if (added_feerate <= removed_feerate) return MemPoolRemovalReason::CONFLICT;

    std::set<outpoint> spent;
    for (const auto& in : removed->x->vin) {
        spent.insert(in.prevout);
    }
    for (const auto& in : added->x->vin) {
        if (!spent.count(in.prevout)) return MemPoolRemovalReason::CONFLICT;
    }

    // absolute fee is higher, feerate is higher, and all inputs in the evicted
    // transaction were spent in the new one
    return MemPoolRemovalReason::REPLACED;
}

void mempool::enqueue(const std::shared_ptr<const mempool_entry>& entry, bool preserve_size_limits) {
    size_t l = 0, r = entry_queue.size(), m = 0;
    double in_feerate = entry->feerate();
    while (r > l) {
        m = l+((r-l)>>1);
        double feerate = entry_queue[m]->feerate();
        // printf("enqueue FR=%lf ([%zu..%zu]: %zu=%lf)\n", in_feerate, l, r, m, feerate);
        if (std::fabs(in_feerate - feerate) < 1) break;
        if (in_feerate < feerate) {
            // in cheaper, move towards front
            r = m;
        } else {
            // in more expensive, move towards end
            l = m + 1;
        }
    }
    // if (entry_queue.size() > m) printf("enqueue FR=%lf: %zu=%lf\n", in_feerate, m, entry_queue[m]->feerate());
    entry_queue.insert(entry_queue.begin() + m, entry);

    if (preserve_size_limits) {
        // do not exceed entry/ref limit
        while (entry_queue.size() > MAX_ENTRIES || ancestry.size() > MAX_REFS) {
            remove_entry(entry_queue[0], MemPoolRemovalReason::SIZELIMIT);
        }
    }
}

} // namespace tiny
