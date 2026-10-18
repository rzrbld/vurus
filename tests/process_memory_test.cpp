#include "io/process_memory.h"
#include <sstream>
#include <stdexcept>
int main(){
 vyruz::ProcessMemory m;
 std::istringstream status("Name: game\nVmSize: 999999 kB\nVmRSS: 100 kB\nRssAnon: 60 kB\nRssFile: 30 kB\nRssShmem: 10 kB\n");
 vyruz::readMemoryStatus(status,m);
 if(m.rss!=100||m.anonymous!=60||m.file!=30||m.shmem!=10||m.pss)throw std::runtime_error("resident fields");
 std::istringstream rollup("0000-ffff ---p\nRss: 100 kB\nPss: 75 kB\nPrivate_Clean: 5 kB\nPrivate_Dirty: 45 kB\nShared_Clean: 40 kB\nShared_Dirty: 10 kB\n");
 vyruz::readMemoryRollup(rollup,m);
 if(m.pss!=75||m.privatePages!=50||m.sharedPages!=50)throw std::runtime_error("proportional/private/shared fields");
 vyruz::ProcessMemory missing;std::istringstream empty;vyruz::readMemoryStatus(empty,missing);
 if(missing.rss)throw std::runtime_error("unavailable is not zero");
}
