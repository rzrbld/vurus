#include "game/native_body_shape.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char*why){if(!b)throw std::runtime_error(why);}
struct Record{uint32_t subtype,size,shape,disabled;float radius;};
int main(int argc,char**argv){try{need(argc==1||argc==2,"usage: native_body_shape_native_test [original-records]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing original shape records");}uint64_t hash=14695981039346656037ull;unsigned count=0;
 for(uint32_t subtype=0x10;subtype<=0x20;++subtype)for(uint32_t size:{0u,1u,2u,8u,20u,48u}){
  const uint32_t result[]={subtype,size,nativeBodyContactShape(subtype,float(size)),uint32_t(size==0)};
  if(argc==2){Record native{};need(bool(in.read(reinterpret_cast<char*>(&native),sizeof native)),"short original shape records");need(native.subtype==result[0]&&native.size==result[1]&&native.shape==result[2]&&native.disabled==result[3],"original41a5d0 subtype/size shape selection");}
  const auto* bytes=reinterpret_cast<const unsigned char*>(result);for(unsigned n=0;n<sizeof result;++n){hash^=bytes[n];hash*=1099511628211ull;}++count;
 }
 need(hash==0xf8edd175015ac86bull,"original bounds-constructor fingerprint");if(argc==2){char extra;need(!in.read(&extra,1),"extra original shape records");}
 std::cout<<"PASS "<<count<<" original41a5d0 shape/disabled selections across17 subtypes and6 sizes\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
