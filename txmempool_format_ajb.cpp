// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include <tinyblock.h>
#include <unistd.h>
#include <amap.h>
#include <tinyfs.h>

#include <txmempool_format_ajb.h>

// static mff::seq_t debug_seq = 0;
// static bool debugging = false;
// static int64_t debug_time = 1516996219;
#define DEBLOG(args...) //if (debugging) printf(args)
// static uint256 debug_txid = uint256S("59aa5ee3db978ea8168a6973b505c31b3f5f4757330da4ef45da0f51a81c1fc9");
// #define DEBUG_SEQ 63916
#define DSL(s, fmt...) //if (s == DEBUG_SEQ) { printf("[SEQ] " fmt); }
// #define l(args...) if (active_chain.height == 521703) { printf(args); }
// #define l1(args...) if (active_chain.height == 521702) { printf(args); }

namespace mff {

std::string ajb_rpc_call = "";

// bool showinfo = false;
#define mplinfo_(args...) // if (showinfo) { printf(args); }
#define mplinfo(args...) // if (showinfo) { printf("[MPL::info] " args); }
#define mplwarn(args...) printf("[MPL::warn] " args)
#define mplerr(args...)  fprintf(stderr, "[MPL::err]  " args)

inline FILE* setup_file(const char* path, bool readonly) {
    FILE* fp = fopen(path, readonly ? "rb" : "rb+");
    if (!readonly && fp == nullptr) {
        fp = fopen(path, "wb");
    }
    if (fp == nullptr) {
        fprintf(stderr, "unable to open %s\n", path);
        assert(fp);
    }
    return fp;
}

mff_ajb::mff_ajb(const std::string path, bool readonly) : in_fp(setup_file(path.length() > 0 ? path.c_str() : (std::string(std::getenv("HOME")) + "/mff.out").c_str(), readonly)), in(in_fp, SER_DISK, 0) {
    nextseq = 1;
    last_seq = 0;
    lastflush = GetTime();
    buffer = (char*)malloc(1024);
    buffer_cap = 1024;
    entry_counter = 0;
    read_time = 0;
    // out.debugme(true);
    mplinfo("start %s\n", path.c_str());
}

mff_ajb::~mff_ajb() {
}

void mff_ajb::apply_block(std::shared_ptr<block> b) {
    if (active_chain.chain.size() > 0 && b->hash == active_chain.chain.back()->hash) {
        // we are already on this block
        return;
    }
    // l1("apply block %u (%s)\n", b->height, b->hash.ToString().c_str());
    if (active_chain.chain.size() > 0 && b->height < active_chain.height + 1) {
        mplwarn("dealing with BLOCK_UNCONF missing bug 20180502153142\n");
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

void mff_ajb::undo_block_at_height(uint32_t height) {
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

seq_t mff_ajb::touched_txid(const uint256& txid, bool count) {
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
        if (last_cmd == BLOCK_CONF) {
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
    if (last_cmd == BLOCK_CONF) {
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

uint256 mff_ajb::get_replacement_txid() const {
    return replacement_seq && txs.count(replacement_seq) ? txs.at(replacement_seq)->id : replacement_txid;
}

uint256 mff_ajb::get_invalidated_txid() const {
    return invalidated_seq && txs.count(invalidated_seq) ? txs.at(invalidated_seq)->id : invalidated_txid;
}

/////// RPC

inline tiny::File rpc_fetch(const char* cmd, const std::string& dst, bool abort_on_failure = false) {
    if (ajb_rpc_call == "") {
        assert(!"no RPC call available");
    }
    system(cmd);
    tiny::File fp = tiny::OpenFile(dst, "r");
    if (!fp->has_data()) {
        fp->close();
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

bool mff_ajb::rpc_get_block(uint32_t height, tiny::block& b, uint256& blockhex) {
    std::string dstfinal = "blockdata/" + std::to_string(height) + ".hth";
    tiny::File fp = tiny::OpenFile(dstfinal, "rb");
    // FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp->has_data()) {
        std::string dsttxt = "blockdata/" + std::to_string(height) + ".hth.txt";
        tiny::File fptxt = tiny::OpenFile(dsttxt, "r");
        if (!fptxt->has_data()) {
            std::string cmd = ajb_rpc_call + " getblockhash " + std::to_string(height) + " > " + dsttxt;
            fptxt = rpc_fetch(cmd.c_str(), dsttxt);
        }
        char hex[128];
        fscanf(fptxt->m_fp, "%s", hex);
        assert(strlen(hex) == 64);
        blockhex = uint256S(hex);
        fptxt->close();
        fp = tiny::OpenFile(dstfinal, "wb");
        fp->autofile() << blockhex;
        return rpc_get_block(blockhex, b, height);
    }
    fp->autofile() >> blockhex;
    return rpc_get_block(blockhex, b, height);
}

bool mff_ajb::rpc_get_block(const uint256& blockhex, tiny::block& b, uint32_t& height) {
    // printf("get block %s\n", blockhex.ToString().c_str());
    std::string dstfinal = "blockdata/" + blockhex.ToString() + ".mffb";
    tiny::File fp = tiny::OpenFile(dstfinal, "rb");
    if (!fp->has_data()) {
        std::string dsthex = "blockdata/" + blockhex.ToString() + ".hex";
        std::string dsthdr = "blockdata/" + blockhex.ToString() + ".hdr";
        tiny::File fphex = tiny::OpenFile(dsthex, "r");
        tiny::File fphdr = tiny::OpenFile(dsthdr, "r");
        if (!fphex->has_data()) {
            std::string cmd = ajb_rpc_call + " getblock " + blockhex.ToString() + " 0 > " + dsthex;
            fphex = rpc_fetch(cmd.c_str(), dsthex);
        }
        if (!fphdr->has_data()) {
            std::string cmd = ajb_rpc_call + " getblockheader " + blockhex.ToString() + " > " + dsthdr;
            fphdr = rpc_fetch(cmd.c_str(), dsthdr);
        }
        fphdr->close();
        // fclose(fphdr);                                      // closes fphdr
        std::string dstheight = std::string("blockdata/") + blockhex.ToString() + ".height";
        std::string cmd = std::string("cat ") + dsthdr + " | jq -r .height > " + dstheight;
        system(cmd.c_str());
        fphdr = tiny::OpenFile(dstheight, "r");
        if (fscanf(fphdr->m_fp, "%u", &height) != 1) {
            // block is probably an orphan block
            printf("failure to load orphan block %u=%s\n", height, blockhex.ToString().c_str());
            // fprintf(stderr, "UNDETECTED FAILURE: BLOCK=%s\n", blockhex.ToString().c_str());
            return false;
        }
        fphdr->close();
        // fclose(fphdr);                                      // closes fphdr (.height open)
        fseek(fphex->m_fp, 0, SEEK_END);
        size_t sz = ftell(fphex->m_fp);
        fseek(fphex->m_fp, 0, SEEK_SET);
        char* blk = (char*)malloc(sz + 1);
        assert(blk);
        fread(blk, 1, sz, fphex->m_fp);
        fphex->close();
        // fclose(fphex);                                      // closes fphex
        blk[sz] = 0;
        std::vector<uint8_t> blkdata = ParseHex(blk);
        free(blk);
        fp = tiny::OpenFile(dstfinal, "wb+");
        // write height
        fp->write(height);
        // fwrite(&height, sizeof(uint32_t), 1, fp);
        // write block
        fp->write(blkdata);
        fseek(fp->m_fp, 0, SEEK_SET);
        // unlink
        unlink(dsthex.c_str());
        unlink(dsthdr.c_str());
        unlink(dstheight.c_str());
    }
    // read height
    fp->read(height);
    // fread(&height, sizeof(uint32_t), 1, fp);
    // deserialize block
    fp->autofile() >> b;
    // deserializer closes fp
    return true;
}

void mff_ajb::rpc_get_tx(const uint256& txhex, tiny::tx& tx, size_t retries) {
    printf("get tx %s\n", txhex.ToString().c_str());
    std::string dstfinal = "txdata/" + txhex.ToString() + ".mfft";
    tiny::File fp = tiny::OpenFile(dstfinal, "rb");
    if (!fp->has_data()) {
        std::string dsthex = "txdata/" + txhex.ToString() + ".hex";
        std::string cmd = ajb_rpc_call + " getrawtransaction " + txhex.ToString() + " > " + dsthex;
        tiny::File fphex = tiny::OpenFile(dsthex, "r");
        if (!fphex->has_data()) {
            fphex = rpc_fetch(cmd.c_str(), dsthex.c_str());
        }
        fseek(fphex->m_fp, 0, SEEK_END);
        size_t sz = ftell(fphex->m_fp);
        fseek(fphex->m_fp, 0, SEEK_SET);
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
        fread(txhex, 1, sz, fphex->m_fp);
        fphex->close();                                      // closes fphex
        txhex[sz] = 0;
        std::vector<uint8_t> txdata = ParseHex(txhex);
        free(txhex);
        fp = tiny::OpenFile(dstfinal, "wb+");
        // write tx
        fp->write(txdata);
        fseek(fp->m_fp, 0, SEEK_SET);
        // unlink
        unlink(dsthex.c_str());
    }
    // deserialize tx
    fp->autofile() >> tx;
    // deserializer closes fp
}

tiny::amount mff_ajb::rpc_get_tx_input_amount(tiny::tx& tx) {
    tiny::amount amount = 0;
    tiny::tx tt;
    for (auto& input : tx.vin) {
        rpc_get_tx(input.prevout.hash, tt);
        amount += tt.vout[input.prevout.n].value;
    }
    return amount;
}

///////// AMAP

int64_t mff_ajb::amap_get_output_value(const uint256& txid, int n)
{
    return amap::output_amount(txid, n);
}

///////// RPC/AMAP

int64_t mff_ajb::get_output_value(const uint256& txid, int n)
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

int64_t mff_ajb::get_tx_input_amount(tiny::tx& tx)
{
    if (!amap::enabled) return get_tx_input_amount(tx);

    int64_t amount = 0;
    for (auto& input : tx.vin) {
        amount += get_output_value(input.prevout.hash, input.prevout.n);
    }
    return amount;
}

std::shared_ptr<tx> mff_ajb::register_tx(tiny::tx& tt) {
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

void mff_ajb::insert(tiny::tx& tt) {
    mempool->insert_tx(std::make_shared<tiny::tx>(tt));
}

void mff_ajb::confirm(uint32_t height, const uint256& hash, tiny::block& b) {
    std::shared_ptr<block> blk = std::make_shared<block>();
    blk->height = height;
    while (active_chain.chain.size() > 0 && height < active_chain.height + 1) {
        mplinfo("unconfirming block #%u\n", active_chain.height);
        mempool->reorg_block(active_chain.height);
        undo_block_at_height(active_chain.height);
    }
    if (!(active_chain.chain.size() == 0 || height == active_chain.height + 1)) {
        fprintf(stderr, "*** active chain height = %u, confirming height = %u, expecting %u\n", active_chain.height, height, active_chain.height + 1);
    }
    assert(active_chain.chain.size() == 0 || height == active_chain.height + 1);
    mempool->process_block(height, hash, b.vtx);
    std::vector<uint256>& pending_conf_unknown = blk->unknown;
    std::vector<seq_t>& pending_conf_known = blk->known;
    for (auto& t : b.vtx) {
        bool known = seqs.count(t.hash);
        if (known) {
            seq_t seq = seqs[t.hash];
            auto& rt = txs[seq];
            pending_conf_known.push_back(seq);
            rt->location = tx::location_confirmed;
        } else {
            pending_conf_unknown.push_back(t.hash);
        }
    }
    blk->is_known = blocks.count(hash);
    blk->count_known = pending_conf_known.size();
    blocks[hash] = blk;
    active_chain.height = height;
    active_chain.chain.push_back(blk);
}

int64_t mff_ajb::peek_time() {
    long csr = ftell(in_fp);
    uint64_t diff;
    in >> VARINT(diff);
    int64_t timestamp = read_time + diff;
    fseek(in_fp, csr, SEEK_SET);
    return timestamp;
}

bool mff_ajb::read_entry() {
    uint8_t pid;
    try {
        uint64_t diff;
        in >> VARINT(diff) >> pid;
        read_time += diff;
        if (shared_time) *shared_time = read_time;
    } catch (std::ios_base::failure& f) {
        return false;
    }
    switch (pid) {
    case 0x01: // tx
        {
            tiny::tx tx;
            in >> tx;
            // printf("- read tx %s\n", tx.ToString().c_str());
            if (!tx.IsCoinBase()) {
                insert(tx);
            }
        }
        return true;
    case 0x02: // block hex
        {
            uint256 blockhash;
            in >> blockhash;
            // printf("- read blk %s\n", blockhash.ToString().c_str());
            tiny::block blk;
            uint32_t height;
            if (rpc_get_block(blockhash, blk, height)) {
                // fill in gaps in case this is not the next block
                uint32_t expected_block_height = active_chain.chain.size() == 0 && chain_del != nullptr ? chain_del->expected_block_height() : active_chain.height + 1;
                if (expected_block_height && expected_block_height < height) {
                    // printf("expected block height %u, got block at height %u\n", expected_block_height, height);
                    for (uint32_t i = expected_block_height; i < height; i++) {
                        nlprintf("filling gap (height=%u)\n", i);
                        tiny::block blk2;
                        uint256 blockhash2;
                        rpc_get_block(i, blk2, blockhash2);
                        confirm(i, blockhash2, blk2);
                    }
                }
                confirm(height, blockhash, blk);
            }
        }
        return true;
    default:
        // ???
        fprintf(stderr, "\nunknown command %02x\n", pid);
        assert(0);
    }
}

inline void mff_ajb::sync() {
    int64_t now = GetTime();
    if (lastflush + 10 < now) {
        // printf("*\b"); fflush(stdout);
        fflush(in_fp);
        lastflush = now;
    }
}

} // namespace mff
