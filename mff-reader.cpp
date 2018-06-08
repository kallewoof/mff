#include <set>

#include <txmempool_format.h>
#include <cliargs.h>
#include <tinytx.h>

std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

typedef std::string (*txid_str_f)(const uint256& txid);

std::string txid_long(const uint256& txid) { return txid.ToString(); }

std::string txid_short(const uint256& txid) {
    static std::set<uint256> seen;
    if (seen.find(txid) == seen.end()) {
        seen.insert(txid);
        return txid_long(txid);
    }
    return txid.ToString().substr(0, 10);
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
    ca.parse(argc, argv);

    bool piping = mff_piping = !isatty(fileno(stdin));

    if (ca.m.count('h') || ca.l.size() < 1 + (!piping)) {
        fprintf(stderr, "syntax: %s [--help|-h] [--long|-l] [--verbose|-v] [--depth=<depth>|-d<depth>] <mff file> <txid> [<txid> [...]]\n", argv[0]);
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
    do {
        bool count = true;
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
                    printf(" (first seen %s%s - %llu vbytes, %llu fee, %.3lf fee rate (s/vbyte), block #%u)\n", txid_str(t->id).c_str(), extra.c_str(), t->vsize(), t->fee, t->feerate(), reader->active_chain.height);
                    // const mff::tx& t = *reader->txs[reader->seqs[txid]];
                    // printf(" (txid seq=%llu) %s", reader->seqs[txid], t->to_string().c_str());
                    // for (const auto& x : t->vin) if (x.is_known()) printf("\n- %llu = %s", x.get_seq(), reader->txs[x.get_seq()]->id.ToString().c_str());
                    // we don't wanna bother with tracking TX_REC for multiple txids so we just break the loop here
                    break;
                } else if (reader->last_cmd == mff::TX_CONF) {
                    printf(" (%s in #%u=%s)", txid_str(txid).c_str(), reader->active_chain.height, reader->active_chain.chain.size() > 0 ? reader->active_chain.chain.back()->hash.ToString().c_str() : "???");
                } else if (reader->last_cmd == mff::TX_IN) {
                    printf(" (%s)", txid_str(reader->last_recorded_tx->id).c_str());
                }
                fputc('\n', stdout);
            }
            count = false;
        }
        entries++;
        nooutputiters++;
        if (nooutputiters > 10000) {
            printf("%s\r", time_string(reader->last_time).c_str());
            fflush(stdout);
        }
    } while (reader->read_entry());
    printf("%s: ----log ends----\n", time_string(reader->last_time).c_str());
    printf("%llu entries parsed\n", entries);
    printf("txid hits:\n");
    uint32_t max = 0;
    for (const auto& th : reader->txid_hits) {
        if (th.second > max || (th.second == max && max > 4)) {
            printf("%s: %6u\n", th.first.ToString().c_str(), th.second);
            max = th.second;
        }
    }
    uint64_t total = (uint64_t)reader->tell();
    uint64_t counted = total;
    for (auto& x : reader->space_usage) {
        counted -= x.second;
        printf("%10s : %-10llu (%.2f%%)\n", mff::cmd_str((mff::CMD)x.first).c_str(), x.second, 100.0 * x.second / total);
    }
    printf("unaccounted: %-10llu (%.2f%%)\n", counted, 100.0 * counted / total);
    delete reader;
}
