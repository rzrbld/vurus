#pragma once
#include <cctype>
#include <string_view>
namespace vyruz {
// Compare keys without copying/lowercasing entire property strings. Keep the
// same unsigned-char std::tolower semantics as the content parser.
inline bool contentNameEqual(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i]!=b[i] &&
        std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i])))return false;
    return true;
}
// TYPE/behaviour callers supply a lowercase token, as before.
inline bool contentNameIs(std::string_view a,std::string_view lower) {
    if(a.size()!=lower.size())return false;
    for(size_t i=0;i<a.size();++i)if(
        std::tolower(static_cast<unsigned char>(a[i]))!=static_cast<unsigned char>(lower[i]))return false;
    return true;
}
}
