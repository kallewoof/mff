#include <set>

#include <txmempool_format.h>
#include <txmempool_format_rs.h>
#include <txmempool_format_aj.h>
#include <txmempool_format_ajb.h>
#include <txmempool_format_timetail.h>
#include <txmempool_format_simpletime.h>
#include <txmempool_format_relseq.h>
#include <cliargs.h>
#include <tinytx.h>
#include <tinyrpc.h>
#include <amap.h>

#undef time_rel_value

#include <bcq/bitcoin.h>
#include <bcq/utils.h>

inline std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

class cqdb_listener : public mff::listener_callback {
public:
    uint64_t m_entries = 0;
    long& m_timestamp;
    std::shared_ptr<bitcoin::mff> m_mff;
    cqdb_listener(long& timestamp, std::shared_ptr<bitcoin::mff> mff) : m_timestamp(timestamp), m_mff(mff) {}
    std::shared_ptr<bitcoin::tx> convert_tx(const mff::tx& x) {
        auto rv = m_mff->tretch(x.id);
        if (rv) return rv;
        rv = std::make_shared<bitcoin::tx>(m_mff.get());
        rv->m_fee = x.fee;
        rv->m_hash = x.id;
        rv->m_weight = x.weight;
        auto& vin = rv->m_vin;
        for (auto& i : x.vin) {
            vin.emplace_back(i.n, i.txid);
        }
        rv->m_vout = x.amounts;
        return rv;
    }
    void discard(const mff::tx& tx, std::vector<uint8_t> txdata, uint8_t reason, const uint256* cause) {
        auto ex = convert_tx(tx);
        m_mff->tx_discarded(m_timestamp, ex, txdata, reason, cause ? m_mff->tretch(*cause) : nullptr);
    }

    virtual void tx_rec(mff::seqdict_server* source, const mff::tx& x) {
        ++m_entries;
        m_mff->tx_entered(m_timestamp, convert_tx(x));
    }
    virtual void tx_in(mff::seqdict_server* source, const mff::tx& x) {
        ++m_entries;
        m_mff->tx_entered(m_timestamp, convert_tx(x));
    }

    virtual void tx_out(mff::seqdict_server* source, const mff::tx& x, mff::tx::out_reason_enum reason) {
        // do we know this transaction?
        auto hash = x.id;
        bool known = m_mff->m_references.count(hash);
        if (known) ++m_entries;
        switch (reason) {
        case mff::tx::out_reason_age_expiry:
            if (known) m_mff->tx_left(m_timestamp, m_mff->tretch(hash), bitcoin::mff::reason_expired);
            return;
        case mff::tx::out_reason_low_fee: // MemPoolRemovalReason::SIZELIMIT:
            if (known) m_mff->tx_left(m_timestamp, m_mff->tretch(hash), bitcoin::mff::reason_sizelimit);
            return;
        case mff::tx::out_reason_unknown: // MemPoolRemovalReason::UNKNOWN:   //! Manually removed or unknown reason
        default:
            if (!known) ++m_entries;
            return m_mff->tx_left(m_timestamp, m_mff->tretch(hash), bitcoin::mff::reason_unknown, nullptr);
        }
    }
    virtual void tx_invalid(mff::seqdict_server* source, const mff::tx& x, std::vector<uint8_t> txdata, mff::tx::invalid_reason_enum reason, const uint256* cause = nullptr) {
        // do we know this transaction?
        auto hash = x.id;
        bool known = m_mff->m_references.count(hash);
        if (known) ++m_entries;
        switch (reason) {
        case mff::tx::invalid_reorg:
            if (known) m_mff->tx_left(m_timestamp, m_mff->tretch(hash), bitcoin::mff::reason_reorg);
            return;
        case mff::tx::invalid_doublespent: //  MemPoolRemovalReason::CONFLICT:  //! Removed for conflict with in-block transaction
            if (!known) ++m_entries;
            return discard(x, txdata, bitcoin::mff::reason_conflict, cause);
        case mff::tx::invalid_rbf_bumped: // MemPoolRemovalReason::REPLACED:  //! Removed for replacement
            if (!known) ++m_entries;
            return discard(x, txdata, bitcoin::mff::reason_replaced, cause);
        case mff::tx::invalid_unknown: // MemPoolRemovalReason::UNKNOWN:   //! Manually removed or unknown reason
        default:
            if (!known) ++m_entries;
            // If there is a cause, we use invalid, otherwise out
            if (cause) {
                return discard(x, txdata, bitcoin::mff::reason_unknown, cause);
            }
            return m_mff->tx_left(m_timestamp, m_mff->tretch(hash), bitcoin::mff::reason_unknown, nullptr);
        }
    }
    virtual void block_confirm(mff::seqdict_server* source, const mff::block& b) {
        ++m_entries;
        std::set<std::shared_ptr<bitcoin::tx>> confirmed_txs;
        for (const auto& x : b.known) {
            confirmed_txs.insert(convert_tx(*source->txs.at(x)));
        }
        for (const auto& x : b.unknown) {
            confirmed_txs.insert(m_mff->tretch(x) ?: std::make_shared<bitcoin::tx>(m_mff.get(), x));
        }
        m_mff->confirm_block(m_timestamp, b.height, b.hash, confirmed_txs);
    }
    virtual void block_unconfirm(mff::seqdict_server* source, uint32_t height) {
        ++m_entries;
        while (m_mff->m_chain.m_tip > height) m_mff->unconfirm_tip(m_timestamp);
    }
};

inline mff::mff* alloc_mff_from_format(const std::string& fmt, const std::string& path, bool readonly) {
    #define ifexpr_static_multi(suffix, name) \
    if (fmt == name) { \
        static size_t idx = 0; \
        idx++; \
        if (idx == 1) return new mff::mff_##suffix<0>(path, readonly); \
        else if (idx == 2) return new mff::mff_##suffix<1>(path, readonly); \
        else return new mff::mff_##suffix<2>(path, readonly); \
    }

    ifexpr_static_multi(fr, "mff")
    else ifexpr_static_multi(rseq, "relseq")
    //
    // static size_t rseq_idx = 0;
    // static size_t fr_idx = 0;
    // if (fmt == "mff") {
    //     fr_idx++;
    //     if (fr_idx == 1) return new mff::mff_rseq<0>(path, readonly);
    //     else if (rseq_idx == 2) return new mff::mff_rseq<1>(path, readonly);
    //     else return new mff::mff_rseq<2>(path, readonly);
    // } else if (fmt == "relseq") {
    //     rseq_idx++;
    //     if (rseq_idx == 1) return new mff::mff_rseq<0>(path, readonly);
    //     else if (rseq_idx == 2) return new mff::mff_rseq<1>(path, readonly);
    //     else return new mff::mff_rseq<2>(path, readonly);
    // }
    else if (fmt == "mff-tt") return new mff::mff_rseq_tt<0>(path, readonly);
    else if (fmt == "mff-rs") return new mff::mff_rs(path, readonly);
    else if (fmt == "mff-st") return new mff::mff_simpletime_rseq<0>(path, readonly);
    else if (fmt == "aj-dump") return new mff::mff_aj(path, readonly);
    else if (fmt == "ajb") return new mff::mff_ajb(path, readonly);
    return nullptr;
}

bool needs_newline = false;
bool mff_piping = false;
tiny::rpc* rpc = nullptr;

int main(int argc, char* const* argv) {
    cliargs ca;
    ca.add_option("help", 'h', no_arg);
    ca.add_option("verbose", 'v', no_arg);
    ca.add_option("rpccall", 'r', req_arg);
    ca.add_option("amap", 'm', req_arg);
    // ca.add_option("do-not-append", 'x', no_arg);
    ca.add_option("mempool-file", 'p', req_arg);
    ca.parse(argc, argv);

    if (ca.m.count('h') || ca.l.size() < 3 || !(ca.l.size() & 1)) {
        fprintf(stderr, "syntax: %s [--help|-h] [--verbose|-v] [--do-not-append|-x] [--mempool-file|-p] [--rpccall|-r] <input-format> <input-path> [<input-format-2> <input-path-2> [...]] <output-path>\n", argv[0]);
        fprintf(stderr,
            "available formats:\n"
            "  mff       Previous Memory File Format, Before Switch to CQDB\n"
            "  mff-tt    Time-tail MFF (Rev 2018-05-27)\n"
            "  mff-rs    Reused sequence based Memory File Format\n"
            "  mff-st    Simple-time MFF (Rev 2018-06-08)\n"
            "  relseq    Relative sequence based MFF (Rev 2019-04-09)\n"
            "  aj-dump   AJ mempool history dump (ZMQ)\n"
            "  ajb       Binary AJ mempool history dump\n"
        );
        return 1;
    }

    if (ca.m.count('r')) {
        rpc = new tiny::rpc(ca.m['r']);
    }

    if (ca.m.count('m')) {
        amap::amap_path = ca.m['m'];
        amap::enabled = true;
    }

    std::string mempool_file = ca.m.count('p') ? ca.m['p'] : "";

    // // bool verbose = ca.m.count('v');
    // bool overwrite_output = ca.m.count('x');

    // if (overwrite_output) {
    //     // truncate file to 0 bytes
    //     fclose(fopen(ca.l[3], "wb"));
    // }

    // check file sizes
    FILE* fp = fopen(ca.l[1], "rb");
    if (!fp) {
        fprintf(stderr, "file not found: %s\n", ca.l[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long in_bytes = ftell(fp);
    fclose(fp);

    long internal_time = 0;

    mff::cluster c;
    for (size_t i = 0; i + 2 < ca.l.size(); i += 2) {
        mff::mff* in = alloc_mff_from_format(ca.l[i], ca.l[i + 1], true);
        if (!in) {
            fprintf(stderr, "invalid format: %s\n", ca.l[i]);
            return 1;
        }
        c.add(in);
        in->conversion_source = true;
        in->tag = ca.l.size() > 4 ? strprintf("IN#%zu", i/2) : "IN  ";
    }
    auto out = std::make_shared<bitcoin::mff>(ca.l[ca.l.size()-1]);
    printf("initializing output..."); fflush(stdout);
    out->load();
    printf("\n");
    cqdb_listener lnr(internal_time, out);
    bitcoin::mff_analyzer azr;
    out->m_delegate = &azr;
    azr.enable_touchmap = true;
    if (!out->m_file) out->begin_segment(0);
    // mff::mff* out = alloc_mff_from_format(ca.l[ca.l.size()-2], ca.l[ca.l.size()-1], false);
    // if (!out) {
    //     fprintf(stderr, "invalid format: %s\n", ca.l[ca.l.size()-2]);
    //     return 1;
    // }
    // out->tag = "OUT ";

    c.ffwd_to_time(out->m_current_time);

    // map cqdb listener
    for (auto& n : c.nodes) n->listener = &lnr;

    // map in mempool to out
    bitcoin::mff_mempool_callback mempool_callback(internal_time, out);
    c.mempool->callback = &mempool_callback;
    // c.mempool->callback = out;

    // load mempool unless overwriting output, if provided and if the file exists
    if (mempool_file != "") {
        FILE* fp = fopen(mempool_file.c_str(), "rb");
        if (fp) {
            fclose(fp);
            c.nodes[0]->load_mempool(mempool_file);
        }
    }

    size_t out_read = 0;

    int64_t c_start_time = 0;
    uint32_t entries = 0;
    int64_t last_time = out->m_current_time;
    int64_t start_time = GetTime();
    int64_t entry_start = 0;
    for (auto& n : c.nodes) {
        n->shared_time = &internal_time;
    }
    // in->shared_time = out->shared_time = &internal_time;
    int64_t timepoint_a, timepoint_b, internalpoint_a, internalpoint_b;
    timepoint_a = timepoint_b = start_time;
    internalpoint_a = internalpoint_b = 0;
    while (c.read_entry()) {
        if (!internalpoint_a) {
            internalpoint_a = internalpoint_b = c.time;
        }
        if (!c_start_time) c_start_time = c.time;
        if (last_time) {
            if (c.time < last_time) {
                // we cannot allow jumping back in time
                fprintf(stderr, "*** the output file (%s) ends at timestamp\n\t%" PRIi64 "\nwhere the input file (%s%s) starts at timestamp\n\t%" PRIi64 "\n*** writing to the output file would result in a jump back in time, which is not allowed. to do this you must do a 3-file merge, i.e. include two input files and a separate output file: mff-conv a-fmt a-name b-fmt b-name out-fmt out-name\n",
                    ca.l[ca.l.size()-1],                // output file (%s
                    last_time,                          // ) ends at timestamp\n\t%" PRIi64 "
                    ca.l[1],                            // \nwhere the input file (%s
                    ca.l.size() > 4 ? "[, ...]" : "",   // %s
                    c.time                              // ) starts at timestamp\n\t%" PRIi64 "
                );
                exit(1);
            }
            last_time = 0;
        }
        if (!entry_start) entry_start = internal_time;// e->time;
        // out->write_entry(e);
        entries++;
        if (!(entries % 100)) {
            long pos = c.nodes[0]->tell();
            float done = (float)(100 * pos) / in_bytes;
            int64_t now = GetTime();
            int64_t elapsed = now - timepoint_a; // real time passed
            int64_t mff_time_elapsed = internal_time - internalpoint_a; // input file time passed
            // we wanna see how many mff seconds pass per real second,
            // i.e. mff_time_elapsed / elapsed
            float s_per_s = !elapsed ? 0 : float(mff_time_elapsed) / elapsed;
            printf(" %5.1f%% %6u %s [%u -> %" PRIu64 ", %7.3fx]   \r", done, out->m_chain.m_tip, time_string(internal_time).c_str(), entries, lnr.m_entries, s_per_s);
            fflush(stdout);
            needs_newline = true;
            if (elapsed > 20 && now - timepoint_b > 10) {
                // switch perspectives
                timepoint_a = timepoint_b;
                internalpoint_a = internalpoint_b;
                timepoint_b = now;
                internalpoint_b = internal_time;
            }
        }
    }
    out->flush();
    printf("%s", nl());
    // save mempool, if provided
    if (mempool_file != "") {
        c.nodes[0]->save_mempool(mempool_file);
    }
    printf("skipped recs = %" PRIu64 "\n", skipped_recs);
    if (c_start_time) {
        int64_t end_time = internal_time + 1800; // rounding
        int64_t elapsed = end_time - c_start_time;
        int64_t days = elapsed / 86400;
        int64_t hours = (elapsed % 86400) / 3600;
        printf("spans over %" PRIi64 " days, %" PRIi64 " hours\n", days, hours);
    }
    uint64_t total = azr.total_bytes;
    uint64_t counted = total;
    for (auto& x : azr.usage) {
        counted -= x.second;
        printf("%10s : %-10ld (%.2f%%)\n", bitcoin::cmd_string(x.first).c_str(), x.second, 100.0 * x.second / total);
    }
    printf("unaccounted: %-10" PRIi64 " (%.2f%%)\n", counted, 100.0 * counted / total);
    printf("timestamp at end: %ld\n", out->m_current_time);
}
