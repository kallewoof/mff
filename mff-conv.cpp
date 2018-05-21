#include <set>

#include <txmempool_format.h>
#include <txmempool_format_rs.h>
#include <cliargs.h>
#include <tinytx.h>

std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}

inline mff::mff* alloc_mff_from_format(const std::string& fmt, const std::string& path, bool readonly) {
    if (fmt == "mff") return new mff::mff_rseq(path, readonly);
    else if (fmt == "mff-rs") return new mff::mff_rs(path, readonly);
    return nullptr;
}

int main(int argc, char* const* argv) {
    cliargs ca;
    ca.add_option("help", 'h', no_arg);
    ca.add_option("verbose", 'v', no_arg);
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

    bool verbose = ca.m.count('v');

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

    printf("seeking to end of output..."); fflush(stdout);
    size_t out_read = 0;
    mff::entry* e = nullptr;
    while ((e = out->read_entry())) out_read++;
    printf(" read %zu entries from %s\n", out_read, ca.l[3]);
    uint32_t entries = 0;
    int64_t last_time = e ? e->time : 0;
    while ((e = in->read_entry())) {
        if (last_time) {
            if (e->time < last_time) {
                // we cannot allow jumping back in time
                fprintf(stderr, "*** the output file (%s) ends at timestamp\n\t%lld\nwhere the input file (%s) starts at timestamp\n\t%lld\n*** writing to the output file would result in a jump back in time, which is not supported\n", ca.l[3], last_time, ca.l[1], e->time);
                exit(1);
            }
            last_time = 0;
        }
        out->write_entry(e);
        entries++;
        if (!(entries % 10000)) {
            printf("%s\r", time_string(in->last_time).c_str());
            fflush(stdout);
        }
    }
    out->flush();
}
