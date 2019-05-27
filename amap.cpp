#include <uint256.h>
#include <tinyformat.h>
#include <streams.h>

#include <amap.h>

namespace amap {

std::string amap_path = "amap";
bool enabled = false;

#include <utilstrencodings.h>
inline std::string hex(const uint8_t* what, size_t what_sz) {
    return HexStr(what, what + what_sz);
}

#define fbdeb(args...) if (debug) printf(args)
bool fbinsearch(FILE* fp, long start, long end, const uint8_t* what, size_t what_sz, size_t el_sz, bool debug) {
    fbdeb("binsearch(%ld, %ld [%ld entries], %s)\n", start, end, (end - start) / el_sz, hex(what, what_sz).c_str());
    assert(((end - start) % el_sz) == 0);
    uint8_t input[what_sz];
    int iters = 0;
    long real_start = start;
    long real_end = end;
    while (end > start) {
        iters++;
        if (iters > 1000) {
            // buggering
            if (debug) {
                printf("BOO\n"); assert(0);
                return false;
            }
            printf("DEBUGGING fbinsearch()\n");
            // show all entries
            for (long x = real_start; x < real_end; x += el_sz) {
                fseek(fp, x, SEEK_SET);
                fread(input, what_sz, 1, fp);
                printf("- %ld : %s\n", x, hex(input, what_sz).c_str());
            }
            return fbinsearch(fp, real_start, real_end, what, what_sz, el_sz, true);
        }
        size_t els = (end - start) / el_sz;
        long mid = start + ((els >> 1) * el_sz);
        size_t real_el = (mid - real_start) / el_sz;
        fseek(fp, mid, SEEK_SET);
        fread(input, what_sz, 1, fp);
        fbdeb("[ %ld..%ld (%ld entries): checking entry #%zu=#%zu = %ld] = %s\n", start, end, els, els>>1, real_el, mid, hex(input, what_sz).c_str());
        int c = memcmp(what, input, what_sz);
        if (c == 0) return true;
        if (c > 0) {
            // too low
            start = mid + el_sz;
        } else {
            // too high
            end = mid;
        }
    }
    return false;
}
#undef fbdeb

CAmount output_amount(const uint256& txid, int index) {
    if (!enabled) return -1;
    if (txid == uint256S("59aa5ee3db978ea8168a6973b505c31b3f5f4757330da4ef45da0f51a81c1fc9")) {
        fprintf(stderr, "you should not be asking for 59aa5... cause you should ALREADY HAVE IT\n");
        exit(1);
    }
    // The amount map file format divides the data into 3 sections:
    // - the first section is the prefix and the number of transactions as a
    //   varint. the end of this varint marks the beginning of the offset_marker
    // - the second section is a list of txids and accumulative offsets,
    //   which lets a binary search operation find a txid and its output amounts
    //   by first locating the txid entry, reading its offset value, then jumping to
    //   the file position (offset_marker + (36 - prefix_len)*txid_count) + offset
    // - the third section is a list of amounts, in the form of a varint for the
    //   amount count, and a set of varints for the amounts themselves

    // figure out which file to open
    prefix_t prefix;
    memcpy(&prefix, txid.begin(), prefix_len);
    std::string fname = amap_path + strprintf("/%x", prefix);
    FILE* fp = fopen(fname.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "cannot find amap for prefix %x\n", prefix);
        assert(0);
    }
    CAutoFile af(fp, SER_DISK, 0);
    prefix_t cmp;
    size_t txcount;
    af >> cmp >> VARINT(txcount);
    if (prefix != cmp) {
        fprintf(stderr, "file prefix != expected prefix: %x != %x\n", cmp, prefix);
        assert(0);
    }
    long cmarker = ftell(fp);

    if (!fbinsearch(fp, cmarker, cmarker + (36 - prefix_len) * txcount, &txid.begin()[prefix_len], 32 - prefix_len, 36 - prefix_len)) {
        // fprintf(stderr, "cannot find txid %s in file %s\n", txid.ToString().c_str(), fname.c_str());
        return -1;
    }
    uint32_t offset;
    fread(&offset, 4, 1, fp);
    fseek(fp, cmarker + (36 - prefix_len) * txcount + offset, SEEK_SET);
    size_t amounts;
    af >> VARINT(amounts);
    assert(amounts > index);
    uint64_t amt = 0;
    for (int i = 0; i <= index; i++) af >> VARINT(amt);
    return amt;
    // af closes fp
}

} // namespace amap
