#include <set>

#include <txmempool_format.h>
#include <txmempool_format_rs.h>
#include <txmempool_format_aj.h>
#include <cliargs.h>
#include <tinytx.h>
#include <amap.h>

std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

inline mff::mff* alloc_mff_from_format(const std::string& fmt, const std::string& path, bool readonly) {
    static size_t rseq_idx = 0;
    if (fmt == "mff") {
        rseq_idx++;
        if (rseq_idx == 1) return new mff::mff_rseq<0>(path, readonly);
        else return new mff::mff_rseq<1>(path, readonly);
    } else if (fmt == "mff-rs") return new mff::mff_rs(path, readonly);
    else if (fmt == "aj-dump") return new mff::mff_aj(path, readonly);
    return nullptr;
}

int main(int argc, char* const* argv) {
    // // sanity check
    // {
    //     tiny::tx tx;
    //     std::vector<uint8_t> data = ParseHex("010000000001032daf336c575e7d97d1262362b5c3f693a3d1b20fffd57522b9d0e7b9f941109b0100000023220020b7a446cff931ca97963d8ffabddf72ecffc31dc052970611b6d32cec6f3ba26cffffffffc66b8e5c89e6eb0f6d60c36a095e1c12656ece43dd5d6d3c2cbff5ca9c5e47db1600000023220020841bbd95a2a51f27f5cccdc1fb347ad90d6a0495e3aa426783da20d28dd02deaffffffff3d83b829984ad06653b35fa62b43b9a751d5a91368b7fdf8098e79a8e8c214a70100000023220020435a27b979845baa4de584328cd433dae0497350cb77d011dd8fa850aafc005cffffffff0216ce2000000000001976a9146a5dfa2a9b11b5328b396c697ff08708374ca75c88acf5cc25010000000017a91465ba938ad381bfe293562f8a4b12bdefb1ada98f87040047304402207b3f02bbc2a131bd73b0335192e51f71b90d36172e91aa9e736d1ec6f75202db02203436ceadf6831f9eea1e2ae2b2cd4f03d97d1dc9e2195e13fc1d5a60fd0f7e1d0147304402204e279b97c5201df63fa7ffb642e12c7976691c32dcb7fa72ac69c0c23ba1917902202cd669723e1ef9501c58e84b50e3134c83449ea3d82d296711764444a62c25ce01475221030f8357d5218d2353715485436230ee8c7f15955bae682f96769faa2159ee22f22103f62b1d27158b53be9caeae920614d9c61c0d4e3ab8a2b8afbc8e020933c6e68352ae040047304402204cf0b1570afb514ec5ce4528f43b541c70d3b9da74b4f2225e59fce80c42f38502207b4ccedf2fa3cc0bf8cb59aac165707bb283e567cc2da07ec09fa3f3d5f11ccb0147304402200bf154be7a10e2e6a0e9f0853f50806da7590a591dc7a4b42097c956f4a7e50c02206fb41442fcb8dd5e41681ecd4b39d30d9c753d0032c37579f39de74e8101119d0147522102744f16023026c65b7241b282cd2f8cf970be2fc4bbebeeb15a2d43d9f8c766142103f62b1d27158b53be9caeae920614d9c61c0d4e3ab8a2b8afbc8e020933c6e68352ae0400473044022010ce3c45690bde25d134fe04652290635840decd4759c4bb0ce3288bf716bcd80220554ce51a4d8cb9761c776e8f3fc9fc2a6fb468a187ed4aebc78cba856e0d2cb80147304402207fd4c06f2ca6b08b285e590aacb4913d5b0590490988e3f9f8f11b85cf9553cd022070a69f6a3cb1a2d1febd082287b7cdcfdfa53f6948db83ffb47cef8ef004371e0147522103ef79ab6be5ad895c534339ddf742e50cce8704994d9f2239fe3af7585e5d88f02103f62b1d27158b53be9caeae920614d9c61c0d4e3ab8a2b8afbc8e020933c6e68352ae00000000");
    //     CDataStream ds(data, SER_GETHASH, PROTOCOL_VERSION);
    //     ds >> tx;
    //     assert(tx.hash == uint256S("be1fc4cf0776dbd8c687deaf24b0b1f0ac09b41fb103041b006bcc1feb69ac9d"));
    // }
    cliargs ca;
    ca.add_option("help", 'h', no_arg);
    ca.add_option("verbose", 'v', no_arg);
    ca.add_option("rpccall", 'r', req_arg);
    ca.add_option("amap", 'm', req_arg);
    ca.add_option("do-not-append", 'x', no_arg);
    ca.add_option("mempool-file", 'p', req_arg);
    ca.parse(argc, argv);

    if (ca.m.count('h') || ca.l.size() != 4) {
        fprintf(stderr, "syntax: %s [--help|-h] [--verbose|-v] <input-format> <input-path> <output-format> <output-path>\n", argv[0]);
        fprintf(stderr,
            "available formats:\n"
            "  mff       Standard Memory File Format\n"
            "  mff-rs    Reused sequence based Memory File Format\n"
            "  aj-dump   AJ mempool history dump\n"
        );
        return 1;
    }

    if (ca.m.count('r')) {
        mff::aj_rpc_call = ca.m['r'];
    }

    if (ca.m.count('m')) {
        amap::amap_path = ca.m['m'];
        amap::enabled = true;
    }

    std::string mempool_file = ca.m.count('p') ? ca.m['p'] : "";

    bool verbose = ca.m.count('v');
    bool overwrite_output = ca.m.count('x');

    if (overwrite_output) {
        // truncate file to 0 bytes
        fclose(fopen(ca.l[3], "wb"));
    }

    // check file sizes
    FILE* fp = fopen(ca.l[1], "rb");
    if (!fp) {
        fprintf(stderr, "file not found: %s\n", ca.l[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long in_bytes = ftell(fp);
    fclose(fp);

    mff::mff* in = alloc_mff_from_format(ca.l[0], ca.l[1], true);
    if (!in) {
        fprintf(stderr, "invalid format: %s\n", ca.l[0]);
        return 1;
    }
    mff::mff* out = alloc_mff_from_format(ca.l[2], ca.l[3], false);
    if (!out) {
        fprintf(stderr, "invalid format: %s\n", ca.l[2]);
        return 1;
    }

    // map in mempool to out
    in->mempool.callback = out;

    // load mempool, if provided and if the file exists
    if (mempool_file != "") {
        FILE* fp = fopen(mempool_file.c_str(), "rb");
        if (fp) {
            fclose(fp);
            in->load_mempool(mempool_file);
        }
    }

    size_t out_read = 0;
    mff::entry* e = nullptr;

    if (!overwrite_output) {
        printf("seeking to end of output..."); fflush(stdout);
        while ((e = out->read_entry())) out_read++;
        printf(" read %zu entries from %s\n", out_read, ca.l[3]);
    }
    uint32_t entries = 0;
    int64_t last_time = e ? e->time : 0;
    int64_t start_time = GetTime();
    int64_t entry_start = 0;
    int64_t internal_time;
    in->shared_time = out->shared_time = &internal_time;
    while ((e = in->read_entry())) {
        if (last_time) {
            if (e->time < last_time) {
                // we cannot allow jumping back in time
                fprintf(stderr, "*** the output file (%s) ends at timestamp\n\t%lld\nwhere the input file (%s) starts at timestamp\n\t%lld\n*** writing to the output file would result in a jump back in time, which is not supported\n", ca.l[3], last_time, ca.l[1], e->time);
                exit(1);
            }
            last_time = 0;
        }
        if (!entry_start) entry_start = internal_time;// e->time;
        // out->write_entry(e);
        entries++;
        if (!(entries % 100)) {
            long pos = in->tell();
            float done = (float)(100 * pos) / in_bytes;
            int64_t now = GetTime();
            int64_t elapsed = now - start_time; // real time passed
            int64_t mff_time_elapsed = internal_time - entry_start; // input file time passed
            // we wanna see how many mff seconds pass per real second,
            // i.e. mff_time_elapsed / elapsed
            float s_per_s = !elapsed ? 0 : float(mff_time_elapsed) / elapsed;
            printf(" %02.1f%% %s [%u, %.3fx]\r", done, time_string(internal_time).c_str(), entries, s_per_s);
            fflush(stdout);
        }
    }
    out->flush();
    printf("\n");
    printf("skipped recs = %llu\n", skipped_recs);
    // save mempool, if provided
    if (mempool_file != "") {
        in->save_mempool(mempool_file);
    }
}
