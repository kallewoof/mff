// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include <tinyblock.h>
#include <unistd.h>
#include <amap.h>

#include <txmempool_format_aj.h>

// static mff::seq_t debug_seq = 0;
// static bool debugging = false;
// static int64_t debug_time = 1516996219;
#define DEBLOG(args...) //if (debugging) printf(args)
// static uint256 debug_txid = uint256S("59aa5ee3db978ea8168a6973b505c31b3f5f4757330da4ef45da0f51a81c1fc9");
// #define DEBUG_SEQ 14937
#define DSL(s, fmt...) // if (s == DEBUG_SEQ) { printf("[SEQ] " fmt); }
// #define l(args...) if (active_chain.height == 521703) { printf(args); }
// #define l1(args...) if (active_chain.height == 521702) { printf(args); }

namespace mff {

std::string aj_rpc_call = "";

template<typename T>
inline bool find_erase(std::vector<T>& v, const T& e) {
    auto it = std::find(std::begin(v), std::end(v), e);
    if (it == std::end(v)) return false;
    v.erase(it);
    return true;
}

// bool showinfo = false;
#define mplinfo_(args...) // if (showinfo) { printf(args); }
#define mplinfo(args...) // if (showinfo) { printf("[MPL::info] " args); }
#define mplwarn(args...) printf("[MPL::warn] " args)
#define mplerr(args...)  fprintf(stderr, "[MPL::err]  " args)

#define read_time(t) \
    if (timerel) {\
        uint8_t r; \
        in >> r; \
        t = last_time + r; \
    } else { \
        static_assert(sizeof(t) == 8, #t " is of wrong type! Must be int64_t!"); \
        in >> t; \
    }

#define write_time(rel) do {\
        if (rel & CMD::TIME_REL_BIT) {\
            int64_t tfull = GetTime() - last_time;\
            uint8_t t = tfull <= 255 ? tfull : 255;\
            in << t;\
            last_time += t;\
        } else {\
            last_time = GetTime();\
            in << last_time;\
        }\
        sync();\
    } while (0)

#define write_set_time(rel, time) do {\
        if (rel & CMD::TIME_REL_BIT) {\
            int64_t tfull = time - last_time;\
            uint8_t t = tfull <= 255 ? tfull : 255;\
            in << t;\
            last_time += t;\
        } else {\
            last_time = time;\
            in << last_time;\
        }\
        sync();\
    } while (0)

#define read_txseq_keep(known, seq, h) \
    if (known) { \
        seq = seq_read(); \
    } else { \
        in >> h; \
        seq = seqs.count(h) ? seqs[h] : 0; \
    }

#define read_txseq(known, seq) \
    if (known) { \
        seq = seq_read(); \
    } else { \
        uint256 h; \
        in >> h; \
        seq = seqs.count(h) ? seqs[h] : 0; \
    }

#define write_txref(seq, id) \
    if (seq) { \
        seq_write(seq); \
    } else { \
        in << id; \
    }

inline FILE* setup_file(const char* path, bool readonly) {
    FILE* fp = fopen(path, readonly ? "r" : "r+");
    if (!readonly && fp == nullptr) {
        fp = fopen(path, "w");
    }
    if (fp == nullptr) {
        fprintf(stderr, "unable to open %s\n", path);
        assert(fp);
    }
    return fp;
}

mff_aj::mff_aj(const std::string path, bool readonly) : in_fp(setup_file(path.length() > 0 ? path.c_str() : (std::string(std::getenv("HOME")) + "/mff.out").c_str(), readonly)), in(in_fp, SER_DISK, 0) {
    nextseq = 1;
    last_seq = 0;
    lastflush = GetTime();
    buffer = (char*)malloc(1024);
    buffer_cap = 1024;
    entry_counter = 0;
    // out.debugme(true);
    mplinfo("start %s\n", path.c_str());
}

mff_aj::~mff_aj() {
}

void mff_aj::apply_block(std::shared_ptr<block> b) {
    // l1("apply block %u (%s)\n", b->height, b->hash.ToString().c_str());
    if (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
        mplwarn("dealing with TX_UNCONF missing bug 20180502153142\n");
        while (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
            mplinfo("unconfirming block #%u\n", active_chain.height);
            undo_block_at_height(active_chain.height);
        }
    }
    assert(active_chain.chain.size() == 0 || b->height == active_chain.height + 1);
    active_chain.height = b->height;
    active_chain.chain.push_back(b);
    assert(blocks[b->hash] == b);
    // l("block for #%u; %zu known, %zu unknown\n", b->height, b->known.size(), b->unknown.size());
    // we need to mark all transactions as confirmed
    for (auto seq : b->known) {
        DSL(seq, "block confirm\n");
        assert(txs.count(seq));
        txs[seq]->location = tx::location_confirmed;
        last_seqs.push_back(seq);
        // l("- %s\n", txs[seq]->id.ToString().c_str());
    }
}

void mff_aj::undo_block_at_height(uint32_t height) {
    mplinfo("undo block %u\n", height);
    // we need to unmark confirmed transactions
    assert(height == active_chain.height);
    auto b = active_chain.chain.back();
    active_chain.chain.pop_back();
    active_chain.height--;
    for (auto seq : b->known) {
        DSL(seq, "block orphan\n");
        assert(txs.count(seq));
        txs[seq]->location = tx::location_in_mempool;
        last_seqs.push_back(seq);
    }
}

inline std::string bits(uint8_t u) {
    std::string s = "";
    for (uint8_t i = 128; i; i>>=1) {
        s += (u & i) ? "1" : "0";
    }
    return s;
}

// static uint256 foo = uint256S("163ee79aa165df2dbd552d41e89abb266cf76b0763504e6ef582cb6df2e0befc");

seq_t mff_aj::touched_txid(const uint256& txid, bool count) {
    // static int calls = 0;calls++;
    if (count) {
        std::set<uint256> counted;
        // int i = 0;
        for (seq_t seq : last_seqs) {
            if (txs.count(seq) && counted.find(txs[seq]->id) == counted.end()) {
                txid_hits[txs[seq]->id]++;
                counted.insert(txs[seq]->id);
                // if (foo == txs[seq]->id) printf("%ld CALL %d: %s seq=%llu @%d : %u\n", ftell(in_fp), calls, cmd_str(last_cmd).c_str(), seq, i, txid_hits[txs[seq]->id]);
            }
            // i++;
        }
        if (last_cmd == TX_CONF) {
            // check last block txid list
            std::shared_ptr<block> tip = active_chain.chain.back();
            for (uint256& u : tip->unknown) {
                if (counted.find(u) == counted.end()) {
                    txid_hits[u]++;
                    counted.insert(u);
                }
                // if (foo == u) printf("%s conf height #%u : %u\n", cmd_str(last_cmd).c_str(), active_chain.height, txid_hits[u]);
            }
        }
    }
    for (seq_t seq : last_seqs) {
        if (txs.count(seq) && txs[seq]->id == txid) return seq;
    }
    if (last_cmd == TX_CONF) {
        // l("CONFIRMING BLOCK -- looking for %s\n", txid.ToString().c_str());
        // check last block txid list
        std::shared_ptr<block> tip = active_chain.chain.back();
        for (uint256& u : tip->unknown) {
            // l("- %s\n", u.ToString().c_str());
            if (u == txid) return true;
        }
        // l("DONE CONFIRMING BLOCK\n");
    }
    return 0;
}

uint256 mff_aj::get_replacement_txid() const {
    return replacement_seq && txs.count(replacement_seq) ? txs.at(replacement_seq)->id : replacement_txid;
}

uint256 mff_aj::get_invalidated_txid() const {
    return invalidated_seq && txs.count(invalidated_seq) ? txs.at(invalidated_seq)->id : invalidated_txid;
}

/////// RPC

inline FILE* rpc_fetch(const char* cmd, const char* dst, bool abort_on_failure = false) {
    if (aj_rpc_call == "") {
        assert(!"no RPC call available");
    }
    system(cmd);
    FILE* fp = fopen(dst, "r");
    if (!fp) {
        fprintf(stderr, "RPC call failed: %s\n", cmd);
        if (!abort_on_failure) {
            fprintf(stderr, "waiting 5 seconds and trying again\n");
            sleep(5);
            return rpc_fetch(cmd, dst, true);
        }
        assert(0);
    }
    return fp;
}

template<typename T>
inline void deserialize_hex_string(const char* string, T& object) {
    CDataStream ds(ParseHex(string), SER_DISK, 0);
    ds >> object;
}

void mff_aj::rpc_get_block(const uint256& blockhex, tiny::block& b, uint32_t& height) {
    // printf("get block %s\n", blockhex.ToString().c_str());
    std::string dstfinal = "blockdata/" + blockhex.ToString() + ".mffb";
    FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp) {
        std::string dsthex = "blockdata/" + blockhex.ToString() + ".hex";
        std::string dsthdr = "blockdata/" + blockhex.ToString() + ".hdr";
        FILE* fphex = fopen(dsthex.c_str(), "r");
        FILE* fphdr = fopen(dsthdr.c_str(), "r");
        if (!fphex) {
            std::string cmd = aj_rpc_call + " getblock " + blockhex.ToString() + " 0 > " + dsthex;
            fphex = rpc_fetch(cmd.c_str(), dsthex.c_str());
        }
        if (!fphdr) {
            std::string cmd = aj_rpc_call + " getblockheader " + blockhex.ToString() + " > " + dsthdr;
            fphdr = rpc_fetch(cmd.c_str(), dsthdr.c_str());
        }
        fclose(fphdr);                                      // closes fphdr
        std::string dstheight = std::string("blockdata/") + blockhex.ToString() + ".height";
        std::string cmd = std::string("cat ") + dsthdr + " | jq -r .height > " + dstheight;
        system(cmd.c_str());
        fphdr = fopen(dstheight.c_str(), "r");
        assert(1 == fscanf(fphdr, "%u", &height));
        fclose(fphdr);                                      // closes fphdr (.height open)
        fseek(fphex, 0, SEEK_END);
        size_t sz = ftell(fphex);
        fseek(fphex, 0, SEEK_SET);
        char* blk = (char*)malloc(sz + 1);
        assert(blk);
        fread(blk, 1, sz, fphex);
        fclose(fphex);                                      // closes fphex
        blk[sz] = 0;
        std::vector<uint8_t> blkdata = ParseHex(blk);
        free(blk);
        fp = fopen(dstfinal.c_str(), "wb+");
        // write height
        fwrite(&height, sizeof(uint32_t), 1, fp);
        // write block
        fwrite(blkdata.data(), 1, blkdata.size(), fp);
        fseek(fp, 0, SEEK_SET);
        // unlink
        unlink(dsthex.c_str());
        unlink(dsthdr.c_str());
        unlink(dstheight.c_str());
    }
    // read height
    fread(&height, sizeof(uint32_t), 1, fp);
    // deserialize block
    CAutoFile deserializer(fp, SER_DISK, 0);
    deserializer >> b;
    // deserializer closes fp
}

void mff_aj::rpc_get_tx(const uint256& txhex, tiny::tx& tx, size_t retries) {
    printf("get tx %s\n", txhex.ToString().c_str());
    std::string dstfinal = "txdata/" + txhex.ToString() + ".mfft";
    FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp) {
        std::string dsthex = "txdata/" + txhex.ToString() + ".hex";
        std::string cmd = aj_rpc_call + " getrawtransaction " + txhex.ToString() + " > " + dsthex;
        FILE* fphex = fopen(dsthex.c_str(), "r");
        if (!fphex) {
            fphex = rpc_fetch(cmd.c_str(), dsthex.c_str());
        }
        fseek(fphex, 0, SEEK_END);
        size_t sz = ftell(fphex);
        fseek(fphex, 0, SEEK_SET);
        if (sz == 0) {
            if (retries == 5) {
                fprintf(stderr, "failed to fetch tx %s after 5 tries, aborting\n", txhex.ToString().c_str());
                assert(0);
            }
            fprintf(stderr, "failed to fetch tx %s... waiting 5 seconds and trying again...\n", txhex.ToString().c_str());
            unlink(dsthex.c_str());
            sleep(5);
            return rpc_get_tx(txhex, tx, retries + 1);
        }
        char* txhex = (char*)malloc(sz + 1);
        assert(txhex);
        fread(txhex, 1, sz, fphex);
        fclose(fphex);                                      // closes fphex
        txhex[sz] = 0;
        std::vector<uint8_t> txdata = ParseHex(txhex);
        free(txhex);
        fp = fopen(dstfinal.c_str(), "wb+");
        // write tx
        fwrite(txdata.data(), 1, txdata.size(), fp);
        fseek(fp, 0, SEEK_SET);
        // unlink
        unlink(dsthex.c_str());
    }
    // deserialize tx
    CAutoFile deserializer(fp, SER_DISK, 0);
    deserializer >> tx;
    // deserializer closes fp
}

tiny::amount mff_aj::rpc_get_tx_input_amount(tiny::tx& tx) {
    tiny::amount amount = 0;
    tiny::tx tt;
    for (auto& input : tx.vin) {
        rpc_get_tx(input.prevout.hash, tt);
        amount += tt.vout[input.prevout.n].value;
    }
    return amount;
}

///////// AMAP

int64_t mff_aj::amap_get_output_value(const uint256& txid, int n)
{
    return amap::output_amount(txid, n);
}

///////// RPC/AMAP

int64_t mff_aj::get_output_value(const uint256& txid, int n)
{
    static uint64_t known_count = 0, amap_count = 0, rpc_count = 0, total_count = 0;
    total_count++;
    if ((total_count % 100000) == 0) {
        printf("[ known=%llu amap=%llu rpc=%llu (total=%llu) ]                  \n", known_count, amap_count, rpc_count, total_count);
    }
    // Known?
    if (seqs.count(txid)) {
        known_count++;
        assert(txs[seqs[txid]]->amounts.size() > n);
        return txs[seqs[txid]]->amounts[n];
    }
    // AMAP is faster, try that first
    if (amap::enabled) {
        int64_t amount = amap::output_amount(txid, n);
        if (amount != -1) {
            amap_count++;
            // printf("%s::%d = %lld\n", txid.ToString().c_str(), n, amount);
            return amount; // got it!
        }
    }
    rpc_count++;
    printf("fallback to RPC for %s\n", txid.ToString().c_str());
    tiny::tx t;
    rpc_get_tx(txid, t);
    return t.vout[n].value;
}

int64_t mff_aj::get_tx_input_amount(tiny::tx& tx)
{
    if (!amap::enabled) return get_tx_input_amount(tx);

    int64_t amount = 0;
    for (auto& input : tx.vin) {
        amount += get_output_value(input.prevout.hash, input.prevout.n);
    }
    return amount;
}

/////////

void mff_aj::tx_invalid(bool known, seq_t seq, std::shared_ptr<tx> t, const tiny::tx& tref, uint8_t state, const uint256* cause) {
    mplinfo("TX_INVALID %s %llu %s [%s]\n", known ? "known" : "new", seq, t->id.ToString().c_str(), tx_invalid_state_str(state));
    auto e = std::make_shared<entry>(this);
    e->cmd = CMD::TX_INVALID;
    e->known = known;
    if (known) {
        t->location = tx::location_invalid;
        t->invalid_reason = (tx::invalid_reason_enum)state;
        // tx_freeze(seq);
    }
    e->invalid_reason = (tx::invalid_reason_enum)state;
    e->invalid_cause_known = cause != nullptr;
    if (cause) e->invalid_replacement = *cause;
    e->invalid_tinytx = &tref;
    push_entry(e);
    // last_entry.invalid_txhex = 
}

bool debug_deps = false;
#define DEBUG_DEPS(args...) if (debug_deps) printf(args)

void mff_aj::check_deps(tiny::tx* cause, const uint256& txid, const uint32_t* index_or_all) {
    // if (seqs.count(txid)) {
    //     seq_t seq = seqs[txid];
    //     DEBUG_DEPS("- %llu deps\n", seq);
    //     if (deps.count(seq)) {
    //         // potential double spend or RBF
    //         for (const auto& other : deps[seq]) {
    //             DEBUG_DEPS("--- %s with %llu inputs\n", other->id.ToString().c_str(), other->inputs);
    //             for (uint64_t j = 0; j < other->inputs; ++j) {
    //                 const auto& prevout2 = other->vin[j];
    //                 const uint256& prevout2hash = prevout2.is_known() ? txs[prevout2.get_seq()]->id : prevout2.get_txid();
    //                 DEBUG_DEPS("--- comparing %s with %s && (!%d || %llu == %u)\n", prevout2hash.ToString().c_str(), txid.ToString().c_str(), !!index_or_all, prevout2.n, index_or_all ? *index_or_all : 0);
    //                 if (prevout2hash == txid && (!index_or_all || prevout2.n == *index_or_all)) {
    //                     // double spent or RBF bumped; we are going with
    //                     // double spent for now
    //                     // TODO: detect RBF
    //                     auto otx = std::make_shared<tiny::tx>();
    //                     rpc_get_tx(other->id, *otx);
    //                     backlog_ttxs.push_back(otx);
    //                     tx_invalid(true /* ? */, seq, other, *otx, tx::invalid_doublespent, cause ? &cause->hash : nullptr);
    //                     // recursively invalidate txs dependent on other (no matter the index)
    //                     check_deps(cause, other->id);
    //                     break;
    //                 }
    //             }
    //         }
    //     }
    // }
}

void mff_aj::check_deps(tiny::tx& newcomer) {
    // debug_deps = false;
    // if (newcomer.hash == uint256S("535a1b4a6eb47a5d22e19c439415bba6b9ae8f200c4806d30886a7d9649ffca2") || newcomer.hash == uint256S("964797a5f19debe525759e126a40baec325d4f17bf3422d580d1b978d25aa096")) {
    //     debug_deps = true;
    // }
    // DEBUG_DEPS("check_deps(%s) w/ %zu inputs\n", newcomer.hash.ToString().c_str(), newcomer.vin.size());
    // for (uint64_t i = 0; i < newcomer.vin.size(); ++i) {
    //     const auto& prevout = newcomer.vin[i].prevout;
    //     DEBUG_DEPS("- #%llu %s:%u (%s)\n", i, prevout.hash.ToString().c_str(), prevout.n, seqs.count(prevout.hash) ? "known" : "unknown");
    //     check_deps(&newcomer, prevout.hash, &prevout.n);
    //     // add dependency if known
    //     if (seqs.count(prevout.hash)) {
    //         seq_t seq = seqs[prevout.hash];
    //         deps[seq].push_back(txs[seq]);
    //     }
    // }
}

void mff_aj::push_entry(std::shared_ptr<entry> e) {
    e->time = read_time;
    backlog.push_back(e);
}

std::shared_ptr<tx> mff_aj::register_tx(tiny::tx& tt) {
    auto t = std::make_shared<tx>();
    t->id = tt.hash;
    t->seq = nextseq++;
    DSL(t->seq, "register_entry\n");
    // printf("tx-reg: %s <=> %llu\n", t->id.ToString().c_str(), t->seq);

    t->weight = tt.GetWeight();
    if (tt.IsCoinBase()) {
        t->fee = 0;
    } else {
        tiny::amount sum_out = 0;
        for (auto& output : tt.vout) {
            sum_out += output.value;
        }
        int64_t in_amount = get_tx_input_amount(tt);
        if (in_amount < sum_out) {
            fprintf(stderr, "*** in < out: %lld < %lld\n", in_amount, sum_out);
        }
        assert(in_amount >= sum_out);
        t->fee = in_amount - sum_out; // rpc_get_tx_input_amount(tt) - sum_out;
        // tiny::tx tx2;
        // rpc_get_tx(tt.hash, tx2);
    }
    t->inputs = tt.vin.size();
    t->state.resize(t->inputs);
    t->vin.resize(t->inputs);
    for (uint64_t i = 0; i < t->inputs; ++i) {
        const auto& prevout = tt.vin[i].prevout;
        bool known = seqs.count(prevout.hash);
        seq_t seq = known ? seqs[prevout.hash] : 0;
        assert(!known || seq != 0);
        if (tt.IsCoinBase()) {
            t->state[i] = outpoint::state_coinbase;
        } else {
            bool confirmed = known && txs[seq]->location == tx::location_confirmed;
            t->state[i] = confirmed ? outpoint::state_confirmed : known ? outpoint::state_known : outpoint::state_unknown;
        }
        t->vin[i] = known ? outpoint(prevout.n, seq) : outpoint(prevout.n, prevout.hash);
        // printf("- vin %llu = %s n=%d, seq=%llu, hash=%s :: %s\n", i, known ? "known" : "unknown", prevout.n, seq, prevout.hash.ToString().c_str(), t->vin[i].to_string().c_str());
    }
    t->amounts.clear();
    for (const tiny::txout& o : tt.vout)
        t->amounts.push_back(o.value);

    txs[t->seq] = t;
    seqs[t->id] = t->seq;
    return t;
}

void mff_aj::insert(tiny::tx& tt) {
    mempool.insert_tx(std::make_shared<tiny::tx>(tt));
    // // mplinfo("insert %s %s\n", seqs.count(tref.GetHash()) ? "known" : "new", tref.GetHash().ToString().c_str());
    // auto e = std::make_shared<entry>(this);
    // if (seqs.count(tt.hash)) {
    //     // we do: TX_IN
    //     DSL(seqs[tt.hash], "insert() TX_IN\n");
    //     e->cmd = CMD::TX_IN;
    //     e->known = true;
    //     e->tx = txs[seqs[tt.hash]];
    // } else {
    //     // we don't: TX_REC
    //     auto t = register_tx(tt);
    //     e->cmd = CMD::TX_REC;
    //     e->known = false;
    //     e->tx = t;
    // }
    // push_entry(e);
    // check_deps(tt);
}

void mff_aj::confirm(uint32_t height, const uint256& hash, tiny::block& b) {
    std::shared_ptr<block> blk = std::make_shared<block>();
    blk->height = height;
    while (active_chain.chain.size() > 0 && height < active_chain.height + 1) {
        mplinfo("unconfirming block #%u\n", active_chain.height);
        mempool.reorg_block(active_chain.height);
        // auto e = std::make_shared<entry>(this);
        // e->cmd = CMD::TX_UNCONF;
        // e->known = true;
        // e->unconf_height = active_chain.height;
        // push_entry(e);
        undo_block_at_height(active_chain.height);
    }
    assert(active_chain.chain.size() == 0 || height == active_chain.height + 1);
    mempool.process_block(height, hash, b.vtx);
    std::vector<uint256>& pending_conf_unknown = blk->unknown;
    std::vector<seq_t>& pending_conf_known = blk->known;
    for (auto& t : b.vtx) {
        bool known = seqs.count(t.hash);
        if (known) {
            seq_t seq = seqs[t.hash];
            auto& rt = txs[seq];
            // if (rt->location == tx::location_discarded || rt->location == tx::location_invalid) {
            //     // re-check dependencies, as this was actually thrown out
            //     check_deps(t);
            // }
            pending_conf_known.push_back(seq);
            rt->location = tx::location_confirmed;
            // tx_freeze(seq);
        } else {
            // check deps
            // check_deps(t);
            pending_conf_unknown.push_back(t.hash);
        }
    }
    // auto e = std::make_shared<entry>(this);
    // e->cmd = CMD::TX_CONF;
    blk->is_known = 
    //e->known =
    blocks.count(hash);
    blk->count_known = pending_conf_known.size();
    //e->block_in = 
    blocks[hash] = blk;
    // push_entry(e);
    active_chain.height = height;
    active_chain.chain.push_back(blk);
}

entry* mff_aj::read_entry() {
    // if (debug_seq) {
    //     if (seqs.count(debug_txid) == 0 || seqs[debug_txid] != debug_seq) {
    //         fprintf(stderr, "*** lost connection to %s (seqs[id] -> %llu)\n", debug_txid.ToString().c_str(), seqs.count(debug_txid) ? seqs[debug_txid] : 0);
    //         assert(0);
    //     }
    // }
    // if (known_tx_list == nullptr) {
    //     known_tx_list = fopen("known_tx_list.txt", "w");
    // }
    // if (backlog.size()) {
    //     entry_counter++;
    //     previous_entry = backlog[0];
    //     backlog.erase(backlog.begin());
    //     last_time = previous_entry->time;
    //     // if (debug_seq) {
    //     //     if (seqs.count(debug_txid) == 0 || seqs[debug_txid] != debug_seq) {
    //     //         fprintf(stderr, "*** lost connection to %s (seqs[id] -> %llu)\n", debug_txid.ToString().c_str(), seqs.count(debug_txid) ? seqs[debug_txid] : 0);
    //     //         assert(0);
    //     //     }
    //     // }
    //     return previous_entry.get();
    // } else {
    //     // clean up backlog stuffs
    //     // TODO: ^^^
    // }
    entry* e = (entry*)1;

    // <timestamp> <action> <data...>
    int64_t timestamp;
    uint32_t fraction;
    char action[64];
    if (fscanf(in_fp, "%lld.%u %s ", &timestamp, &fraction, action) != 3) {
        return nullptr;
    }
    // bool old_d = debugging;
    // debugging = timestamp == debug_time;
    // if (!old_d && debugging) {
    //     printf("debugging now!\n");
    // }
    // DEBLOG("%lld.%u %s ", timestamp, fraction, action);

    if (shared_time) *shared_time = timestamp;
    read_time = timestamp;

    char* bptr = buffer;
    size_t rem = buffer_cap;
    for (;;) {
        if (!fgets(bptr, rem, in_fp)) {
            if (ferror(in_fp)) {
                fprintf(stderr, "\nerror reading from input file\n");
                return nullptr;
            }
            break;
        }
        DEBLOG("%s", bptr);
        size_t rbytes = strlen(bptr);
        if (rbytes == 0) {
            fprintf(stderr, "\nunable to read from input file\n");
            return nullptr;
        }
        bptr += rbytes;
        if (bptr[-1] == '\n') {
            // end of line
            break;
        }
        rem -= rbytes;
        if (rem < 2) {
            rbytes = bptr - buffer;
            buffer_cap <<= 1;
            buffer = (char*)realloc(buffer, buffer_cap);
            bptr = buffer + rbytes;
            rem = buffer_cap - rbytes;
        }
    }

    // remove newline
    if (bptr > buffer && bptr[-1] == '\n') bptr[-1] = 0;

    // // if buffer is 64 bytes, we check it against debug txid
    // if (strlen(buffer) == 64) {
    //     uint256 x = uint256S(buffer);
    //     if (x == debug_txid) {
    //         printf("*** DEBUG TXID IN %lld.%u %s\n", timestamp, fraction, action);
    //     }
    // }

    // hashtx <txid>
    // rawtx <tx hex>
    // hashblock <block id>

    if (!strcmp(action, "hashtx")) {
        // a transaction entered the mempool; we can't do anything
        // with it yet, though, as we don't know inputs and stuff
        // so we loop back and fetch the next entry
        uint256 old = hashtx;
        hashtx = uint256S(buffer);
        // if (read_hashtx) {
        //     fprintf(stderr, "\nwarning: read_hashtx is true @ hashtx (%s -> %s)\n", old.ToString().c_str(), hashtx.ToString().c_str());
        // }
        read_hashtx = true;
        // fprintf(known_tx_list, "%s (hash only)\n", hashtx.ToString().c_str());
        // fflush(known_tx_list);
        // if (debug_seq) {
        //     if (seqs.count(debug_txid) == 0 || seqs[debug_txid] != debug_seq) {
        //         fprintf(stderr, "*** lost connection to %s (seqs[id] -> %llu)\n", debug_txid.ToString().c_str(), seqs.count(debug_txid) ? seqs[debug_txid] : 0);
        //         assert(0);
        //     }
        // }
        return e;// read_entry();
    }

    if (!strcmp(action, "rawtx")) {
        // if (!read_hashtx) {
        //     fprintf(stderr, "\nwarning: read_hashtx is false @ rawtx\n");
        // }
        read_hashtx = false;
        tiny::tx tx;
        deserialize_hex_string(buffer, tx);
        // fprintf(known_tx_list, "%s (full)\n", tx.hash.ToString().c_str());
        // fflush(known_tx_list);
        // if (tx.hash != hashtx) {
        //     fprintf(stderr, "\nwarning: tx.hash != hashtx:\n\t%s\n!=\t%s\n", tx.hash.ToString().c_str(), hashtx.ToString().c_str());
        //     // assert(0);
        // }
        insert(tx);
        // DEBLOG("deserialized tx %s with seq=%llu\n", tx.hash.ToString().c_str(), seqs[tx.hash]);
        // if (debugging && tx.hash == debug_txid) debug_seq = seqs[tx.hash];
        // if (debug_seq) {
        //     if (seqs.count(debug_txid) == 0 || seqs[debug_txid] != debug_seq) {
        //         fprintf(stderr, "*** lost connection to %s (seqs[id] -> %llu)\n", debug_txid.ToString().c_str(), seqs.count(debug_txid) ? seqs[debug_txid] : 0);
        //         assert(0);
        //     }
        // }
        return e;// read_entry();
    }

    // if (read_hashtx) {
    //     fprintf(stderr, "\nwarning: read_hashtx is true @ %s\n", action);
    // }

    if (!strcmp(action, "hashblock")) {
        uint256 blockhash = uint256S(buffer);
        tiny::block blk;
        uint32_t height;
        rpc_get_block(blockhash, blk, height);
        confirm(height, blockhash, blk);
        // if (debug_seq) {
        //     if (seqs.count(debug_txid) == 0 || seqs[debug_txid] != debug_seq) {
        //         fprintf(stderr, "*** lost connection to %s (seqs[id] -> %llu)\n", debug_txid.ToString().c_str(), seqs.count(debug_txid) ? seqs[debug_txid] : 0);
        //         assert(0);
        //     }
        // }
        return e;//read_entry();
    }

    fprintf(stderr, "\nunknown command %s\n", action);
    assert(0);
}

// void mff_aj::write_entry(entry* e) {
//     uint8_t u8;
//     CMD cmd = e->cmd;
//     bool known, timerel;
//     known = e->known;
//     u8 = prot(cmd, known);
//     in << u8;
// 
//     switch (cmd) {
//         case CMD::TIME_SET:
//             // time updated after switch()
//             mplinfo("TIME_SET()\n");
//             break;
// 
//         case TX_REC: {
//             mplinfo("TX_REC(): "); fflush(stdout);
//             auto t = import_tx(e->sds, e->tx);
//             DSL(t->seq, "TX_REC\n");
//             if (txs.count(t->seq)) {
//                 seqs.erase(txs[t->seq]->id); // this unlinks the txid-seq rel
//             }
//             txs[t->seq] = t;
//             seqs[t->id] = t->seq;
//             mplinfo_("id=%s, seq=%llu\n", t->id.ToString().c_str(), t->seq);
//             t->location = tx::location_in_mempool;
//             serializer.serialize_tx(in, *t);
//             break;
//         }
// 
//         case TX_IN: {
//             mplinfo("TX_IN(): "); fflush(stdout);
//             auto t = import_tx(e->sds, e->tx);
//             uint64_t seq = seqs[t->id];
//             assert(seq && txs.count(seq));
//             DSL(seq, "TX_IN\n");
//             if (txs.count(seq)) {
//                 txs[seq]->location = tx::location_in_mempool;
//             }
//             seq_write(seq);
//             mplinfo_("seq=%llu\n", seq);
//             break;
//         }
// 
//         case TX_CONF: {
//             // l1("TX_CONF(%s): ", known ? "known" : "unknown");
//             auto b = e->block_in;
//             if (known) {
//                 // we know the block; just get the header info and find it, then apply
//                 b->is_known = true;
//                 // conversion not needed
//                 serializer.serialize_block(in, *b);
//                 assert(blocks.count(b->hash));
//                 // l1("block %u=%s\n", b.height, b.hash.ToString().c_str());
//                 apply_block(blocks[b->hash]);
//             } else {
//                 b->is_known = false;
//                 b = import_block(e->sds, b);
//                 serializer.serialize_block(in, *b);
//                 blocks[b->hash] = b;
//                 // l1("block %u=%s\n", b->height, b->hash.ToString().c_str());
//                 apply_block(b);
//             }
//             break;
//         }
// 
//         case TX_OUT: {
//             assert(known);
//             mplinfo("TX_OUT(): "); fflush(stdout);
//             seq_t seq = seqs[e->tx->id];
//             uint8_t reason = e->out_reason;
//             DSL(seq, "TX_OUT\n");
//             assert(txs.count(seq));
//             auto t = txs[seq];
//             t->location = tx::location_discarded;
//             t->out_reason = (tx::out_reason_enum)reason;
//             seq_write(seq);
//             in << reason;
//             mplinfo_("seq=%llu, reason=%s\n", seq, tx_out_reason_str(reason));
//             break;
//         }
// 
//         case TX_INVALID: {
//             mplinfo("TX_INVALID(): "); fflush(stdout);
//             // out.debugme(true);
//             replacement_seq = 0;
//             replacement_txid.SetNull();
// 
//             auto t_invalid = import_tx(e->sds, e->tx);
//             // printf("t_invalid = %s\n", t_invalid->id.ToString().c_str());
//             seq_t tx_invalid = seqs[t_invalid->id];
//             assert(!tx_invalid || txs[seqs[t_invalid->id]]->id == t_invalid->id);
//             uint8_t state = e->invalid_reason;
//             bool cause_known = e->invalid_cause_known;
//             seq_t tx_cause(0);
//             const uint256& invalid_replacement = e->invalid_replacement;
//             const tiny::tx* invalid_tinytx = e->invalid_tinytx;
// 
//             write_txref(tx_invalid, t_invalid->id);
// 
//             uint8_t v = state | (cause_known ? CMD::TX_KNOWN_BIT : 0);
//             in << v;
//             if (state != tx::invalid_unknown && state != tx::invalid_reorg) {
//                 assert(!invalid_replacement.IsNull());
//                 seq_t causeseq = seqs.count(invalid_replacement) ? seqs[invalid_replacement] : 0;
//                 write_txref(causeseq, invalid_replacement);
//             }
// 
//             in << *invalid_tinytx;
//             // printf("invalid tinytx = %s\n", invalid_tinytx->hash.ToString().c_str());
// 
//             if (tx_invalid) {
//                 auto t = txs[tx_invalid];
//                 t->location = tx::location_invalid;
//                 t->invalid_reason = (tx::invalid_reason_enum)state;
//                 if (txs.count(tx_invalid)) {
//                     if (invalid_tinytx->hash != txs[tx_invalid]->id) {
//                         printf("tx hash failure:\n\t%s\nvs\t%s\n", invalid_tinytx->hash.ToString().c_str(), txs[tx_invalid]->id.ToString().c_str());
//                         printf("tx hex = %s\n", HexStr(*e->invalid_txhex).c_str());
//                     }
//                     assert(invalid_tinytx->hash == txs[tx_invalid]->id);
//                 }
//             }
//             mplinfo_("seq=%llu, state=%s, cause=%llu, tx_data=%s\n", tx_invalid, tx_invalid_state_str(state), tx_cause, last_invalidated_tx.ToString().c_str());
//             mplinfo_("----- tx invalid deserialization ends -----\n");
//             // out.debugme(false);
//             break;
//         }
// 
//         case TX_UNCONF: {
//             mplinfo("TX_UNCONF(): "); fflush(stdout);
//             uint32_t height = e->unconf_height;
//             in << height;
//             if (known) {
//                 mplinfo_("known, height=%u\n", height);
//                 undo_block_at_height(height);
//             } else {
//                 assert(!"not implemented");
//             }
//             break;
//         }
// 
//         default:
//             mplerr("u8 = %u, cmd = %u\n", u8, cmd);
//             assert(!"unknown command"); // todo: exceptionize
//     }
//     write_set_time(u8, e->time);
// }

inline void mff_aj::sync() {
    int64_t now = GetTime();
    if (lastflush + 10 < now) {
        // printf("*\b"); fflush(stdout);
        fflush(in_fp);
        lastflush = now;
    }
}
} // namespace mff
