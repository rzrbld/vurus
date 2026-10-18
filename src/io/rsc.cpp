#include "io/rsc.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace vyruz {

namespace {

inline uint8_t ror8(uint8_t x, unsigned b) {
    b &= 7;
    if (b == 0) return x;
    return static_cast<uint8_t>((x >> b) | (x << (8 - b)));
}

inline uint16_t rd16(const std::vector<uint8_t>& d, size_t off) {
    return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

inline uint32_t rd32(const std::vector<uint8_t>& d, size_t off) {
    return static_cast<uint32_t>(d[off]) |
           (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) |
           (static_cast<uint32_t>(d[off + 3]) << 24);
}

std::vector<uint8_t> decryptTable(const std::vector<uint8_t>& buf) {
    std::vector<uint8_t> out(buf.size());
    for (size_t i = 0; i < buf.size(); ++i)
        out[i] = static_cast<uint8_t>(~ror8(buf[i], static_cast<unsigned>(i)));
    return out;
}

} // namespace

struct RscArchive::File {
    std::ifstream stream;
    std::mutex mutex;
    explicit File(const std::string& path):stream(path,std::ios::binary){}
    std::vector<uint8_t> read(uint64_t offset,size_t size) {
        std::vector<uint8_t> bytes(size);
        std::lock_guard lock(mutex);
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        if(!stream || (size && !stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size))))
            throw std::runtime_error("truncated archive read");
        return bytes;
    }
};

bool RscArchive::open(const std::string& path) {
    *this=RscArchive{};
    try {
        auto file=std::make_shared<File>(path);
        if(!file->stream)return false;
        file->stream.seekg(0,std::ios::end);
        const auto length=file->stream.tellg();
        if(length<32 || length>512*1024*1024)return false;
        const auto fileSize=static_cast<uint64_t>(length);
        const auto d=file->read(0,32);

        const uint32_t count = rd16(d, 0);
        const uint32_t version = rd16(d, 2);
        const uint32_t t1_off = rd32(d, 4);
        const uint32_t t1_size = rd32(d, 8);
        const uint32_t t2_off = rd32(d, 12);
        const uint32_t t2_size = rd32(d, 16);
        const uint32_t dataOffset = rd32(d, 20);
        const uint32_t data_size = rd32(d, 24);
        const uint32_t magic = rd32(d, 28);
        if (magic != 0x5f584b5f || version!=0x102 || count==0) return false;
        const auto fits=[&](uint32_t offset,uint32_t size){return offset<=fileSize && size<=fileSize-offset;};
        if (!fits(t1_off,t1_size)||!fits(t2_off,t2_size)||!fits(dataOffset,data_size)||
            uint64_t(count)*12>t1_size)return false;

        // Tables and payload may have gaps, but may not alias each other/header.
        const std::array<std::pair<uint32_t,uint32_t>,3> ranges{{{t1_off,t1_size},{t2_off,t2_size},{dataOffset,data_size}}};
        for(size_t i=0;i<ranges.size();++i){
            if(ranges[i].first<32)return false;
            for(size_t j=0;j<i;++j)if(ranges[i].second&&ranges[j].second&&
                uint64_t(ranges[i].first)<uint64_t(ranges[j].first)+ranges[j].second&&
                uint64_t(ranges[j].first)<uint64_t(ranges[i].first)+ranges[i].second)return false;
        }

        table1_ = decryptTable(file->read(t1_off,t1_size));
        table2_ = decryptTable(file->read(t2_off,t2_size));

        std::vector<RscEntry> entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * 12;
            if (off + 12 > table1_.size()) return false;
            RscEntry e;
            const uint16_t name_off = rd16(table1_, off);
            e.flags = rd16(table1_, off + 2);
            e.data_offset = rd32(table1_, off + 4);
            e.data_size = rd32(table1_, off + 8);
            if(name_off>=table2_.size() || e.data_offset>data_size || e.data_size>data_size-e.data_offset || e.data_size>256*1024*1024)return false;
            // NUL-terminated name in table2
            size_t end = name_off;
            while (end < table2_.size() && table2_[end] != 0) ++end;
            if(end==table2_.size() || end==name_off)return false;
            e.name.assign(reinterpret_cast<const char*>(&table2_[name_off]), end - name_off);
            entries.push_back(std::move(e));
        }
        entries_=std::move(entries);
        data_off_=dataOffset;
        file_=std::move(file);
        return true;
    } catch (...) {
        *this=RscArchive{};
        return false;
    }
}

std::vector<uint8_t> RscArchive::read(uint32_t i) const {
    if (i >= entries_.size()) throw std::out_of_range("rsc entry index");
    const auto& e = entries_[i];
    if(!file_)throw std::runtime_error("archive is not open");
    auto raw=file_->read(uint64_t(data_off_)+e.data_offset,e.data_size);
    for (auto& b : raw) b = static_cast<uint8_t>(b ^ 0xff);
    return raw;
}

int RscArchive::find(const std::string& name) const {
    for (uint32_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name.size() == name.size() &&
            std::equal(name.begin(), name.end(), entries_[i].name.begin(),
                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace vyruz
