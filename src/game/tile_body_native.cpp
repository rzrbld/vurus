#include "game/tile_body_native.h"
#include <algorithm>
#include <sstream>
#include <utility>

namespace vyruz {
namespace {
std::uint16_t word(std::span<const std::uint8_t> data,std::size_t at){return data[at]|(std::uint16_t(data[at+1])<<8);}
std::string lower(std::string s){for(auto&c:s){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c+=32;}return s;}
std::uint32_t category(const std::string& resource) {
    const auto path=lower(resource);const auto end=path.rfind('/');if(end==std::string::npos)return 0;
    const auto start=path.rfind('/',end-1);
    const auto name=path.substr(start==std::string::npos?0:start+1,end-(start==std::string::npos?0:start+1));
    constexpr std::string_view names[]={"","bridge","corner","ground","ground+leftwall","ground+rightwall","half-ground","half-roof","horizontalwall","leftwall","obstacle","rightwall","roof","roof+","wallcorner","water"};
    for(unsigned i=1;i<16;++i)if(name==names[i])return i;
    return 0;
}
std::uint32_t resourceKind(const std::string& resource) {
    const auto path=lower(resource);const auto filename=path.rfind('/');
    if(filename==std::string::npos||!filename)return 0;
    const auto category=path.rfind('/',filename-1);if(category==std::string::npos||!category)return 0;
    const auto start=path.rfind('/',category-1);
    const auto name=path.substr(start==std::string::npos?0:start+1,category-(start==std::string::npos?0:start+1));
    return name=="anim"?1:name=="bitmap"?2:name=="object"?3:0;
}
int slot(std::uint32_t type) {
    switch(type){case 2:case 3:case 6:case 8:case 15:return 0;
    case 7:case 12:return 1;case 1:case 13:return 2;
    case 4:case 5:case 9:case 10:case 11:case 14:return 3;default:return -1;}
}
}
bool decodeNativeTileBodyCatalog(std::span<const std::uint8_t> instances,
                                std::string_view descriptions,NativeTileBodyCatalog& out,std::string& error) {
    const auto fail=[&](const char* text){error=text;return false;};
    if(instances.size()<4 || (instances.size()-4)%6)return fail("invalid native tile instance records");
    NativeTileBodyCatalog result;
    result.width=word(instances,0)/64+1;result.height=word(instances,2)/32+1;
    result.cells.resize(std::size_t(result.width)*result.height);
    result.sources.push_back({});
    std::vector<std::string> gafs(1);std::string line;
    std::istringstream text{std::string(descriptions)};int section=0,remaining=-1;
    while(std::getline(text,line)) {
        const auto start=line.find_first_not_of(" \t\r");if(start==std::string::npos)continue;
        line.erase(0,start);
        line.erase(line.find_last_not_of(" \t\r")+1);
        if(line.starts_with("# GAF files")){section=1;remaining=-1;continue;}
        if(line.starts_with("# Objects")){if(section!=1||remaining)return fail("invalid native tile sections");section=2;remaining=-1;continue;}
        if(line.front()=='#'||!section)continue;
        std::istringstream row(line);
        if(remaining<0){if(!(row>>remaining)||remaining<0||remaining>32767)return fail("invalid native tile source count");continue;}
        if(!remaining)continue;
        if(section==1)gafs.push_back(line);
        else {
            int gaf=0;std::string name;
            if(!(row>>gaf)||gaf<0||std::size_t(gaf)>=gafs.size())return fail("invalid native tile GAF reference");
            std::getline(row>>std::ws,name);if(name.empty())return fail("missing native tile resource");
            NativeTileBodySource source;source.resource=gaf?gafs[gaf]:name;source.animation=gaf?name:std::string{};
            source.kind=resourceKind(source.resource);
            source.subtype=category(source.resource);result.sources.push_back(std::move(source));
        }
        --remaining;
    }
    if(section!=2||remaining!=0)return fail("incomplete native tile sources");
    for(std::size_t at=4;at<instances.size();at+=6) {
        const auto x=word(instances,at),y=word(instances,at+2),id=word(instances,at+4);
        if(x>=result.width||y>=result.height||id>=result.sources.size())return fail("invalid native tile instance reference");
        const auto destination=slot(result.sources[id].subtype);
        if(destination>=0)result.cells[std::size_t(y)*result.width+x][destination]=id;
    }
    out=std::move(result);error.clear();return true;
}
}
