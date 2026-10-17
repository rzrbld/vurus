#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vyruz {

// Reverse-engineered Vyruz .rsc container (reverse-engineered format).
//
// Header (32 bytes, little-endian):
//   u16 count, u16 version, u32 table1_off, u32 table1_size,
//   u32 table2_off, u32 table2_size, u32 data_off, u32 data_size,
//   u32 magic == 0x5f584b5f ("_KX_")
//
// Obfuscation:
//   directory tables: plain[i] = ~ror8(cipher[i], i & 7)
//   file payloads:    plain[i] = cipher[i] ^ 0xff
struct RscEntry {
    std::string name;
    uint16_t flags = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
};

class RscArchive {
public:
    bool open(const std::string& path);

    uint32_t entryCount() const { return static_cast<uint32_t>(entries_.size()); }
    const RscEntry& entry(uint32_t i) const { return entries_[i]; }

    // Returns decrypted file bytes for entry i.
    std::vector<uint8_t> read(uint32_t i) const;

    // Lookup by exact name (case-insensitive) or empty if not found.
    int find(const std::string& name) const;

private:
    std::string path_;
    std::vector<uint8_t> table1_;  // decrypted
    std::vector<uint8_t> table2_;  // decrypted
    uint32_t data_off_ = 0;
    std::vector<RscEntry> entries_;
};

} // namespace vyruz
