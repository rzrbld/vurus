#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main(){
    // Call libm through volatile pointers: test the selected runtime symbols,
    // including sqrtf's error path, rather than compiler constant folding.
    float (*volatile angle)(float,float)=static_cast<float(*)(float,float)>(&std::atan2);
    float (*volatile root)(float)=static_cast<float(*)(float)>(&std::sqrt);
    float (*volatile remainder)(float,float)=static_cast<float(*)(float,float)>(&std::remainder);
    for(float value:{-7.f,-3.f,-1.f,-0.f,0.f,1.f,3.f,7.f})
        assert(remainder(value,2.f)==static_cast<float>(std::remainder(double(value),2.0)));
    assert(std::isnan(remainder(1.f,0.f)));
    constexpr float tolerance=4*std::numeric_limits<float>::epsilon();
    for(int x=-128;x<=128;++x)for(int y=-128;y<=128;++y){
        const float expected=static_cast<float>(std::atan2(double(y),double(x)));
        assert(std::abs(angle(float(y),float(x))-expected)<=tolerance);
    }
    for(float value:{0.f,1e-30f,1e-10f,.5f,1.f,2.f,1000.f,1e30f}){
        const float expected=static_cast<float>(std::sqrt(double(value)));
        assert(std::abs(root(value)-expected)<=tolerance*expected);
    }
    assert(std::signbit(root(-0.f)));
    assert(std::isnan(root(-1.f)));
    assert(std::isinf(root(std::numeric_limits<float>::infinity())));
    assert(std::signbit(angle(-0.f,1.f)));
}
