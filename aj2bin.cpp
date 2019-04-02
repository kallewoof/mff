#include <cstdio>
#include <cstdlib>
#include <serialize.h>
#include <streams.h>
#include <tinytx.h>

char* buffer = (char*)malloc(1024);
size_t buffer_cap = 1024;
int64_t last_timestamp = 0;
int64_t last_btimestamp = 0;
tiny::tx latest_btx, latest_tx;
uint256 latest_bblk, latest_blk;

// bool verifying = false;
// #define V(args...) if (verifying) printf(args)

template<typename T>
inline void deserialize_hex_string(const char* string, T& object) {
    CDataStream ds(ParseHex(string), SER_DISK, 0);
    ds >> object;
}

template<typename T>
static inline void write_entry(CAutoFile& out, const T& t, uint8_t pid, int64_t timestamp) {
    uint64_t diff = timestamp >= last_timestamp ? uint64_t(timestamp - last_timestamp) : 0;
    last_timestamp = timestamp;
    out << VARINT(diff) << pid << t;
}

static inline void read_entry_header(CAutoFile& fin, uint8_t& pid) {
    uint64_t diff;
    fin >> VARINT(diff) >> pid;
    last_btimestamp += diff;
}

bool read_entry(FILE* in_fp, CAutoFile& out) {
    // <timestamp> <action> <data...>
    int64_t timestamp;
    uint32_t fraction;
    char action[64];
    if (fscanf(in_fp, "%lld.%u %s ", &timestamp, &fraction, action) != 3) {
        return false;
    }
    // V("%lld.%u %s\n", timestamp, fraction, action);

    char* bptr = buffer;
    size_t rem = buffer_cap;
    for (;;) {
        if (!fgets(bptr, rem, in_fp)) {
            if (ferror(in_fp)) {
                fprintf(stderr, "\nerror reading from input file\n");
                return false;
            }
            break;
        }
        size_t rbytes = strlen(bptr);
        if (rbytes == 0) {
            fprintf(stderr, "\nunable to read from input file\n");
            return false;
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

    // hashtx <txid>
    // rawtx <tx hex>
    // hashblock <block id>

    if (!strcmp(action, "hashtx")) {
        // a transaction entered the mempool; we can't do anything
        // with it yet, though, as we don't know inputs and stuff
        // so we loop back and fetch the next entry
        return read_entry(in_fp, out);
    }

    if (!strcmp(action, "rawtx")) {
        deserialize_hex_string(buffer, latest_tx);
        // V(" TX %s\n", latest_tx.ToString().c_str());
        if (!latest_tx.IsCoinBase()) {
            write_entry(out, latest_tx, 0x01 /* rawtx */, timestamp);
            return true;
        }
        // V(" (coinbase tx; moving along)\n");
        return read_entry(in_fp, out);
    }

    if (!strcmp(action, "hashblock")) {
        latest_blk = uint256S(buffer);
        // V(" BLOCK %s\n", latest_blk.ToString().c_str());
        write_entry(out, latest_blk, 0x02 /* hashblock */, timestamp);
        return true;
    }

    return false;
}

bool read_bentry(CAutoFile& fin) {
    uint8_t pid;
    read_entry_header(fin, pid);
    switch (pid) {
        case 0x01:
            fin >> latest_btx;
            // V(" BTX %s\n", latest_btx.ToString().c_str());
            assert(latest_btx.verify(latest_tx));
            break;
        case 0x02:
            fin >> latest_bblk;
            // V(" BBLK %s\n", latest_bblk.ToString().c_str());
            assert(latest_bblk == latest_blk);
            break;
        default:
            assert(!"invalid pid");
    }
    return true;
}

int main(const int argc, const char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "syntax: %s <input file>\n", argv[0]);
        return 1;
    }
    FILE* in_fp = fopen(argv[1], "r");
    if (!in_fp) {
        fprintf(stderr, "unable to open file for reading: %s\n", argv[1]);
        return 2;
    }
    std::string outf = std::string(argv[1]) + ".bin";
    FILE* fp = fopen(outf.c_str(), "wb");
    {
        CAutoFile af(fp, SER_DISK, 0);
        size_t entries = 0;
        while (read_entry(in_fp, af)) {
            ++entries;
        }
        printf("%zu entries\n", entries);
        fclose(in_fp);
    }

    // verify
    {
        // verifying = true;
        in_fp = fopen(argv[1], "r");
        FILE* in2_fp = fopen(outf.c_str(), "rb");
        fp = fopen(".foo.bin", "wb");
        CAutoFile inaf(in2_fp, SER_DISK, 0);
        CAutoFile af(fp, SER_DISK, 0);
        while (read_entry(in_fp, af)) {
            assert(read_bentry(inaf));
        }
        fclose(in_fp);
    }
}
