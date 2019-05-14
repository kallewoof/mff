#include <cq/cq.h>

#include <stdexcept>
#include <vector>

#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <tinyformat.h>

#ifdef _WIN32
#   include <direct.h>
#endif

namespace cq {

void registry::serialize(serializer* stream) const {
    // CLUSTER SIZE
    stream->write((uint8_t*)&m_cluster_size, 4);
    // CLUSTERS
    m_clusters.serialize(stream);
}

void registry::deserialize(serializer* stream) {
    // CLUSTER SIZE
    stream->read((uint8_t*)&m_cluster_size, 4);
    // CLUSTERS
    m_clusters.deserialize(stream);
    m_tip = m_clusters.size() > 0
        ? *m_clusters.m.rbegin()
        : 0;
}

header::header(uint8_t version, uint64_t timestamp, id cluster) : m_cluster(cluster), m_version(version), m_timestamp_start(timestamp) {}

header::header(id cluster, serializer* stream) : m_cluster(cluster) {
    deserialize(stream);
}

void header::serialize(serializer* stream) const {
    // MAGIC
    char magic[2];
    magic[0] = 'C'; magic[1] = 'Q';
    stream->write((uint8_t*)magic, 2);
    // VERSION
    stream->write(&m_version, 1);
    // TIMESTAMP
    stream->write((uint8_t*)&m_timestamp_start, 8);
    // SEGMENTS
    m_segments.serialize(stream);
}

void header::deserialize(serializer* stream) {
    // MAGIC
    char magic[2];
    stream->read(magic, 2);
    if (magic[0] != 'C' || magic[1] != 'Q') {
        throw db_error(strprintf("magic invalid (expected 'CQ', got '%c%c')", magic[0], magic[1]));
    }
    // VERSION
    stream->read(&m_version, 1);
    // TIMESTAMP
    stream->read((uint8_t*)&m_timestamp_start, 8);
    // SEGMENTS
    m_segments.deserialize(stream);
}

void header::mark_segment(id segment, id position) {
    m_segments.m[segment] = position;
}

id header::get_segment_position(id segment) const {
    return m_segments.at(segment);
}

size_t header::get_segment_count() const {
    return m_segments.size();
}

id header::get_first_segment() const {
    return m_segments.size() ? m_segments.m.begin()->first : 0;
}

id header::get_last_segment() const {
    return m_segments.size() ? m_segments.m.rbegin()->first : 0;
}

std::string db::cluster_path(id cluster = -1) {
    char clu[6];
    sprintf(clu, "%05lld", cluster == -1 ? m_cluster : cluster);
    return m_dbpath + "/" + m_prefix + clu + ".cq"; 
}

void db::open(bool readonly) {
    // TODO: timestamps in headers/footers are all 0
    if (m_file) {
        delete m_file;
        m_file = nullptr;
    }
    if (m_header) {
        if (!m_readonly) {
            // header may have changed; write to disk before closing
            file next_file(cluster_path(m_header->m_cluster), false, true);
            m_header->serialize(&next_file);
        }
        delete m_header;
        m_header = nullptr;
    }
    if (m_footer) {
        delete m_footer;
        m_footer = nullptr;
    }

    // fprintf(stderr, "opening %s\n", cluster_path().c_str());
    m_file = new file(cluster_path(), false);
    if (readonly) {
        // read footer
        m_footer = new footer(m_cluster, m_file);
    } else {
        if (m_file->empty()) {
            // write footer
            m_footer = new footer(1, 0, m_cluster);
            m_footer->serialize(m_file);
        } else {
            // we are opening with the intent on writing, so we move to the end of the file automagically, but
            // read footer first
            m_footer = new footer(m_cluster, m_file);
            // seek to end
            m_file->seek(0, SEEK_END);
        }
    }
    m_readonly = readonly;
    id next_cluster = m_cluster + 1;
    try {
        file next_file(cluster_path(next_cluster), true);
        m_header = new header(next_cluster, &next_file);
    } catch (fs_error& e) {
        // file probably not found; start with blank header
        m_header = new header(1, 0, next_cluster);
    }
}

void db::resume() {
    if (m_reg.m_tip == 0) throw db_error("initial segment must be begun before writing to CQ database");
    m_cluster = m_reg.open_cluster_for_segment(m_reg.m_tip);
    open(false);
}

db::db(const std::string& dbpath, const std::string& prefix, uint32_t cluster_size)
    : m_dbpath(dbpath)
    , m_prefix(prefix)
    , m_cluster(0)
    , m_reg(cluster_size)
    , m_header(nullptr)
    , m_footer(nullptr)
    , m_readonly(true)
    , m_file(nullptr)
{
    if (!mkdir(dbpath)) {
        file regfile(dbpath + "/cq.registry", false);
        m_reg.deserialize(&regfile);
        resume();
    }
}

db::~db() {
    if (m_header) delete m_header;
    if (m_file) delete m_file;
}

//
// registry
//

id db::store(object* t) {
    if (m_readonly) resume();
    assert(m_file);
    assert(t);
    id rval = m_file->tell();
    t->serialize(m_file);
    t->m_sid = rval;
    return rval;
}

void db::load(object* t) {
    assert(m_file);
    assert(t);
    id rval = m_file->tell();
    t->deserialize(m_file);
    t->m_sid = rval;
}

void db::fetch(object* t, id i) {
    assert(m_file);
    assert(t);
    long p = m_file->tell();
    if (p != i) m_file->seek(i, SEEK_SET);
    t->deserialize(m_file);
    if (p != m_file->tell()) m_file->seek(p, SEEK_SET);
    t->m_sid = i;
}

void db::refer(object* t) {
    assert(m_file);
    assert(t);
    assert(t->m_sid);
    assert(t->m_sid < m_file->tell());
    varint(m_file->tell() - t->m_sid).serialize(m_file);
}

id db::derefer() {
    assert(m_file);
    return m_file->tell() - varint::load(m_file);
}

void db::refer(object** ts, size_t sz) {
    id known, unknown;
    assert(ts);
    assert(sz < 65536);

    known = 0;
    size_t klist[sz];
    size_t idx = 0;
    for (size_t i = 0; i < sz; ++i) {
        if (ts[i]->m_sid) {
            known++;
            klist[idx++] = i;
        }
    }
    unknown = sz - known;

    // bits:    purpose:
    // 0-3      1111 = known is 15 + a varint starting at next (available) byte, 0000~1110 = there are byte(bits) known (0-14)
    // 4-7      ^ s/known/unknown/g

    cond_varint<4> known_vi(known);
    cond_varint<4> unknown_vi(unknown);

    uint8_t multi_refer_header =
        (  known_vi.byteval()     )
    |   (unknown_vi.byteval() << 4);

    *m_file << multi_refer_header;
    known_vi.cond_serialize(m_file);
    unknown_vi.cond_serialize(m_file);

    // write known objects
    // TODO: binomial encoding etc
    id refpoint = m_file->tell();
    for (id i = 0; i < known; ++i) {
        varint(refpoint - ts[klist[i]]->m_sid).serialize(m_file);
    }
    // write unknown object refs
    for (id i = 0; i < sz; ++i) {
        if (ts[i]->m_sid == 0) {
            ts[i]->m_hash.Serialize(*m_file);
        }
    }
}

void db::derefer(std::set<id>& known_out,  std::set<uint256>& unknown_out) {
    known_out.clear();
    unknown_out.clear();

    uint8_t multi_refer_header;
    *m_file >> multi_refer_header;
    cond_varint<4> known_vi(multi_refer_header & 0x0f, m_file);
    cond_varint<4> unknown_vi(multi_refer_header >> 4, m_file);
    id known = known_vi.m_value;
    id unknown = unknown_vi.m_value;

    // read known objects
    // TODO: binomial encoding etc
    id refpoint = m_file->tell();
    for (id i = 0; i < known; ++i) {
        known_out.insert(refpoint - varint::load(m_file));
    }
    // read unknown refs
    for (id i = 0; i < unknown; ++i) {
        uint256 h;
        *m_file >> h;
        unknown_out.insert(h);
    }
}

void db::begin_segment(id segment_id) {
    if (segment_id < m_reg.m_tip) throw db_error("may not begin a segment < current tip");
    id new_cluster = m_reg.open_cluster_for_segment(segment_id);
    assert(m_reg.m_tip == segment_id);
    if (m_cluster != new_cluster || !m_file) {
        resume();
    }
    m_header->mark_segment(segment_id, m_file->tell());
}

void db::goto_segment(id segment_id) {
    id new_cluster = m_reg.open_cluster_for_segment(segment_id);
    if (new_cluster != m_cluster || !m_file) {
        m_cluster = new_cluster;
        open(true);
    }
    id pos = m_header->get_segment_position(segment_id);
    m_file->seek(pos, SEEK_SET);
}

void db::flush() {
    if (m_file) m_file->flush();
}

} // namespace cq
