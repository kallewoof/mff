#ifndef included_tinyfs_h
#define included_tinyfs_h

#include <string>
#include <streams.h>

namespace tiny {

struct file {
    CAutoFile* m_af{nullptr};
    FILE* m_fp{nullptr};
    long m_fsize{0};

    file(file&& other) {
        m_fp = other.m_fp;
        m_fsize = other.m_fsize;
        other.m_fp = nullptr;
    }

    file(FILE* fp) : m_fp(fp) {
        if (m_fp) {
            long pos = ftell(m_fp);
            fseek(m_fp, SEEK_END, 0);
            m_fsize = ftell(m_fp);
            if (pos < m_fsize) fseek(m_fp, SEEK_SET, pos);
        }
    }

    file(const std::string& path, const std::string& mode) : file(fopen(path.c_str(), mode.c_str())) {}

    ~file() {
        if (m_fp) fclose(m_fp);
        if (m_af) delete m_af;
    }

    bool has_data() { return m_fp && ftell(m_fp) < m_fsize; }

    template<typename T>
    inline size_t write(const std::vector<T>& tv) {
        return fwrite(tv.data(), sizeof(T), tv.size(), m_fp);
    }

    template<typename T>
    inline size_t write(const T& t) {
        return fwrite(&t, sizeof(T), 1, m_fp);
    }

    template<typename T>
    inline size_t read(T& t) {
        return fread(&t, sizeof(T), 1, m_fp);
    }

    void close() {
        if (m_fp) fclose(m_fp);
        m_fp = nullptr;
    }

    CAutoFile& autofile() {
        if (!m_af) {
            m_af = new CAutoFile(m_fp, SER_DISK, 0);
            m_fp = nullptr;
        }
        return *m_af;
    }
};

typedef std::shared_ptr<file> File;
static inline File OpenFile(const std::string& path, const std::string& mode) {
    return std::make_shared<file>(path, mode);
}

}

#endif // included_tinyfs_h
