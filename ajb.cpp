#include <amap.h>
#include <tinyrpc.h>

#include <ajb.h>

namespace mff {

inline FILE* setup_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr) {
        fprintf(stderr, "unable to open %s\n", path);
        assert(fp);
    }
    return fp;
}

ajb::ajb(std::shared_ptr<bitcoin::mff> mff_in, std::shared_ptr<tiny::mempool> mempool_in, const std::string& path)
:   mff(mff_in)
,   mempool(mempool_in)
,   in_fp(setup_file(path.length() > 0 ? path.c_str() : (std::string(std::getenv("HOME")) + "/mff.out").c_str()))
,   in(in_fp, SER_DISK, 0)
,   current_time(0)
,   buffer((char*)malloc(1024))
,   buffer_cap(1024)
{}

/////// RPC

template<typename T>
inline void deserialize_hex_string(const char* string, T& object) {
    CDataStream ds(ParseHex(string), SER_DISK, 0);
    ds >> object;
}

///////// AMAP

int64_t ajb::amap_get_output_value(const uint256& txid, int n)
{
    return amap::output_amount(txid, n);
}

///////// RPC/AMAP

int64_t ajb::get_output_value(const uint256& txid, int n)
{
    static uint64_t known_count = 0, amap_count = 0, rpc_count = 0, total_count = 0;
    total_count++;
    if ((total_count % 100000) == 0) {
        printf("[ known=%" PRIu64 " amap=%" PRIu64 " rpc=%" PRIu64 " (total=%" PRIu64 ") ]                  \n", known_count, amap_count, rpc_count, total_count);
    }
    // Known?
    if (mff->m_references.count(txid)) {
        ++known_count;
        auto tx = mff->tretch(txid);
        assert(tx->m_vout.size() > n);
        return tx->m_vout[n];
    }
    // AMAP is faster, try that first
    if (amap::enabled) {
        int64_t amount = amap::output_amount(txid, n);
        if (amount != -1) {
            amap_count++;
            // printf("%s::%d = %" PRIi64 "\n", txid.ToString().c_str(), n, amount);
            return amount; // got it!
        }
    }
    rpc_count++;
    printf("fallback to RPC for %s\n", txid.ToString().c_str());
    tiny::tx t;
    rpc->get_tx(txid, t);
    return t.vout[n].value;
}

int64_t ajb::get_tx_input_amount(tiny::tx& tx)
{
    if (!amap::enabled) return get_tx_input_amount(tx);

    int64_t amount = 0;
    for (auto& input : tx.vin) {
        amount += get_output_value(input.prevout.hash, input.prevout.n);
    }
    return amount;
}

bool ajb::process_block_hash(const uint256& blockhash, bool reorging) {
    // printf("- read blk %s\n", blockhash.ToString().c_str());
    tiny::block blk;
    uint32_t height;
    if (rpc->get_block(blockhash, blk, height)) {
        // if this is in the chain, we ignore
        auto b = mff->m_chain.get_block_for_height(height);
        if (!b || b->m_hash != blockhash) {
            // is this actually in the same chain?
            auto p = mff->m_chain.get_block_for_height(height - 1);
            if (p && blk.prev_blk != p->m_hash) {
                // it isn't; we are reorging
                mff->unconfirm_tip(current_time);
                process_block_hash(blk.prev_blk, true);
                p = mff->m_chain.get_block_for_height(height - 1);
                if (p && blk.prev_blk != p->m_hash) {
                    // we were unable to reorg into the new chain; this is a bug
                    fprintf(stderr, "\n*** failure to reorg into new chain (prev %s should have become %s for block %s)\n", p->m_hash.ToString().c_str(), blk.prev_blk.ToString().c_str(), blk.GetHash().ToString().c_str());
                    exit(1);
                }
            }
            // fill in gaps in case this is not the next block
            uint32_t expected_block_height = mff->get_height() == 0 ? height : mff->get_height() + 1;
            if (expected_block_height && expected_block_height < height) {
                printf("expected block height %u, got block at height %u\n", expected_block_height, height);
                for (uint32_t i = expected_block_height; i < height; ++i) {
                    printf("filling gap (height=%u)\n", i);
                    tiny::block blk2;
                    uint256 blockhash2;
                    rpc->get_block(i, blk2, blockhash2);
                    confirm(i, blockhash2, blk2);
                }
            }
            confirm(height, blockhash, blk);
            if (!reorging) {
                // determine the time/hash of the next block, if any
                if (rpc->get_block(height+1, blk, next_block)) {
                    next_block_time = blk.time + 300;
                } else {
                    next_block_time = 0;
                }
            }
        }
    }
    return true;
}

bool ajb::read_entry() {
    if (next_block_time && next_block_time <= current_time) {
        return process_block_hash(next_block);
    }
    uint8_t pid;
    try {
        uint64_t diff;
        in >> VARINT(diff) >> pid;
        current_time += diff;
        // if (shared_time) *shared_time = current_time;
    } catch (std::ios_base::failure& f) {
        return false;
    }
    switch (pid) {
    case 0x01: // tx
        {
            tiny::tx tx;
            in >> tx;
            // we want to catch up with whatever block was mined before tx
            // unless we already know when the next block is arriving
            if (next_block_time == 0) {
                tiny::block block;
                uint32_t height;
                if (rpc->get_tx_block(tx.hash, block, height)) {
                    // presumably, the next block is "block". we want to make sure
                    // we are up to speed on the previous one
                    if (height == mff->m_chain.m_tip + 1) {
                        next_block_time = block.time;
                        next_block = block.GetHash();
                    } else {
                        // find the first block whose timestamp < current_time, then mine up to it
                        // and mark up the next block
                        uint32_t height2;
                        uint256 hash2;
                        if (rpc->find_block_near_timestamp(current_time, mff->m_chain.m_tip > height - 200 ? mff->m_chain.m_tip : height - 200, height, height2, hash2)) {
                            process_block_hash(hash2);
                        } else {
                            // whatever, just confirm the block at height-1
                            rpc->get_block(height - 1, block, hash2);
                            process_block_hash(hash2);
                        }
                    }
                }
            }
            // printf("- read tx %s\n", tx.ToString().c_str());
            if (!tx.IsCoinBase()) {
                mempool->insert_tx(std::make_shared<tiny::tx>(tx));
            }
        }
        return true;
    case 0x02: // block hash
        {
            uint256 blockhash;
            in >> blockhash;
            return process_block_hash(blockhash);
        }
    default:
        // ???
        fprintf(stderr, "\nunknown command %02x\n", pid);
        assert(0);
    }
}

void ajb::confirm(uint32_t height, const uint256& hash, tiny::block& b) {
    // TODO: ensure that logic is in place for the case where a jump occurs (back and/or forth in chain height)
    // TODO: the code below handled this, but is supposed to have moved
    // while (mff->get_height() > 0 && height < mff->get_height() + 1) {
    //     // mplinfo("unconfirming block #%u\n", active_chain.height);
    //     mempool->reorg_block(mff->get_height());
    //     mff->unconfirm_tip(current_time);
    // }
    // if (mff->get_height() != 0 && height != 1 + mff->get_height()) {
    //     fprintf(stderr, "*** active chain height = %u, confirming height = %u, expecting %u\n", mff->get_height(), height, mff->get_height() + 1);
    // }
    // assert(mff->m_chain.get_blocks().size() == 0 || height == mff->get_height() + 1);
    mempool->process_block(height, hash, b.vtx);
}

} // namespace mff
