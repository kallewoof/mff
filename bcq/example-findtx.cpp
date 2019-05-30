#include <vector>
#include <memory>
#include <cqdb/uint256.h>
#include <utiltime.h>

#include <bcq/bitcoin.h>

inline std::string time_string(int64_t time);

bool txid_in_vtx(const uint256& txid, const std::vector<std::shared_ptr<bitcoin::tx>>& vtx);

std::string txid_str(const uint256& txid) { return txid.ToString(); }

int main(int argc, const char** argv) {
    if (argc < 3) {
        fprintf(stderr, "syntax: %s <db path> <txid>\n", argv[0]);
        return 1;
    }

    const uint256 txid = uint256S(argv[2]);

    // mff handles the mempool file format disk I/O; it is our source in this case
    auto mff = std::make_shared<bitcoin::mff>(argv[1], "example", 2016, true);
    bitcoin::mff& f = *mff;
    f.load();

    // we will use the built-in mff_analyzer to determine if we found our tx
    bitcoin::mff_analyzer azr;
    mff->m_delegate = &azr;

    // rewind to the beginning
    f.rewind();

    // start iterating through; if we encounter the transaction, show info about it
    long internal_start_time = 0;
    uint64_t entries = 0;
    int64_t start_time = GetTime();
    std::set<uint256> touched_txids;
    while (f.iterate()) {
        if (!internal_start_time) {
            internal_start_time = f.m_current_time;
            printf("%s: ----log begins----\n", time_string(internal_start_time).c_str());
        }
        entries++;
        if (!(entries % 100)) {
            cq::id cluster = mff->get_registry().m_current_cluster;
            uint32_t block_height = mff->m_chain.m_tip;
            long pos2 = mff->m_file->tell();
            printf(" %llu %ld : %s <cluster=%" PRIid " block=%u>     \r", entries, pos2, time_string(mff->m_current_time).c_str(), cluster, block_height);
            fflush(stdout);
        }

        azr.populate_touched_txids(touched_txids);
        if (touched_txids.count(txid)) {
            printf("%s: %s", time_string(mff->m_current_time).c_str(), bitcoin::cmd_string(azr.last_command).c_str());
            if (azr.last_command == bitcoin::mff::cmd_mempool_invalidated) {
                printf(" %s (%s)", txid_str(azr.last_txids.back()).c_str(), bitcoin::reason_string(azr.last_reason).c_str());
                if (!azr.last_cause.IsNull()) {
                    uint256 replacement = azr.last_cause;
                    printf(" -> %s", txid_str(replacement).c_str());
                }
            } else if (azr.last_command == bitcoin::mff::cmd_mempool_out) {
                printf(" %s (%s)", txid_str(azr.last_txids.back()).c_str(), bitcoin::reason_string(azr.last_reason).c_str());
            } else if (azr.last_command == bitcoin::mff::cmd_mempool_in) {
                if (azr.last_txs.size()) {
                    // recorded
                    const auto& t = azr.last_txs.back();
                    // printf("\n\t%s\n", t->to_string().c_str());
                    std::string extra = "";
                    // check if any of our targeted tx's is spent by this tx
                    // for (const tracked& tracked2 : txids) {
                    //     uint32_t index;
                    //     if (t->spends(tracked2.txid, azr.seqs[tracked2.txid], index)) {
                    //         extra += std::string(" (spends ") + txid_str(tracked2.txid) + ":" + std::to_string(index) + ")";
                    //         if (tracked2.depth > 0 && txids.find(tracked{t->id}) == txids.end()) {
                    //             txids.insert(tracked{tracked2, t->id});
                    //         }
                    //     }
                    // }
                    // if (!t->is(txid) && extra == "") {
                    // //     // printf(" (");
                    // // } else if (t->spends(txid, azr.seqs[txid])) {
                    // //     extra = std::string(" (spends ") + txid_str(txid) + ")";
                    // //     // printf(" (spends %s: ", txid_str(txid).c_str());
                    // // } else {
                    //     extra = " (?)";
                    //     // printf(" (???: ");
                    // }
                    printf(" (first seen %s%s - %" PRIu64 " vbytes, %" PRIu64 " fee, %.3lf fee rate (s/vbyte), block #%u)\n", txid_str(t->m_hash).c_str(), extra.c_str(), t->vsize(), t->m_fee, t->feerate(), mff->m_chain.m_tip);
                    // const mff::tx& t = *azr.txs[azr.seqs[txid]];
                    // printf(" (txid seq=%" PRIu64 ") %s", azr.seqs[txid], t->to_string().c_str());
                    // for (const auto& x : t->vin) if (x.is_known()) printf("\n- %" PRIu64 " = %s", x.get_seq(), azr.txs[x.get_seq()]->id.ToString().c_str());
                    // we don't wanna bother with tracking TX_REC for multiple txids so we just break the loop here
                } else {
                    // re-entry
                    printf(" (%s)", txid_str(azr.last_txids.back()).c_str());
                }
                break;
            } else if (azr.last_command == bitcoin::mff::cmd_block_mined) {
                printf(" (%s in #%u%s)", txid_str(txid).c_str(), mff->m_chain.m_tip, mff->m_chain.get_blocks().size() > 0 ? mff->m_chain.get_blocks().back()->m_hash.ToString().c_str() : "???");
            }
            fputc('\n', stdout);
        }
        static int apboll = 0; apboll++; if (apboll > 100000) break;
    }
    int64_t recorded_end_time = f.m_current_time;
    int64_t end_time = GetTime();
    printf("%s: ----log ends----\n", time_string(recorded_end_time).c_str());
    int64_t elapsed = recorded_end_time - internal_start_time;
    int64_t htotal = elapsed / 3600;
    int64_t days = elapsed / 86400;
    int64_t hours = (elapsed % 86400) / 3600;
    printf("start = %ld (%lld)\n"
           "end   = %lld (%lld)\n", internal_start_time, start_time, recorded_end_time, end_time);
    printf("%" PRIu64 " entries over %" PRIi64 " days, %" PRIi64 " hours parsed in %" PRIi64 " seconds (%" PRIi64 " entries/s, or %" PRIi64 " hours/real second)\n", entries, days, hours, end_time - start_time, entries / (end_time - start_time), htotal / (end_time - start_time));

    uint64_t total = azr.total_bytes;
    uint64_t counted = total;
    printf("%-25s   %-10s (%-6s) [%-8s (%-6s)]\n", "category", "bytes", "%", "count", "%");
    printf("=========================   ==========  ======   ========  ======  \n");
    for (auto& x : azr.usage) {
        counted -= x.second;
        uint64_t count = azr.count[x.first];
        printf("%25s : %-10zu (%5.2f%%) [%-8" PRIi64 " (%5.2f%%)]\n", bitcoin::cmd_string(x.first).c_str(), x.second, 100.0 * x.second / total, count, 100.0 * count / entries);
        if (x.first == bitcoin::mff::cmd_mempool_in) {
            uint64_t count2 = azr.total_txrecs;
            uint64_t amount = azr.total_txrec_bytes;
            printf("%25s : %-10llu (%5.2f%%) [%-8" PRIi64 " (%5.2f%%)]\n", "(tx recordings)", amount, 100.0 * amount / total, count2, 100.0 * count2 / entries);
            amount = x.second - amount;
            count2 = count - count2;
            printf("%25s : %-10llu (%5.2f%%) [%-8" PRIi64 " (%5.2f%%)]\n", "(tx references)", amount, 100.0 * amount / total, count2, 100.0 * count2 / entries);
        }
    }
    printf("unaccounted: %-10" PRIi64 " (%.2f%%)\n", counted, 100.0 * counted / total);

    printf("timestamp at end: %ld\n", f.m_current_time);
}

inline std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

bool txid_in_vtx(const uint256& txid, const std::vector<std::shared_ptr<bitcoin::tx>>& vtx) {
    for (const auto& x : vtx) if (x->m_hash == txid) return true;
    return false;
}
