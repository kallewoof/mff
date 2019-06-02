#ifndef included_tinyrpc_h
#define included_tinyrpc_h

#include <ios>

#include <tinyfs.h>
#include <tinyblock.h>

namespace tiny {

class rpc_error : public std::runtime_error { public: explicit rpc_error(const std::string& str) : std::runtime_error(str) {} };

struct rpc {
    std::string m_call;

    rpc(const std::string& call) : m_call(call) {}

    inline File fetch(const char* cmd, const std::string& dst, bool abort_on_failure = false) {
        system(cmd);
        File fp = OpenFile(dst, "r");
        if (!fp->has_data()) {
            fp->close();
            fprintf(stderr, "RPC call failed: %s\n", cmd);
            if (!abort_on_failure) {
                fprintf(stderr, "waiting 5 seconds and trying again\n");
                sleep(5);
                return fetch(cmd, dst, true);
            }
            throw rpc_error(std::string("failed fetch operation: ") + cmd);
        }
        return fp;
    }

    uint32_t get_block_count() {
        auto f = fetch("getblockcount > .blockcount", ".blockcount");
        uint32_t count = 0;
        fscanf(f->m_fp, "%u", &count);
        return count;
    }

    // bool get_block_header(uint32_t height, uint256& blockhex) {
    //     std::string dstfinal = "blockdata/" + std::to_string(height) + ".hth";
    //     File fp = OpenFile(dstfinal, "rb");
    //     if (!fp->has_data()) {
    //         std::string dsttxt = "blockdata/" + std::to_string(height) + ".hth.txt";
    //         File fptxt = OpenFile(dsttxt, "r");
    //         if (!fptxt->has_data()) {
    //             std::string cmd = m_call + " getblockhash " + std::to_string(height) + " > " + dsttxt;
    //             try {
    //                 fptxt = fetch(cmd.c_str(), dsttxt);
    //             } catch (const rpc_error& err) {
    //                 return false;
    //             }
    //         }
    //         char hex[128];
    //         fscanf(fptxt->m_fp, "%s", hex);
    //         assert(strlen(hex) == 64);
    //         blockhex = uint256S(hex);
    //         fptxt->close();
    //         fp = OpenFile(dstfinal, "wb");
    //         fp->autofile() << blockhex;
    //         return true;
    //     }
    //     fp->autofile() >> blockhex;
    //     return true;
    // }

    bool get_block(uint32_t height, block& b, uint256& blockhex) {
        std::string dstfinal = "blockdata/" + std::to_string(height) + ".hth";
        File fp = OpenFile(dstfinal, "rb");
        if (!fp->has_data()) {
            std::string dsttxt = "blockdata/" + std::to_string(height) + ".hth.txt";
            File fptxt = OpenFile(dsttxt, "r");
            if (!fptxt->has_data()) {
                std::string cmd = m_call + " getblockhash " + std::to_string(height) + " > " + dsttxt;
                fptxt = fetch(cmd.c_str(), dsttxt);
            }
            char hex[128];
            fscanf(fptxt->m_fp, "%s", hex);
            assert(strlen(hex) == 64);
            blockhex = uint256S(hex);
            fptxt->close();
            fp = OpenFile(dstfinal, "wb");
            fp->autofile() << blockhex;
            return get_block(blockhex, b, height);
        }
        fp->autofile() >> blockhex;
        return get_block(blockhex, b, height);
    }

    bool get_block(const uint256& blockhex, block& b, uint32_t& height) {
        // printf("get block %s\n", blockhex.ToString().c_str());
        std::string dstfinal = "blockdata/" + blockhex.ToString() + ".mffb";
        File fp = OpenFile(dstfinal, "rb");
        if (!fp->has_data()) {
            std::string dsthex = "blockdata/" + blockhex.ToString() + ".hex";
            std::string dsthdr = "blockdata/" + blockhex.ToString() + ".hdr";
            File fphex = OpenFile(dsthex, "r");
            File fphdr = OpenFile(dsthdr, "r");
            if (!fphex->has_data()) {
                std::string cmd = m_call + " getblock " + blockhex.ToString() + " 0 > " + dsthex;
                fphex = fetch(cmd.c_str(), dsthex);
            }
            if (!fphdr->has_data()) {
                std::string cmd = m_call + " getblockheader " + blockhex.ToString() + " > " + dsthdr;
                fphdr = fetch(cmd.c_str(), dsthdr);
            }
            fphdr->close();
            // fclose(fphdr);                                      // closes fphdr
            std::string dstheight = std::string("blockdata/") + blockhex.ToString() + ".height";
            std::string cmd = std::string("cat ") + dsthdr + " | jq -r .height > " + dstheight;
            system(cmd.c_str());
            fphdr = OpenFile(dstheight, "r");
            if (fscanf(fphdr->m_fp, "%u", &height) != 1) {
                // block is probably an orphan block
                printf("failure to load orphan block %u=%s                          \n", height, blockhex.ToString().c_str());
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
            fphex->close();                                        // closes fphex
            blk[sz] = 0;
            std::vector<uint8_t> blkdata = ParseHex(blk);
            free(blk);
            fp = OpenFile(dstfinal, "wb+");
            // write height
            fp->write(height);
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
        // deserialize block
        fp->autofile() >> b;
        return true;
    }

    void get_tx(const uint256& txhex, tx& tx, size_t retries = 0) {
        printf("get tx %s\n", txhex.ToString().c_str());
        std::string dstfinal = "txdata/" + txhex.ToString() + ".mfft";
        File fp = OpenFile(dstfinal, "rb");
        if (!fp->has_data()) {
            std::string dsthex = "txdata/" + txhex.ToString() + ".hex";
            std::string cmd = m_call + " getrawtransaction " + txhex.ToString() + " > " + dsthex;
            File fphex = OpenFile(dsthex, "r");
            if (!fphex->has_data()) {
                fphex = fetch(cmd.c_str(), dsthex.c_str());
            }
            fseek(fphex->m_fp, 0, SEEK_END);
            size_t sz = ftell(fphex->m_fp);
            fseek(fphex->m_fp, 0, SEEK_SET);
            if (sz == 0) {
                if (retries == 5) {
                    throw rpc_error("failed to fetch tx " + txhex.ToString() + " after 5 tries");
                }
                fprintf(stderr, "failed to fetch tx %s... waiting 5 seconds and trying again...\n", txhex.ToString().c_str());
                unlink(dsthex.c_str());
                sleep(5);
                return get_tx(txhex, tx, retries + 1);
            }
            char* txhex = (char*)malloc(sz + 1);
            assert(txhex);
            fread(txhex, 1, sz, fphex->m_fp);
            fphex->close();                                      // closes fphex
            txhex[sz] = 0;
            std::vector<uint8_t> txdata = ParseHex(txhex);
            free(txhex);
            fp = OpenFile(dstfinal, "wb+");
            // write tx
            fp->write(txdata);
            fseek(fp->m_fp, 0, SEEK_SET);
            // unlink
            unlink(dsthex.c_str());
        }
        // deserialize tx
        fp->autofile() >> tx;
    }

    bool get_tx_block(const uint256& txhex, tiny::block& block, uint32_t& height, size_t retries = 0) {
        uint256 blockhex;
        std::string dstfinal = "txdata/" + txhex.ToString() + ".blk";
        File fp = OpenFile(dstfinal, "rb");
        if (!fp->has_data()) {
            std::string dsthex = "txdata/" + txhex.ToString() + ".blktxt";
            std::string cmd = m_call + " getrawtransaction " + txhex.ToString() + " 1 | jq -r .blockhash > " + dsthex;
            File fphex = OpenFile(dsthex, "r");
            if (!fphex->has_data()) {
                fphex = fetch(cmd.c_str(), dsthex.c_str());
            }
            fseek(fphex->m_fp, 0, SEEK_END);
            size_t sz = ftell(fphex->m_fp);
            if (sz == 0) {
                if (retries == 3) {
                    fprintf(stderr, "failed to fetch tx %s after 3 tries", txhex.ToString().c_str());
                    return false;
                }
                fprintf(stderr, "failed to fetch tx %s... waiting 5 seconds and trying again...\n", txhex.ToString().c_str());
                unlink(dsthex.c_str());
                sleep(5);
                return get_tx_block(txhex, block, height, retries + 1);
            }
            if (sz != 65) fprintf(stderr, "block hash wrong size: %zu\n", sz);
            fseek(fphex->m_fp, 0, SEEK_SET);
            char buf[sz+1];
            fread(buf, 1, sz, fphex->m_fp);
            fphex->close();
            buf[sz] = 0;
            blockhex = uint256S(buf);
            unlink(dsthex.c_str());
            fp = OpenFile(dstfinal, "wb");
            fp->autofile() << blockhex;
            return get_block(blockhex, block, height);
        }
        fp->autofile() >> blockhex;
        return get_block(blockhex, block, height);
    }

    amount get_tx_input_amount(tx& t) {
        amount amount = 0;
        tx t2;
        for (auto& input : t.vin) {
            get_tx(input.prevout.hash, t2);
            amount += t2.vout[input.prevout.n].value;
        }
        return amount;
    }

    bool find_block_near_timestamp(int64_t time, uint32_t min_height, uint32_t max_height, uint32_t& height, uint256& blockhash) {
        tiny::block b;
        uint256 hash;
        uint32_t h;
        bool rv = false;
        while (max_height > min_height) {
            h = min_height + ((max_height - min_height) >> 1);
            if (!get_block(h, b, hash)) throw rpc_error("failed to get block at height " + std::to_string(h));
            if (b.time < time) {
                // can go forward in time; block is within timeframe
                blockhash = hash;
                height = h;
                min_height = h + 1;
                rv = true;
            } else {
                // cannot go forward in time; block not within timeframe
                max_height = h;
            }
        }
        return rv;
    }

    // void get_blockinfo_at_time(int64_t time, uint32_t& height, uint256& hash) {
    //     uint32_t blocks = get_block_count();
    //     uint32_t l = blocks > 50000 ? blocks - 50000 : 0;
    //     uint32_t r = blocks;
    //     while (r > l) {
    //         uint32_t m = l + ((r - l) >> 1);
    //         if (!get_block_header(m, hash)) {
    //             throw rpc_error("failure when trying to get block header #" + std::to_string(m));
    //         }

    //     }
    // }
};

}

#endif // included_tinyrpc_h
