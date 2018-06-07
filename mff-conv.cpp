#include <set>

#include <txmempool_format.h>
#include <txmempool_format_rs.h>
#include <txmempool_format_aj.h>
#include <txmempool_format_timetail.h>
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
    }
    else if (fmt == "mff-tt") return new mff::mff_rseq_tt<0>(path, readonly);
    else if (fmt == "mff-rs") return new mff::mff_rs(path, readonly);
    else if (fmt == "aj-dump") return new mff::mff_aj(path, readonly);
    return nullptr;
}

bool needs_newline = false;
bool mff_piping = false;

int main(int argc, char* const* argv) {
    cliargs ca;
    ca.add_option("help", 'h', no_arg);
    ca.add_option("verbose", 'v', no_arg);
    ca.add_option("rpccall", 'r', req_arg);
    ca.add_option("amap", 'm', req_arg);
    ca.add_option("do-not-append", 'x', no_arg);
    ca.add_option("mempool-file", 'p', req_arg);
    ca.parse(argc, argv);

    if (ca.m.count('h') || ca.l.size() < 4 || (ca.l.size() % 1)) {
        fprintf(stderr, "syntax: %s [--help|-h] [--verbose|-v] [--do-not-append|-x] [--mempool-file|-p] [--rpccall|-r] <input-format> <input-path> [<input-format-2> <input-path-2> [...]] <output-format> <output-path>\n", argv[0]);
        fprintf(stderr,
            "available formats:\n"
            "  mff       Standard Memory File Format\n"
            "  mff-tt    Time-tail MFF (Rev 2018-05-27)\n"
            "  mff-rs    Reused sequence based Memory File Format\n"
            "  aj-dump   AJ mempool history dump (ZMQ)\n"
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

    mff::cluster c;
    for (size_t i = 0; i + 2 < ca.l.size(); i += 2) {
        mff::mff* in = alloc_mff_from_format(ca.l[i], ca.l[i + 1], true);
        if (!in) {
            fprintf(stderr, "invalid format: %s\n", ca.l[i]);
            return 1;
        }
        c.add(in);
        in->conversion_source = true;
        in->tag = ca.l.size() > 4 ? strprintf("IN#%zu", i/2) : "IN  ";
    }
    mff::mff* out = alloc_mff_from_format(ca.l[ca.l.size()-2], ca.l[ca.l.size()-1], false);
    if (!out) {
        fprintf(stderr, "invalid format: %s\n", ca.l[ca.l.size()-2]);
        return 1;
    }
    out->tag = "OUT ";
    for (auto& n : c.nodes) out->link_source(n);

    // map in mempool to out
    c.mempool->callback = out;

    // load mempool unless overwriting output, if provided and if the file exists
    if (!overwrite_output && mempool_file != "") {
        FILE* fp = fopen(mempool_file.c_str(), "rb");
        if (fp) {
            fclose(fp);
            c.nodes[0]->load_mempool(mempool_file);
        }
    }

    size_t out_read = 0;

    int64_t c_start_time = 0;
    if (!overwrite_output) {
        printf("seeking to end of output..."); fflush(stdout);
        if (out->read_entry()) {
            out_read++;
            c_start_time = out->last_time;
        }
        while (out->read_entry()) out_read++;
        printf(" read %zu entries from %s\n", out_read, ca.l[3]);
        uint64_t total = (uint64_t)out->tell();
        uint64_t counted = total - 2; // magic 'BM'
        for (auto& x : out->space_usage) {
            counted -= x.second;
            printf("%10s : %-10llu (%.2f%%)\n", mff::cmd_str((mff::CMD)x.first).c_str(), x.second, 100.0 * x.second / total);
        }
        printf("unaccounted: %-10llu (%.2f%%)\n", counted, 100.0 * counted / total);
    }
    uint32_t entries = 0;
    out->entry_counter = out_read;
    int64_t last_time = out->last_time;
    int64_t start_time = GetTime();
    int64_t entry_start = 0;
    int64_t internal_time;
    for (auto& n : c.nodes) {
        n->shared_time = &internal_time;
    }
    out->shared_time = &internal_time;
    // in->shared_time = out->shared_time = &internal_time;
    int64_t timepoint_a, timepoint_b, internalpoint_a, internalpoint_b;
    timepoint_a = timepoint_b = start_time;
    internalpoint_a = internalpoint_b = 0;
    while (c.read_entry()) {
        if (!internalpoint_a) {
            internalpoint_a = internalpoint_b = c.time;
        }
        if (!c_start_time) c_start_time = c.time;
        if (last_time && !overwrite_output) {
            if (c.time < last_time) {
                // we cannot allow jumping back in time
                fprintf(stderr, "*** the output file (%s) ends at timestamp\n\t%lld\nwhere the input file (%s%s) starts at timestamp\n\t%lld\n*** writing to the output file would result in a jump back in time, which is not allowed. to do this you must do a 3-file merge, i.e. include two input files and a separate output file: mff-conv a-fmt a-name b-fmt b-name out-fmt out-name\n",
                    ca.l[ca.l.size()-1],                // output file (%s
                    last_time,                          // ) ends at timestamp\n\t%lld
                    ca.l[1],                            // \nwhere the input file (%s
                    ca.l.size() > 4 ? "[, ...]" : "",   // %s
                    c.time                              // ) starts at timestamp\n\t%lld
                );
                exit(1);
            }
            last_time = 0;
        }
        if (!entry_start) entry_start = internal_time;// e->time;
        // out->write_entry(e);
        entries++;
        if (!(entries % 100)) {
            long pos = c.nodes[0]->tell();
            float done = (float)(100 * pos) / in_bytes;
            int64_t now = GetTime();
            int64_t elapsed = now - timepoint_a; // real time passed
            int64_t mff_time_elapsed = internal_time - internalpoint_a; // input file time passed
            // we wanna see how many mff seconds pass per real second,
            // i.e. mff_time_elapsed / elapsed
            float s_per_s = !elapsed ? 0 : float(mff_time_elapsed) / elapsed;
            printf(" %5.1f%% %s [%u -> %llu, %.3fx]\r", done, time_string(internal_time).c_str(), entries, out->entry_counter, s_per_s);
            fflush(stdout);
            needs_newline = true;
            if (elapsed > 20 && now - timepoint_b > 10) {
                // switch perspectives
                timepoint_a = timepoint_b;
                internalpoint_a = internalpoint_b;
                timepoint_b = now;
                internalpoint_b = internal_time;
            }
        }
    }
    out->flush();
    printf("%s", nl());
    // save mempool, if provided
    if (mempool_file != "") {
        c.nodes[0]->save_mempool(mempool_file);
    }
    printf("skipped recs = %llu\n", skipped_recs);
    if (c_start_time) {
        int64_t end_time = internal_time + 1800; // rounding
        int64_t elapsed = end_time - c_start_time;
        int64_t days = elapsed / 86400;
        int64_t hours = (elapsed % 86400) / 3600;
        printf("spans over %lld days, %lld hours\n", days, hours);
    }
    uint64_t total = (uint64_t)out->tell();
    uint64_t counted = total;
    for (auto& x : out->space_usage) {
        counted -= x.second;
        printf("%10s : %-10llu (%.2f%%)\n", mff::cmd_str((mff::CMD)x.first).c_str(), x.second, 100.0 * x.second / total);
    }
    printf("unaccounted: %-10llu (%.2f%%)\n", counted, 100.0 * counted / total);
}
