#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace vyruz {
// Native opening threshold and 30-credit quantum; port uses a stable 60Hz rate.
class MapView {
public:
    bool open() const { return open_; }
    bool toggle(int64_t money) {
        if(open_) { close();return true; }
        if(money<21000)return false;
        open_=true;remainingMs_=0;return true;
    }
    void close() { open_=false;remainingMs_=0; }
    int64_t advance(double ms,int64_t money) {
        if(!open_ || !std::isfinite(ms) || ms<=0)return 0;
        if(money<30){close();return 0;}
        remainingMs_+=ms;
        const double steps=std::floor(remainingMs_*60/1000+1e-9);
        const int64_t affordable=money/30;
        if(steps>static_cast<double>(affordable)) { close();return affordable*30; }
        const auto charged=static_cast<int64_t>(steps);
        remainingMs_-=charged*(1000.0/60);
        return charged*30;
    }
private:
    bool open_=false;
    double remainingMs_=0;
};
}
