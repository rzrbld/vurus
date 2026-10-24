#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace vyruz {
// Cached on the main thread. Callback convolution contains no trig/allocation.
struct AudioResampleKernel {
    static constexpr unsigned Phases=128,MaxTaps=256;
    unsigned taps,left;
    std::vector<float> weights;
    AudioResampleKernel(unsigned source,unsigned output) {
        taps=std::clamp(unsigned(std::ceil(24.*source/output))&~1u,16u,MaxTaps);
        left=taps/2-1;weights.resize((Phases+1)*taps);
        constexpr double pi=3.14159265358979323846;
        const double cutoff=.94*std::min(1.,double(output)/source);
        for(unsigned phase=0;phase<=Phases;++phase){
            double total=0;
            for(unsigned i=0;i<taps;++i){
                const double x=double(i)-left-double(phase)/Phases;
                const double window=std::abs(x)<taps*.5 ? .42+.5*std::cos(2*pi*x/taps)+.08*std::cos(4*pi*x/taps):0;
                const double value=(std::abs(x)<1e-12?cutoff:std::sin(pi*cutoff*x)/(pi*x))*window;
                weights[phase*taps+i]=float(value);total+=value;
            }
            for(unsigned i=0;i<taps;++i)weights[phase*taps+i]=float(weights[phase*taps+i]/total);
        }
    }
};
inline float cubicAudio(float a,float b,float c,float d,float t) {
    return b+.5f*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
}
}
