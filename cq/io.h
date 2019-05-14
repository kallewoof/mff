#ifndef included_cq_io_h_
#define included_cq_io_h_

#include <string>
#include <map>
#include <set>
#include <vector>

#include <cstdlib>
#include <cstdio>

namespace cq {

class fs_error : public std::runtime_error { public: explicit fs_error(const std::string& str) : std::runtime_error(str) {} };
class io_error : public std::runtime_error { public: explicit io_error(const std::string& str) : std::runtime_error(str) {} };

bool mkdir(const std::string& path);
bool rmdir(const std::string& path);
bool rmfile(const std::string& path);
bool listdir(const std::string& path, std::vector<std::string>& list);
bool rmdir_r(const std::string& path);
void randomize(void* dst, size_t bytes);

typedef uint64_t id;

class serializer {
public:
    virtual ~serializer() {}
    virtual bool eof() =0;
    virtual bool empty() { return tell() == 0 && eof(); }
    virtual size_t write(const uint8_t* data, size_t len) =0;
    virtual size_t read(uint8_t* data, size_t len) =0;
    virtual void seek(long offset, int whence) =0;
    virtual long tell() =0;
    uint8_t get_uint8();
    virtual void flush() {}

    // bitcoin core compatibility
    inline size_t write(char* data, size_t len) { return write((const uint8_t*)data, len); }
    inline size_t read(char* data, size_t len) { return read((uint8_t*)data, len); }

    template<typename T> serializer& operator<<(const T& obj) { if (sizeof(T) != write((const uint8_t*)&obj, sizeof(obj))) throw fs_error("failed serialization"); return *this; }
    template<typename T> serializer& operator>>(T& obj) { if (sizeof(T) != read((uint8_t*)&obj, sizeof(obj))) throw fs_error("failed deserialization"); return *this; }

    // template<typename T> friend size_t serialize(T t) { return write((uint8_t*)&t, sizeof(T)); }
    // template<typename T> size_t deserialize(T& t) { return read((uint8_t*)&t, sizeof(T)); }

    virtual std::string to_string() const { return "?"; }
};


class serializable {
public:
    virtual void serialize(serializer* stream) const =0;
    virtual void deserialize(serializer* stream) =0;
};

struct sizer : public serializer {
    size_t m_len{0};
    sizer() {}
    sizer(serializable* s) {
        s->serialize(this);
    }
    size_t write(const uint8_t* data, size_t len) override { m_len += len; return len; }
    size_t read(uint8_t* data, size_t len) override { m_len += len; return len; }
    bool eof() override { return false; }
    void seek(long offset, int whence) override {}
    long tell() override { return (long)m_len; }
};

#define prepare_for_serialization() \
    virtual void serialize(cq::serializer* stream) const override; \
    virtual void deserialize(cq::serializer* stream) override

struct varint : public serializable {
    id m_value;
    explicit varint(id value = 0) : m_value(value) {}
    explicit varint(serializer* s) { deserialize(s); }
    prepare_for_serialization();
    static inline id load(serializer* s) { varint v(s); return v.m_value; }
};

struct conditional : public varint {
    using varint::varint;
    virtual ~conditional() {}
    virtual uint8_t byteval() const =0;
    virtual void cond_serialize(serializer* stream) const =0;
    virtual void cond_deserialize(uint8_t val, serializer* stream) =0;
};

template<uint8_t BITS>
struct cond_varint : public conditional {
private:
    // a 2 bit space would let us pre-describe the conditional varint as
    // 0b00 : 0
    // 0b01 : 1
    // 0b10 : 2
    // 0b11 : 3 + varint-value
    // so the cap here becomes (1 << 2) - 1 = 4 - 1 = 3 = 0b11
    // and anything BELOW the cap is pre-described completely, and at or above the cap requires an additional varint
    static constexpr uint8_t CAP = (1 << BITS) - 1;
public:
    using conditional::conditional;
    cond_varint(uint8_t val, serializer* s) {
        cond_deserialize(val, s);
    }
    uint8_t byteval() const override { return m_value < CAP ? m_value : CAP; }
    void cond_serialize(serializer* stream) const override {
        if (m_value >= CAP) {
            const_cast<cond_varint*>(this)->m_value -= CAP;
            varint::serialize(stream);
            const_cast<cond_varint*>(this)->m_value += CAP;
        }
    }
    void cond_deserialize(uint8_t val, serializer* stream) override {
        if (val < CAP) {
            m_value = val;
        } else {
            varint::deserialize(stream);
            m_value += CAP;
        }
    }
    void serialize(serializer* stream) const override {
        uint8_t val = m_value < CAP ? m_value : CAP;
        stream->write(&val, 1);
        cond_serialize(stream);
    }
    void deserialize(serializer* stream) override {
        uint8_t val;
        stream->read(&val, 1);
        cond_deserialize(val, stream);
    }
};

/**
 * Incmaps are efficiently encoded maps linking two ordered sequences together. The two
 * sequences must be increasing s.t. each key and value can be expressed as the previous
 * key and value + positive integers (one for the key and one for the value).
 */
struct incmap : serializable {
    std::map<id, id> m;
    prepare_for_serialization();
    bool operator==(const incmap& other) const;
    inline id at(id v) const { return m.at(v); }
    inline size_t count(id v) const { return m.count(v); }
    inline size_t size() const { return m.size(); }
    inline void clear() noexcept { m.clear(); }
};

struct unordered_set : serializable {
    std::set<id> m;
    prepare_for_serialization();
    unordered_set() {}
    unordered_set(id* ids, size_t sz) { for (size_t i =0; i < sz; ++i) m.insert(ids[i]); }
    unordered_set(const std::set<id>& ids) { m.insert(ids.begin(), ids.end()); }
    bool operator==(const unordered_set& other) const { return m == other.m; }
    inline size_t size() const { return m.size(); }
    inline void clear() noexcept { m.clear(); }
};

class file : public serializer {
private:
    long m_tell;
    FILE* m_fp;
public:
    file(FILE* fp);
    file(const std::string& path, bool readonly, bool clear = false);
    ~file() override;
    bool eof() override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t read(uint8_t* data, size_t len) override;
    void seek(long offset, int whence) override;
    long tell() override;
    void flush() override { fflush(m_fp); }
};

class chv_stream : public serializer {
private:
    long m_tell{0};
    std::vector<uint8_t> m_chv;
public:
    bool eof() override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t read(uint8_t* data, size_t len) override;
    void seek(long offset, int whence) override;
    long tell() override;
    std::string to_string() const override {
        char rv[(m_chv.size() << 1) + 1];
        char* rvp = rv;
        for (auto ch : m_chv) {
            rvp += sprintf(rvp, "%02x", ch);
        }
        return rv;
    }
};

} // namespace cq

#endif // included_cq_io_h_
