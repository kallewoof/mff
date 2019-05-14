#include <cq/bitcoin.h>

namespace cq {

namespace bitcoin {

void tx::serialize(cq::serializer* stream) const {
    id.Serialize(*stream);
}

void tx::deserialize(cq::serializer* stream) {
    id.Unserialize(*stream);
}

void block::serialize(cq::serializer* stream) const {
    *stream << height;
    hash.Serialize(*stream);
}

void block::deserialize(cq::serializer* stream) {
    *stream >> height;
    hash.Unserialize(*stream);
}

} // namespace bitcoin

} // namespace cq
