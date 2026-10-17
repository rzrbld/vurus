#pragma once
#include <array>
#include <cstdint>
#include <utility>
namespace vyruz {
// Window-size presets. Exclusive fullscreen offers SDL display modes; desktop
// borderless fullscreen uses native desktop size. Changes apply transactionally.
inline constexpr std::array<std::pair<int,int>,25> VideoModes{{
    {640,480},{800,600},{1024,768},{1280,960},{1600,1200},
    {1080,1080},{1280,1024},{2736,1824},{3000,2000},
    {1280,800},{1920,1200},{2560,1600},
    {1280,720},{1920,1080},{2560,1440},{3840,2160},{7680,4320},
    {2048,1080},{4096,2160},{2160,1080},{2400,1080},
    {2560,1080},{3440,1440},{3840,1080},{5120,1440}
}};
inline bool offeredVideoMode(int width,int height) {
    return width>=640 && height>=480 && width<=7680 && height<=4320 &&
           width>=height && int64_t(width)*9<=int64_t(height)*32;
}
}
