#ifndef included_cq_bitcoin_h_
#define included_cq_bitcoin_h_

#include <map>

#include <cq/cq.h>
#include <uint256.h>

namespace cq {

namespace bitcoin {

class tx : object {
public:
    uint256 id;
    prepare_for_serialization();
};

class block : serializable {
    uint32_t height;
    uint256 hash;
    prepare_for_serialization();
};

class mff : public chronology<tx> {
public:
    mff(const std::string& dbpath, const std::string& prefix, uint32_t cluster_size = 2016)
    : chronology<tx>(dbpath, prefix, cluster_size) {}
};

} // namespace bitcoin

} // namespace cq

#endif // included_cq_bitcoin_h_
