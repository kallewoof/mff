#ifndef included_mff_ajb_h_
#define included_mff_ajb_h_

#include <memory>

#include <serialize.h>
#include <streams.h>
#include <tinyrpc.h>
#include <bcq/bitcoin.h>
#include <tinymempool.h>

extern tiny::rpc* rpc;

namespace mff {

struct ajb {
    std::shared_ptr<bitcoin::mff> mff;
    std::shared_ptr<tiny::mempool> mempool;

    long current_time;
    long next_block_time{0};
    uint256 next_block;

    FILE* in_fp;
    CAutoFile in;

    char* buffer;
    size_t buffer_cap;

    ajb(std::shared_ptr<bitcoin::mff> mff_in, std::shared_ptr<tiny::mempool> mempool_in, const std::string& path = "");

    // AMAP stuff
    int64_t amap_get_output_value(const uint256& txid, int n);

    // RPC/AMAP combinator
    int64_t get_output_value(const uint256& txid, int n);
    int64_t get_tx_input_amount(tiny::tx& tx);

    bool process_block_hash(const uint256& blockhash);

    bool read_entry();
    long tell() { return ftell(in_fp); }
    void flush() { fflush(in_fp); }

    void confirm(uint32_t height, const uint256& hash, tiny::block& b);
};

} // namespace mff

#endif // included_mff_ajb_h_
