#pragma once
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

namespace vyruz::platform {
#if defined(__APPLE__) || defined(__linux__)
// Apple libc++ lacks floating from_chars. Use an explicit C locale without
// changing process locale; retain strict full-token decimal parsing.
inline bool parseFiniteFloatClassic(std::string_view text,float& out) {
    if(text.empty() || text.front()=='+' ||
       (text.front()!='-' && text.front()!='.' && (text.front()<'0'||text.front()>'9')))
        return false;
    auto magnitude=text;
    if(magnitude.front()=='-')magnitude.remove_prefix(1);
    if(magnitude.starts_with("0x")||magnitude.starts_with("0X"))return false;
    struct Locale {
        locale_t value=newlocale(LC_NUMERIC_MASK,"C",nullptr);
        ~Locale(){if(value)freelocale(value);}
    };
    static const Locale locale;
    if(!locale.value)return false;
    const std::string copy(text);char* end=nullptr;
    const int previousErrno=errno;errno=0;
    const float value=strtof_l(copy.c_str(),&end,locale.value);
    const bool underflow=errno==ERANGE && value==0;
    errno=previousErrno;
    if(end==copy.c_str()||end!=copy.c_str()+copy.size()||underflow||!std::isfinite(value))return false;
    out=value;return true;
}
#endif
inline bool parseFiniteFloat(std::string_view text,float& out) {
#ifdef __APPLE__
    return parseFiniteFloatClassic(text,out);
#else
    float value{};
    const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||!std::isfinite(value))return false;
    out=value;return true;
#endif
}
}
