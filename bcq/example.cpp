#include <bcq/bitcoin.h>
#include <test/helpers.h>

int main(int argc, const char** argv) {
    bitcoin::mff_analyzer alr;
    bitcoin::mff* db;
    if (argc < 2) {
        fprintf(stderr, "syntax: %s <db path>\n", argv[0]);
        return 1;
    }

    db = new bitcoin::mff(&alr, argv[1], "example");

    auto tx1 = make_random_tx();
    auto tx2 = make_random_tx();

    db->push_event(1, 0, tx1, false);

    fprintf(stderr, "db stell: %s\n", db->stell().c_str());
    delete db;
}
