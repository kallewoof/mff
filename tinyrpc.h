#ifndef included_tinyrpc_h
#define included_tinyrpc_h

#include <tinyfs.h>
#include <tinyblock.h>

namespace tiny {

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
            assert(0);
        }
        return fp;
    }

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
                    fprintf(stderr, "failed to fetch tx %s after 5 tries, aborting\n", txhex.ToString().c_str());
                    assert(0);
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

    amount get_tx_input_amount(tx& t) {
        amount amount = 0;
        tx t2;
        for (auto& input : t.vin) {
            get_tx(input.prevout.hash, t2);
            amount += t2.vout[input.prevout.n].value;
        }
        return amount;
    }
};

}

#endif // included_tinyrpc_h
