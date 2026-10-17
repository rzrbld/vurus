#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vyruz {
// Native405bb0 folds ASCII A..Z then rotates the32-bit accumulator four
// bits before XORing the sign-extended input byte.409490 resolves behavior
// keywords by this hash, including its accepted hash-collision aliases.
inline std::uint32_t enemyBehaviorKeywordHash(std::string_view text) {
    std::uint32_t result=0;
    for(unsigned char byte:text) {
        if(byte>='A'&&byte<='Z')byte+=32;
        const auto signedByte=static_cast<std::int32_t>(byte<128?int(byte):int(byte)-256);
        result=((result<<4)|(result>>28))^static_cast<std::uint32_t>(signedByte);
    }
    return result;
}
struct EnemyBehaviorParse {
    std::uint32_t flags=0;
    std::vector<std::string> tokens;
};
// Native409490: validate before publishing. Preserve ordinary authored token
// spelling (and existing catalog fingerprints); canonicalize only hash aliases
// so current Player/AI consumers receive the keyword actually resolved natively.
inline bool parseEnemyBehavior(std::string_view text,EnemyBehaviorParse& output,std::string& error) {
    constexpr std::array<std::string_view,8> words={"Player","Random","Fixed","Patrol","Straight","Seeker","Tracker","Guardian"};
    constexpr std::array<std::string_view,5> firstErrors={
        "PLAYER behaviour must be specified alone","RANDOM behaviour must be specified first",
        "FIXED behaviour must be specified first","PATROL behaviour must be specified first",
        "STRAIGHT behaviour must be specified first"};
    EnemyBehaviorParse parsed;bool base=false,target=false,guardian=false;
    text=text.substr(0,text.find('\0'));size_t cursor=0;
    const auto space=[](char ch){return ch==' '||ch=='\t';};
    const auto reject=[&](std::string_view reason){error=reason;return false;};
    while(cursor<text.size()&&space(text[cursor]))++cursor;
    while(cursor<text.size()) {
        size_t end=cursor;
        while(end<text.size()&&text[end]!='='&&!space(text[end]))++end;
        const auto token=text.substr(cursor,end-cursor);const auto hash=enemyBehaviorKeywordHash(token);
        int word=-1;
        for(size_t i=0;i<words.size();++i)if(hash==enemyBehaviorKeywordHash(words[i])){word=static_cast<int>(i);break;}
        if(word>=0&&word<5) {
            if(parsed.flags)return reject(firstErrors[word]);
            parsed.flags=word==0?3:word*4;base=word!=0;target=word==0;
        } else if(word==5||word==6) {
            if(target)return reject("Multiple use of SEEKER/TRACKER/Player");
            if(!base)return reject("SEEKER/TRACKER cannot be specified first");
            parsed.flags=(parsed.flags&~3u)|(word-4);target=true;
        } else if(word==7) {
            if(guardian)return reject("Multiple use of GUARDIAN");
            if(!base||!target)return reject("GUARDIAN must be specified in third place");
            parsed.flags|=32;guardian=true;
        } else return reject("Unexpected behaviour type");
        const auto canonical=words[word];bool same=token.size()==canonical.size();
        for(size_t i=0;same&&i<token.size();++i) {
            unsigned char ch=token[i],want=canonical[i];if(ch>='A'&&ch<='Z')ch+=32;if(want>='A'&&want<='Z')want+=32;same=ch==want;
        }
        parsed.tokens.emplace_back(same?token:canonical);
        cursor=end;while(cursor<text.size()&&space(text[cursor]))++cursor;
    }
    output=std::move(parsed);error.clear();return true;
}
}
