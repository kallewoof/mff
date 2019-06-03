#include <vector>
#include <memory>
#include <uint256.h>
#include <utiltime.h>

#include <bcq/bitcoin.h>

#include <streams.h>
#include <tinytx.h>

inline char* time_string(int64_t time);
inline char* size_string(long size);

bool txid_in_vtx(const uint256& txid, const std::vector<std::shared_ptr<bitcoin::tx>>& vtx);

std::string txid_str(const uint256& txid) { return txid.ToString(); }
void parse_range(const char* expr, uint32_t& block_start, uint32_t& block_end, int64_t& time_start, int64_t& time_end);

int main(int argc, const char** argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "syntax: %s <db path> <txid> [blocks=<block-range>] [period=<time-range>]\n", argv[0]);
        return 1;
    }

    uint32_t block_start = 0;
    uint32_t block_end = 0;
    int64_t time_start = 0;
    int64_t time_end = 0;

    const auto& dbpath = argv[1];
    const auto& txidstr = argv[2];
    if (argc > 3) parse_range(argv[3], block_start, block_end, time_start, time_end);

    const uint256 txid = uint256S(argv[2]);

    // mff handles the mempool file format disk I/O; it is our source in this case
    auto mff = std::make_shared<bitcoin::mff>(argv[1], bitcoin::mff::detect_prefix(argv[1]), 2016, true);
    bitcoin::mff& f = *mff;
    f.load();

    // we will use the built-in mff_analyzer to determine if we found our tx
    bitcoin::mff_analyzer azr;
    mff->m_delegate = &azr;

    if (block_start == 0 && time_start == 0) {
        // rewind to the beginning
        f.rewind();
    } else if (block_start) {
        // go to block cluster
        uint32_t starting_block = (block_start / 2016) * 2016;
        f.goto_segment(starting_block);
    } else {
        // go to time
        fprintf(stderr, "time range not yet implemented but it'll be great; in the meantime, using starting time 0 (end time is supported)\n");
        exit(1);
    }
    // f.goto_segment(546336);

    // start iterating through; if we encounter the transaction, show info about it
    long internal_start_time = 0;
    uint32_t first_block = 0;
    uint32_t last_block = 0;
    uint64_t entries = 0;
    int64_t start_time = GetTime();
    std::set<uint256> touched_txids;
    azr.enable_touchmap = true;
    while (f.iterate()) {
        if (block_end && f.m_chain.m_tip > block_end) break;
        if (time_end && f.m_current_time > time_end) break;
        if (!internal_start_time) {
            internal_start_time = f.m_current_time;
            printf("%s: ----log begins----\n", time_string(internal_start_time));
        }
        if (!first_block) first_block = f.m_chain.m_tip;
        entries++;
        if (!(entries % 1000)) {
            cq::id cluster = mff->get_registry().m_current_cluster;
            uint32_t block_height = mff->m_chain.m_tip;
            long pos2 = mff->m_file->tell();
            printf(" %sE %sB : %s <cluster=%" PRIid " block=%u>     \r", size_string(entries), size_string(pos2), time_string(mff->m_current_time), cluster, block_height);
            fflush(stdout);
        }

        azr.populate_touched_txids(touched_txids);
        if (touched_txids.count(txid) /*|| azr.last_command == bitcoin::mff::cmd_mempool_invalidated*/) {
            printf("%s: %s", time_string(mff->m_current_time), bitcoin::cmd_string(azr.last_command).c_str());
            if (azr.last_command == bitcoin::mff::cmd_mempool_invalidated) {
                tiny::tx inv;
                CDataStream ds(azr.last_rawtx, SER_DISK, PROTOCOL_VERSION);
                ds >> inv;
                printf(" %s (%s)\n%s", txid_str(azr.last_txids.back()).c_str(), bitcoin::reason_string(azr.last_reason).c_str(), inv.ToString().c_str());
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
                    printf(" (first seen %s%s - %" PRIu64 " vbytes, %" PRIu64 " fee, %.3lf fee rate (sat/vb), block #%u)", txid_str(t->m_hash).c_str(), extra.c_str(), t->vsize(), t->m_fee, t->feerate(), mff->m_chain.m_tip);
                    // const mff::tx& t = *azr.txs[azr.seqs[txid]];
                    // printf(" (txid seq=%" PRIu64 ") %s", azr.seqs[txid], t->to_string().c_str());
                    // for (const auto& x : t->vin) if (x.is_known()) printf("\n- %" PRIu64 " = %s", x.get_seq(), azr.txs[x.get_seq()]->id.ToString().c_str());
                    // we don't wanna bother with tracking TX_REC for multiple txids so we just break the loop here
                } else {
                    // re-entry
                    printf(" (%s)", txid_str(azr.last_txids.back()).c_str());
                }
            } else if (azr.last_command == bitcoin::mff::cmd_block_mined) {
                printf(" (%s in #%u=%s)", txid_str(txid).c_str(), mff->m_chain.m_tip, mff->m_chain.get_blocks().size() > 0 ? mff->m_chain.get_blocks().back()->m_hash.ToString().c_str() : "???");
            }
            fputc('\n', stdout);
        }
    }
    last_block = f.m_chain.m_tip;
    int64_t recorded_end_time = f.m_current_time;
    int64_t end_time = GetTime();
    end_time += end_time == start_time;
    printf("\n%s: ----log ends----\n", time_string(recorded_end_time));
    int64_t elapsed = recorded_end_time - internal_start_time;
    int64_t htotal = elapsed / 3600;
    int64_t days = elapsed / 86400;
    int64_t hours = (elapsed % 86400) / 3600;
    uint32_t blocks = 1 + last_block - first_block;
    printf("start = %ld (%lld) [height %u]\n"
           "end   = %lld (%lld) [height %u]\n", internal_start_time, start_time, first_block, recorded_end_time, end_time, last_block);
    printf("%" PRIu64 " entries over %" PRIi64 " days, %" PRIi64 " hours (%u blocks) parsed in %" PRIi64 " seconds (%" PRIi64 " entries/s, or %" PRIi64 " hours/real second, or %u blocks/minute)\n", entries, days, hours, blocks, end_time - start_time, entries / (end_time - start_time), htotal / (end_time - start_time), uint32_t(blocks * 60 / (end_time - start_time)));

    uint64_t total = azr.total_bytes;
    uint64_t counted = total;
    printf("%-25s   %-10s (%-6s) [%-8s (%-6s)] {%-10s}\n", "category", "bytes", "%", "count", "%", "avg bytes");
    printf("=========================   ==========  ======   ========  ======    ==========\n");
    #define P(category, bytes, count, avgbytes) printf("%-25s : %10zu (%5.2lf%%) [%8" PRIi64 " (%5.2lf%%)] {%10.2f}\n", category, bytes, 100.0 * (bytes) / total, count, 100.0 * (count) / entries, avgbytes);

    for (auto& x : azr.usage) {
        counted -= x.second;
        uint64_t count = azr.count[x.first];
        float avgbytes = (float)x.second / count;
        P(bitcoin::cmd_string(x.first).c_str(), x.second, count, avgbytes);
        if (x.first == bitcoin::mff::cmd_mempool_in) {
            uint64_t count2 = azr.total_txrecs;
            size_t amount = azr.total_txrec_bytes;
            float avgbytes2 = (float)amount / count2;
            P("    (tx recordings)", amount, count2, avgbytes2);
            amount = x.second - amount;
            count2 = count - count2;
            avgbytes2 = (float)amount / count2;
            P("    (tx references)", amount, count2, avgbytes2);
        }
    }
    printf("unaccounted: %10" PRIi64 " (%.2f%%)\n", counted, 100.0 * counted / total);

    printf("txid hits:\n");
    uint32_t max = 0;
    for (const auto& th : azr.touchmap) {
        if (th.second > max || (th.second == max && max > 4)) {
            printf("%s: %6u\n", th.first.ToString().c_str(), th.second);
            max = th.second;
        }
    }
}

inline char* time_string(int64_t time) {
    static char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

inline char* size_string(long size) {
    static char buf[2][32];
    static int which = 0;
    which = 1 - which;
    if (size < 104857) sprintf(buf[which], "%.2fk", (float)size/1024);
    else if (size < 107374182) sprintf(buf[which], "%.2fM", (float)size/1048576);
    else sprintf(buf[which], "%.2fG", (float)size/1073741824);
    return buf[which];
}

bool txid_in_vtx(const uint256& txid, const std::vector<std::shared_ptr<bitcoin::tx>>& vtx) {
    for (const auto& x : vtx) if (x->m_hash == txid) return true;
    return false;
}

void parse_range(const char* expr, uint32_t& block_start, uint32_t& block_end, int64_t& time_start, int64_t& time_end) {
    if (!strncmp("blocks=", expr, 6)) {
        // blocks range
        const char* range = &expr[7];
        if (sscanf(range, "%u-%u", &block_start, &block_end) != 2) {
            fprintf(stderr, "invalid block range (expected 'number-number', e.g. '123-124', got %s)\n", range);
            exit(1);
        }
    } else if (!strncmp("period=", expr, 6)) {
        // time range
        const char* range = &expr[7];
        if (sscanf(range, "%lld-%lld", &time_start, &time_end) != 2) {
            fprintf(stderr, "invalid time range (expected 'number-number', e.g. '123-124', got %s)\n", range);
            exit(1);
        }
    } else {
        fprintf(stderr, "invalid range type in expression \"%s\" (accept blocks and period)\n", expr);
        exit(1);
    }
}
