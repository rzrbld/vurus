#include "game/save_file.h"
#include "host_temp.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <zlib.h>
using namespace vyruz;
static void check(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 SaveGame source;source.currentLevel="CAVE/T1C1.3DG";
 auto& world=source.levels[source.currentLevel].world;
 world.actors.emplace_back();world.objects.emplace_back();world.actors[0].vitals.money=123456;
 world.message=std::string(100000,'A');
 std::vector<uint8_t> raw,packed;std::string error;
 check(encodeSave(source,raw,error),error.c_str());check(encodeStoredSave(source,packed,error),error.c_str());
 check(packed.size()<raw.size()/10,"compression materially reduces repetitive checkpoint size");
 check(std::string(packed.begin(),packed.begin()+8)=="VYRZPACK","new disk envelope");
 SaveGame decoded;check(decodeSave(packed,decoded,error),error.c_str());std::vector<uint8_t> restored;
 check(encodeSave(decoded,restored,error)&&restored==raw,"all state survives losslessly");
 check(decodeSave(raw,decoded,error),"legacy uncompressed v35 remains readable");
 auto owned=source;std::vector<uint8_t> moved;check(encodeStoredSave(std::move(owned),moved,error)&&moved==packed,"owned and const encoders agree");
 const auto reject=[&](const std::vector<uint8_t>& bytes){SaveGame sentinel;sentinel.currentLevel="sentinel";
  check(!decodeSave(bytes,sentinel,error)&&sentinel.currentLevel=="sentinel","invalid compressed input rejected transactionally");};
 for(size_t n:{size_t(8),size_t(19),packed.size()-1})reject({packed.begin(),packed.begin()+n});
 auto bad=packed;bad[8]=99;reject(bad);
 bad=packed;bad.back()^=1;reject(bad);
 bad=packed;bad.push_back(0);reject(bad); // zlib must consume the complete stream.
 bad=packed;for(size_t i=12;i<20;++i)bad[i]=255;reject(bad); // Reject before allocating.
 bad=packed;bad[12]^=1;reject(bad); // Exact decompressed size required.
 // A valid zlib stream must still satisfy the inner save checksum and schema.
 auto envelope=[&](const std::vector<uint8_t>& inner){uLongf size=compressBound(inner.size());std::vector<uint8_t> b(20+size);
  std::copy(packed.begin(),packed.begin()+20,b.begin());for(int i=0;i<8;++i)b[12+i]=uint8_t(uint64_t(inner.size())>>(8*i));
  check(compress2(b.data()+20,&size,inner.data(),inner.size(),1)==Z_OK,"fixture compression");b.resize(20+size);return b;};
 auto corrupt=raw;corrupt.back()^=1;reject(envelope(corrupt));reject(envelope(packed));
 const auto root=hostTempDirectory("vurus-compression-");const auto path=root/"save.vzs";
 check(writeSaveFile(path.string(),source,error),error.c_str());
 std::ifstream file(path,std::ios::binary);std::vector<uint8_t> disk((std::istreambuf_iterator<char>(file)),{});
 check(disk==packed,"file writer uses packed encoding");check(readSaveFile(path.string(),decoded,error),error.c_str());
 std::filesystem::remove_all(root);
 std::cout<<"save compression passed: "<<raw.size()<<" -> "<<packed.size()<<" bytes\n";
}
