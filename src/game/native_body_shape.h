#pragma once
#include <cstdint>
namespace vyruz {
//41a5d0 selects by runtime subtype, independent of optional attributes.
//41a740 then marks zero-size bounds disabled and installs the authored radius.
inline uint32_t nativeBodyContactShape(uint32_t subtype,float radius) {
    if(subtype==0x11)return 1;
    if(subtype==0x13||subtype==0x14)return 4;
    if((subtype==0x10||subtype==0x15)&&radius<2)return 2;
    return 5;
}
}
