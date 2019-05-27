#include <cq/bitcoin.h>

int main(int argc, const char** argv) {
    bitcoin::mff_analyzer alr;
    bitcoin::mff* db;
    if (argc < 2) {
        fprintf(stderr, "syntax: %s <db path>\n", argv[0]);
        return 1;
    }
    try {
        db = new bitcoin::mff(&alr, argv[1], "example");
    } catch (cq::fs_error& err) {
        fprintf(stderr, "unable to open database: %s\n", err.what());
        return 2;
    }
    
    delete db;
}
