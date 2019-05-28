#include <bcq/bitcoin.h>
#include <test/helpers.h>
#include <serialize.h>
#include <tinymempool.h>
#include <ajb.h>
#include <amap.h>

void do_stuff();

int main(int argc, const char** argv) {
    if (argc < 3) {
        fprintf(stderr, "syntax: %s <db path> <ajb path>\n", argv[0]);
        return 1;
    }

    // mempool object handles tx purges, double spends (conflicts), block confirmations, etc
    auto mempool = std::make_shared<tiny::mempool>();

    // mff handles the mempool file format disk I/O; it is our destination in this case
    // output is to arg 1 (a dir)
    auto mff = std::make_shared<bitcoin::mff>(argv[1], "example");

    // ajb is the source ("AJ binary"); it slightly depends on the MFF object for seeing if items are
    // known beforehand or not, but this is only an optimization
    // input is arg 2 (a file)
    mff::ajb a(mff, mempool, argv[2]);

    // the mempool callback routes mempool operations into mff commands; it also hooks up to the AJB
    // object's timer
    bitcoin::mff_mempool_callback mempool_callback(a.current_time, mff);
    mempool->callback = &mempool_callback;
}

void do_stuff() {
    rpc = new tiny::rpc("bitcoin-cli");
    amap::amap_path = "amap";
    amap::enabled = true;
}
