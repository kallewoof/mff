#include <bcq/bitcoin.h>
#include <test/helpers.h>
#include <serialize.h>
#include <tinymempool.h>
#include <ajb.h>
#include <amap.h>

void do_stuff();
inline std::string time_string(int64_t time);

int main(int argc, const char** argv) {
    if (argc < 3) {
        fprintf(stderr, "syntax: %s <db path> <ajb path>\n", argv[0]);
        return 1;
    }

    // do some stuff...
    do_stuff();

    // mempool object handles tx purges, double spends (conflicts), block confirmations, etc
    auto mempool = std::make_shared<tiny::mempool>();

    // mff handles the mempool file format disk I/O; it is our destination in this case
    // output is to arg 1 (a dir)
    auto mff = std::make_shared<bitcoin::mff>(argv[1], "example");
    mff->load();
    if (cq::file::accessible(std::string(argv[1]) + "/mempool.tmp")) {
        bitcoin::load_mempool(mempool, std::string(argv[1]) + "/mempool.tmp");
    }
    if (!mff->m_file) mff->begin_segment(0);
    mff->enable_reflection(std::make_shared<bitcoin::mff>(argv[1], "example", 2016, true));

    // ajb is the source ("AJ binary"); it slightly depends on the MFF object for seeing if items are
    // known beforehand or not, but this is only an optimization
    // input is arg 2 (a file)
    mff::ajb a(mff, mempool, argv[2]);

    // the mempool callback routes mempool operations into mff commands; it also hooks up to the AJB
    // object's timer
    bitcoin::mff_mempool_callback mempool_callback(a.current_time, mff);
    mempool->callback = &mempool_callback;

    // everything hooked up, we are good to go
    long internal_start_time = 0;
    long in_bytes = cq::fsize(argv[2]);
    size_t entries = 0;
    while (a.read_entry()) {
        if (!internal_start_time) {
            internal_start_time = a.current_time;
        }
        entries++;
        if (!(entries % 100)) {
            cq::id cluster = mff->get_registry().m_current_cluster;
            uint32_t block_height = mff->m_chain.m_tip;
            long pos = ftell(a.in_fp);
            long pos2 = mff->m_file->tell();
            float done = 100.f * pos / in_bytes;
            printf(" [%5.2f%%] %zu %ld -> %ld : %s <cluster=%" PRIid " block=%u>     \r", done, entries, pos, pos2, time_string(a.current_time).c_str(), cluster, block_height);
            fflush(stdout);
        }
    }
    printf("\nwriting mempool ...");
    fflush(stdout);
    bitcoin::save_mempool(mempool, std::string(argv[1]) + "/mempool.tmp");
    printf("done\n");
}

tiny::rpc* rpc = nullptr;

void do_stuff() {
    rpc = new tiny::rpc("bitcoin-cli");
    amap::amap_path = "amap";
    amap::enabled = true;
}

inline std::string time_string(int64_t time) {
    char buf[128];
    sprintf(buf, "%s", asctime(gmtime((time_t*)(&time))));
    buf[strlen(buf)-1] = 0; // remove \n
    return buf;
}
