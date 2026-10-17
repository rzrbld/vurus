#include "game/native_actor_cells.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
struct Record {uint32_t sequence,head[2],next[3],cell[3];};
static void need(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main(int argc,char**argv){try{
 need(argc==1||argc==2,"usage: native_actor_cells_original_test [NATIVE_RECORDS]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"native record file missing");}
 uint64_t fingerprint=14695981039346656037ull;unsigned failed=0;
 for(unsigned sequence=0;sequence<6561;++sequence){NativeActorCells cells;cells.reset(1,2);cells.cell.resize(3,NativeActorCells::Missing);cells.next.resize(3,NativeActorCells::Missing);cells.arrivalOrder.resize(3,0);
  unsigned code=sequence;for(unsigned step=0;step<4;++step){unsigned operation=code%9;code/=9;cells.relocate(operation/3,operation%3==2?NativeActorCells::Missing:operation%3);need(cells.valid(3),"producer validity");}
  const auto id=[](size_t value){return value==NativeActorCells::Missing?UINT32_MAX:static_cast<uint32_t>(value);};Record actual{};actual.sequence=sequence;for(unsigned n=0;n<2;++n)actual.head[n]=id(cells.heads[n]);for(unsigned n=0;n<3;++n){actual.next[n]=id(cells.next[n]);actual.cell[n]=id(cells.cell[n]);}
  if(argc==2){Record original{};need(bool(in.read(reinterpret_cast<char*>(&original),sizeof original)),"short native records");if(std::memcmp(&actual,&original,sizeof actual)!=0)++failed;}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&actual);for(size_t n=0;n<sizeof actual;++n){fingerprint^=bytes[n];fingerprint*=1099511628211ull;}
 }
 if(argc==2){char byte;need(!in.read(&byte,1),"extra native records");}
 need(fingerprint==0x9823d92967db6523ull,"native producer fingerprint mismatch");need(failed==0,"native producer comparison mismatch");
 std::cout<<"6561 original41a8c0/41a970 producer sequences, "<<failed<<" mismatches (4 operations,3 actors,2 cells/removal)\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
