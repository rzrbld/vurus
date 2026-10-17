#include "game/tile_body_resources.h"
#include "assets/gaf.h"
#include "assets/lzss.h"
#include <algorithm>
#include <map>

namespace vyruz {
namespace {
std::string resourcePath(std::string path,std::string map) {
    for(auto& c:path){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c+=32;}
    std::replace(map.begin(),map.end(),'\\','/');
    const auto data=path.find("/data/");
    return data==std::string::npos?path:map.substr(0,map.find('/'))+path.substr(data);
}
bool animationNames(const std::vector<std::uint8_t>& bytes,std::map<std::string,std::uint32_t>& out) {
    GafChunk root;if(!parseGaf(bytes,root))return false;
    const auto* data=root.findChild("DATA");const auto* names=data?data->findChild("NAM_"):nullptr;
    if(!names)return false;
    const auto& p=names->payload;if(p.size()<96)return false;
    const auto read=[](const auto& b,std::size_t i){return std::uint32_t(b[i])|(std::uint32_t(b[i+1])<<8)|(std::uint32_t(b[i+2])<<16)|(std::uint32_t(b[i+3])<<24);};
    std::vector<std::vector<std::uint8_t>> streams(4);std::size_t at=96;
    for(unsigned i=0;i<4;++i) {
        const auto comp=read(p,i*24),compressed=read(p,i*24+12),size=read(p,i*24+16);if(!size)continue;
        if(comp!=0 && comp!=0x100)return false;
        const auto stored=comp?compressed:size;const std::size_t aligned=(std::size_t(stored)+3)&~std::size_t(3);
        if(aligned>p.size()-at)return false;
        if(comp)streams[i]=lzssDecompress(p.data()+at,stored,size);else streams[i].assign(p.begin()+at,p.begin()+at+stored);
        if(streams[i].size()!=size)return false;
        at+=aligned;
    }
    if(streams[0].size()%8 || streams[2].size()%12)return false;
    std::size_t descriptor=0,string=0;
    for(std::size_t group=0;group<streams[0].size();group+=8) {
        const auto count=read(streams[0],group),tag=read(streams[0],group+4);
        for(std::uint32_t i=0;i<count;++i,descriptor+=12) {
            if(descriptor+12>streams[2].size())return false;
            const auto length=read(streams[2],descriptor),index=read(streams[2],descriptor+4);
            if(!length || length>streams[3].size()-string)return false;
            const auto begin=streams[3].begin()+string,end=std::find(begin,begin+length,0);
            if(end==begin+length)return false;
            if(tag==0x5f4d4e41u && index)out.emplace(std::string(begin,end),index);
            string+=length;
        }
    }
    return descriptor==streams[2].size() && string==streams[3].size();
}
}
bool loadNativeTileBodyResources(NativeTileBodyCatalog& catalog,const std::string& map,
                                 const NativeTileResourceReader& reader,std::string& error) {
    std::map<std::string,std::map<std::string,std::uint32_t>> banks;
    for(const auto& source:catalog.sources)if(source.kind==1) {
        const auto path=resourcePath(source.resource,map);
        auto [it,inserted]=banks.try_emplace(path);
        if(inserted) {
            std::vector<std::uint8_t> bytes;
            if(!reader(path,bytes)||!animationNames(bytes,it->second)){error="invalid original animated terrain resource: "+path;return false;}
        }
        if(!it->second.contains(source.animation)){error="missing original terrain animation: "+path+" : "+source.animation;return false;}
    }
    for(auto& source:catalog.sources)if(source.kind==1) {
        source.resolvedResource=resourcePath(source.resource,map);
        source.sequence=banks.at(source.resolvedResource).at(source.animation);
        source.flags|=1;
    }else if(source.kind==2)source.flags&=~1u;
    error.clear();return true;
}
}
