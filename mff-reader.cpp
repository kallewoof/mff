#include <set>

#include <txmempool_format.h>
#include <cliargs.h>
#include <tinytx.h>
#include <hash.h>

std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

typedef std::string (*txid_str_f)(const uint256& txid);

static std::set<uint256> seen;

std::string txid_long(const uint256& txid) { return txid.ToString(); }

std::string txid_short(const uint256& txid) {
    if (seen.find(txid) == seen.end()) {
        seen.insert(txid);
        return txid_long(txid);
    }
    return txid.ToString().substr(0, 10);
}
std::string hash_once(const uint256& hash, const std::string& prefix = "") {
    if (seen.find(hash) == seen.end()) {
        seen.insert(hash);
        return prefix + txid_long(hash);
    }
    return "";
}
txid_str_f txid_str = txid_short;

struct tracked {
    uint256 txid;
    int depth;
    tracked(const uint256& txid_in, int depth_in = 0) : txid(txid_in), depth(depth_in) {}
    tracked(const tracked& origin, const uint256& txid_in) : tracked(txid_in, origin.depth - 1) {}
    bool operator<(const tracked& other) const { return txid<other.txid; }
    bool operator==(const tracked& other) const { return txid==other.txid; }
};

bool needs_newline = false;
bool mff_piping = false;

int main(int argc, char* const* argv) {
    // sanity check
    {
        tiny::tx tx;
        std::vector<uint8_t> data = ParseHex("0100000001e369193a3892832248b9f8aa82fb328d72ab586d8232e7abe328f4f7dac4cb82000000006a4730440220400ea18e4599c7ee3adf854e686e839f37018db417c0c159f0d06c0e5770a4330220510aa49debfb1a5e24f292c5a158584b62164e3a8e29e1477e599461fbde65c40121027b538a03778eea62a8f2c55aec9d8b94b4e5914edc244b7b298fd97ef6c4a9dcfdffffff025798bf07000000001976a914a737fcf80f8b79b53ef8ec8eb2e2cdf087aa701c88ac942dc878000000001976a9141feede8623007b60316b6166a35601805bad1cef88acdcfb0700");
        CDataStream ds(data, SER_GETHASH, PROTOCOL_VERSION);
        ds >> tx;
        assert(tx.hash == uint256S("a9311d2a6b2272f90ac42296fefe89f22ba7bb5042e297f4fa8ae24e80da676a"));
    }
    cliargs ca;
    ca.add_option("help", 'h', no_arg);
    ca.add_option("long", 'l', no_arg);
    ca.add_option("verbose", 'v', no_arg);
    ca.add_option("depth", 'd', req_arg);
    ca.add_option("count", 'c', no_arg);
    ca.parse(argc, argv);

    bool piping = mff_piping = !isatty(fileno(stdin));

    if (ca.m.count('h') || ca.l.size() < 1 + (!piping)) {
        fprintf(stderr, "syntax: %s [--help|-h] [--long|-l] [--verbose|-v] [--count|-c] [--depth=<depth>|-d<depth>] <mff file> <txid> [<txid> [...]]\n", argv[0]);
        return 1;
    }

    if (ca.m.count('l')) txid_str = txid_long;
    bool verbose = ca.m.count('v');
    int depth = 0;
    if (ca.m.count('d')) {
        depth = atoi(ca.m['d']);
        printf("depth = %d\n", depth);
    }

    std::string infile = ca.l[0];
    std::set<tracked> txids;
    for (size_t i = !piping; i < ca.l.size(); ++i) {
        txids.insert(tracked{uint256S(ca.l[i]), depth});
    }
    mff::mff_rseq<0>* reader = piping ? new mff::mff_rseq<0>(stdin) : new mff::mff_rseq<0>(infile);
    reader->read_entry();
    printf("%s: ---log starts---\n", time_string(reader->last_time).c_str());
    uint64_t entries = 0;
    uint32_t nooutputiters = 0;
    bool enable_counting = ca.m.count('c') > 0;
    int64_t start_time = GetTime();
    int64_t recorded_start_time = reader->last_time;
    do {
        bool count = enable_counting;
        for (const tracked& t : txids) {
            const uint256& txid = t.txid;
            if (reader->touched_txid(txid, count)) {
                nooutputiters = 0;
                if (reader->last_cmd == mff::TX_INVALID && txid == reader->get_replacement_txid() && txids.find(reader->get_invalidated_txid()) != txids.end()) {
                    // if we have both the invalidated and replacement txids in the txids set,
                    // we skip the replacement version or we end up showing the same entry
                    // twice
                    continue;
                }
                printf("%s: %s", time_string(reader->last_time).c_str(), cmd_str(reader->last_cmd).c_str());
                if (reader->last_cmd == mff::TX_INVALID) {
                    printf(" %s (%s)", txid_str(reader->get_invalidated_txid()).c_str(), tx_invalid_state_str(reader->last_invalid_state));
                    if (reader->replacement_seq != 0 || !reader->replacement_txid.IsNull()) {
                        uint256 replacement = reader->get_replacement_txid();
                        txids.insert(tracked{replacement, t.depth});
                        printf(" -> %s", txid_str(replacement).c_str());
                    }
                    if (verbose) {
                        printf("\n\t%s", HexStr(reader->last_invalidated_txhex).c_str());
                    }
                } else if (reader->last_cmd == mff::TX_OUT) {
                    printf(" %s (%s)", txid_str(reader->get_invalidated_txid()).c_str(), tx_out_reason_str(reader->last_out_reason));
                } else if (reader->last_cmd == mff::TX_REC) {
                    const auto& t = reader->last_recorded_tx;
                    // printf("\n\t%s\n", t->to_string().c_str());
                    std::string extra = "";
                    // check if any of our targeted tx's is spent by this tx
                    for (const tracked& tracked2 : txids) {
                        uint32_t index;
                        if (t->spends(tracked2.txid, reader->seqs[tracked2.txid], index)) {
                            extra += std::string(" (spends ") + txid_str(tracked2.txid) + ":" + std::to_string(index) + ")";
                            if (tracked2.depth > 0 && txids.find(tracked{t->id}) == txids.end()) {
                                txids.insert(tracked{tracked2, t->id});
                            }
                        }
                    }
                    if (!t->is(txid) && extra == "") {
                    //     // printf(" (");
                    // } else if (t->spends(txid, reader->seqs[txid])) {
                    //     extra = std::string(" (spends ") + txid_str(txid) + ")";
                    //     // printf(" (spends %s: ", txid_str(txid).c_str());
                    // } else {
                        extra = " (?)";
                        // printf(" (???: ");
                    }
                    printf(" (first seen %s%s - %" PRIu64 " vbytes, %" PRIu64 " fee, %.3lf fee rate (s/vbyte), block #%u)\n", txid_str(t->id).c_str(), extra.c_str(), t->vsize(), t->fee, t->feerate(), reader->active_chain.height);
                    // const mff::tx& t = *reader->txs[reader->seqs[txid]];
                    // printf(" (txid seq=%" PRIu64 ") %s", reader->seqs[txid], t->to_string().c_str());
                    // for (const auto& x : t->vin) if (x.is_known()) printf("\n- %" PRIu64 " = %s", x.get_seq(), reader->txs[x.get_seq()]->id.ToString().c_str());
                    // we don't wanna bother with tracking TX_REC for multiple txids so we just break the loop here
                    break;
                } else if (reader->last_cmd == mff::BLOCK_CONF) {
                    printf(" (%s in #%u%s)", txid_str(txid).c_str(), reader->active_chain.height, reader->active_chain.chain.size() > 0 ? hash_once(reader->active_chain.chain.back()->hash, "=").c_str() : "???");
                } else if (reader->last_cmd == mff::TX_IN) {
                    printf(" (%s)", txid_str(reader->last_recorded_tx->id).c_str());
                }
                fputc('\n', stdout);
            }
            count = false;
        }
        entries++;
        if (!mff_piping) {
            nooutputiters++;
            if (nooutputiters > 10000) {
                nooutputiters = 0;
                printf("%s\r", time_string(reader->last_time).c_str());
                fflush(stdout);
            }
        }
    } while (reader->read_entry());
    int64_t recorded_end_time = reader->last_time;
    int64_t end_time = GetTime();
    printf("%s: ----log ends----\n", time_string(reader->last_time).c_str());
    int64_t elapsed = recorded_end_time - recorded_start_time;
    int64_t htotal = elapsed / 3600;
    int64_t days = elapsed / 86400;
    int64_t hours = (elapsed % 86400) / 3600;
    printf("%" PRIu64 " entries over %" PRIi64 " days, %" PRIi64 " hours parsed in %" PRIi64 " seconds (%" PRIi64 " entries/s, or %" PRIi64 " hours/real second)\n", entries, days, hours, end_time - start_time, entries / (end_time - start_time), htotal / (end_time - start_time));
    if (enable_counting) {
        printf("txid hits:\n");
        uint32_t max = 0;
        for (const auto& th : reader->txid_hits) {
            if (th.second > max || (th.second == max && max > 4)) {
                printf("%s: %6u\n", th.first.ToString().c_str(), th.second);
                max = th.second;
            }
        }
    } else {
        printf("(no txid hit data available; re-run with --count flag)\n");
    }
    uint64_t total = (uint64_t)reader->tell();
    uint64_t counted = total - 2; // magic number
    printf("%-10s   %-10s (%-6s) [%-8s (%-6s)]\n", "category", "bytes", "%", "count", "%");
    printf("==========   ==========  ======   ========  ======  \n");
    for (auto& x : reader->space_usage) {
        counted -= x.second;
        uint64_t count = reader->cmd_usage[x.first];
        printf("%10s : %-10" PRIi64 " (%5.2f%%) [%-8" PRIi64 " (%5.2f%%)]\n", mff::cmd_str((mff::CMD)x.first).c_str(), x.second, 100.0 * x.second / total, count, 100.0 * count / entries);
    }
    printf("unaccounted: %-10" PRIi64 " (%.2f%%)\n", counted, 100.0 * counted / total);
    delete reader;
}
