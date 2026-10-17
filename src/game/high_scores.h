#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace vyruz {
// Original vz.sco:20 raw little-endian72-byte records (13 words +20 name bytes).
struct HighScoreEntry {
    std::array<uint32_t,13> values{};
    std::string name;
    enum Field:size_t { Tower=2,Level=3,Killed=4,Money=5,Hit=6,Ammo=7,Hours=8,Minutes=9,Seconds=10 };
};
using HighScores=std::array<HighScoreEntry,20>;
HighScores defaultHighScores(uint32_t randomSeed);
// 4015a0: descending lexicographic fields2..7, newer exact ties first.
unsigned insertHighScore(HighScores&,const HighScoreEntry&);
bool decodeHighScores(std::span<const uint8_t>,HighScores&,std::string& error);
std::vector<uint8_t> encodeHighScores(const HighScores&);
bool readHighScores(const std::string& path,HighScores&,std::string& error);
bool writeHighScores(const std::string& path,const HighScores&,std::string& error);
HighScoreEntry makeHighScore(std::string name,uint32_t tower,uint32_t level,uint32_t kills,uint32_t lifetimeMoney,uint64_t elapsedMs,uint32_t hits=0,uint32_t misses=0);
}
