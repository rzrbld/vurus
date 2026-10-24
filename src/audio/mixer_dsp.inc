// Included inside namespace vyruz. Fixed-size realtime DSP; no ownership release.
static void smoothAudioGain(float& value,float target,float coefficient){
    value+=coefficient*(target-value);
    if(std::abs(value-target)<1e-6f)value=target; // exact mute; no denormal tails
}
size_t AudioMixer::mixVoice(Voice& v,float* stereo,size_t frames) {
    if(!v.active)return 0;
    const auto& clip=*v.clip;const double step=double(clip.wave.sampleRate)/sampleRate_;
    float busGain=&v==&music_?(musicOverlay_.active?0.f:musicGain_):&v==&musicOverlay_?musicGain_:
        v.bus==AudioBus::Voices?voicesGain_:v.bus==AudioBus::Presentation||v.bus==AudioBus::Ui?1.f:effects_;
    if(mixSettings_.balanced)busGain=(&v==&music_&&musicOverlay_.active)?0.f:1.f;
    float left,right;
    if(v.native){
        const float gain=busGain>0&&(!mixSettings_.balanced||v.nativeVolume>0)?nativeAudioVolumeGain(uint16_t(std::clamp(v.nativeVolume*busGain,0.f,65535.f))):0;
        if(mixSettings_.balanced){
            const float angle=(v.nativePan/32767.f+1)*.7853981633974483f;
            left=gain*std::max(0.f,std::cos(angle))*1.41421356237f;right=gain*std::max(0.f,std::sin(angle))*1.41421356237f;
        }else{auto pan=nativeAudioPanGains(v.nativePan);left=gain*pan.first;right=gain*pan.second;}
    }else{left=v.gain*busGain*(v.pan>0?1-v.pan:1);right=v.gain*busGain*(v.pan<0?1+v.pan:1);}
    if(v.left<0){v.left=left;v.right=right;}
    const bool quality=mixSettings_.highQuality&&clip.frames>=4&&clip.wave.sampleRate!=unsigned(sampleRate_);
    const auto read=[&](int64_t relative,float (&out)[2]){
        int64_t absolute=int64_t(v.streamOffset)+relative;
        absolute=std::max<int64_t>(0,absolute);
        if(!v.loop)absolute=std::min<int64_t>(absolute,int64_t(clip.frames)-1);
        if(clip.stream)return clip.stream->frame(uint64_t(absolute),out);
        const size_t at=v.loop?uint64_t(absolute)%clip.frames:size_t(absolute);
        out[0]=sample(clip,at,0);out[1]=sample(clip,at,1);return true;
    };
    size_t i=0;
    for(;i<frames;++i){
        if(v.position>=clip.frames){
            if(v.loop){v.streamOffset+=uint64_t(v.position/clip.frames)*clip.frames;v.position=std::fmod(v.position,double(clip.frames));}
            else{v.active=false;break;}
        }
        const auto first=int64_t(v.position);const float t=float(v.position-first);
        float result[2]{};bool ready=true;
        if(quality&&v.kernel){
            const auto& k=*v.kernel;const auto phase=unsigned(std::lround(t*AudioResampleKernel::Phases));
            const float* weights=k.weights.data()+phase*k.taps;
            for(unsigned n=0;n<k.taps;++n){float f[2];if(!read(first-int64_t(k.left)+n,f)){ready=false;break;}
                result[0]+=f[0]*weights[n];result[1]+=f[1]*weights[n];}
        }else if(quality){
            float a[2],b[2],c[2],d[2];
            ready=read(first-1,a)&&read(first,b)&&read(first+1,c)&&read(first+2,d);
            if(ready)for(unsigned ch=0;ch<2;++ch)result[ch]=cubicAudio(a[ch],b[ch],c[ch],d[ch],t);
        }else{
            float a[2],b[2];ready=read(first,a)&&read(first+1,b);
            if(ready)for(unsigned ch=0;ch<2;++ch)result[ch]=a[ch]+(b[ch]-a[ch])*t;
        }
        if(!ready){++stats_.underruns;if(clip.stream&&clip.stream->failed())v.active=false;break;}
        if(mixSettings_.balanced){
            smoothAudioGain(v.left,left,ramp_);smoothAudioGain(v.right,right,ramp_);
            v.fade=std::clamp(v.fade+(v.retiring?-1.f/(sampleRate_*v.fadeSeconds):1.f/(sampleRate_*v.fadeSeconds)),0.f,1.f);
            const float ending=!v.loop&&clip.frames>48?float(std::min(1.,(clip.frames-v.position)/(step*sampleRate_*.001))):1.f;
            stereo[2*i]+=result[0]*v.left*v.fade*ending;stereo[2*i+1]+=result[1]*v.right*v.fade*ending;
            if(v.retiring&&v.fade<=0){v.active=false;break;}
        }else{stereo[2*i]+=result[0]*left;stereo[2*i+1]+=result[1]*right;}
        v.position+=step;
    }
    if(clip.stream){
        const uint64_t position=v.streamOffset+uint64_t(v.position);
        const uint64_t history=quality?(v.kernel?v.kernel->left+1:1):0;
        clip.stream->consume(position>history?position-history:0);
    }
    if(!v.loop&&v.position>=clip.frames)v.active=false;
    return i;
}
void AudioMixer::mixBalanced(float* stereo,size_t frames){
    // Native masks bound channels; modern admission protection is main-thread only.
    constexpr size_t Block=128,Buses=7;
    std::array<std::array<float,Block*2>,Buses> buses,worldBuses;
    for(size_t offset=0;offset<frames;offset+=Block){
        const auto count=std::min(Block,frames-offset);
        for(auto&b:buses)std::fill_n(b.data(),count*2,0.f);
        for(auto&b:worldBuses)std::fill_n(b.data(),count*2,0.f);
        for(auto&v:voices_)if(!paused_||!v.pauseWithWorld)mixVoice(v,(v.pauseWithWorld?worldBuses:buses)[size_t(v.bus)].data(),count);
        for(auto&v:tails_)if((!paused_||!v.pauseWithWorld)&&!(v.bus==AudioBus::Music&&musicPaused_))mixVoice(v,(v.pauseWithWorld?worldBuses:buses)[size_t(v.bus)].data(),count);
        if(!paused_&&!musicPaused_){
            size_t done=mixVoice(music_,worldBuses[6].data(),count);
            if(!music_.active&&queuedMusic_.active){std::swap(music_,queuedMusic_);++musicSerial_;mixVoice(music_,worldBuses[6].data()+2*done,count-done);}
            mixVoice(musicOverlay_,worldBuses[6].data(),count);
        }
        for(size_t i=0;i<count;++i){
            const float pauseStep=1.f/(sampleRate_*.003f);
            pauseEnvelope_=std::clamp(pauseEnvelope_+(paused_?-pauseStep:pauseStep),0.f,1.f);
            musicEnvelope_=std::clamp(musicEnvelope_+(musicPaused_?-pauseStep:pauseStep),0.f,1.f);
            for(size_t bus=0;bus<Buses;++bus)for(unsigned ch=0;ch<2;++ch){
                const bool frozen=paused_||(bus==6&&musicPaused_);
                if(!frozen)lastWorld_[bus][ch]=worldBuses[bus][2*i+ch];
                buses[bus][2*i+ch]+=lastWorld_[bus][ch]*pauseEnvelope_*(bus==6?musicEnvelope_:1.f);
            }
            const float speech=std::max(std::abs(buses[1][i*2]),std::abs(buses[1][i*2+1]))*voicesGain_;
            const float cue=std::max(std::abs(buses[3][i*2]),std::abs(buses[3][i*2+1]))*effects_;
            const auto follow=[&](float&env,float target){smoothAudioGain(env,target,target>env?duckAttack_:duckRelease_);};
            follow(dialogueDuck_,std::clamp((speech-.008f)*8,0.f,1.f));
            follow(cueDuck_,std::clamp((cue-.015f)*6,0.f,1.f));
            smoothAudioGain(masterSmooth_,master_,ramp_);smoothAudioGain(effectsSmooth_,effects_,ramp_);
            smoothAudioGain(voicesSmooth_,voicesGain_,ramp_);smoothAudioGain(musicSmooth_,musicGain_,ramp_);
            smoothAudioGain(ambienceSmooth_,mixSettings_.ambience,ramp_);
            smoothAudioGain(headroomSmooth_,mixSettings_.headroom,ramp_);
            smoothAudioGain(duckStrengthSmooth_,mixSettings_.ducking,ramp_);
            const float duck=dialogueDuck_*duckStrengthSmooth_;
            const std::array<float,Buses> gain={
                effectsSmooth_*headroomSmooth_*(1-.4f*duck),voicesSmooth_,1.f,
                effectsSmooth_,effectsSmooth_*ambienceSmooth_*(1-.75f*duck)*(1-.45f*cueDuck_*duckStrengthSmooth_),
                1.f,musicSmooth_*(1-.45f*duck)};
            float l=0,r=0;
            for(size_t bus=0;bus<Buses;++bus){l+=buses[bus][2*i]*gain[bus];r+=buses[bus][2*i+1]*gain[bus];}
            l*=masterSmooth_;r*=masterSmooth_;
            const float peak=std::max(std::abs(l),std::abs(r));stats_.inputPeak=std::max(stats_.inputPeak,peak);
            if(peak>1)++stats_.overloads;
            // Headroom/roles do the mixing; this is a final safety limiter only.
            const float ceiling=peak>.98f?.98f/peak:1.f;
            outputGain_=std::min(ceiling,outputGain_+outputRelease_*(1-outputGain_));
            stats_.minimumGain=std::min(stats_.minimumGain,outputGain_);
            l*=outputGain_;r*=outputGain_;
            if(!std::isfinite(l)||!std::isfinite(r)){l=r=0;++stats_.clipped;}
            if(std::abs(l)>1||std::abs(r)>1)++stats_.clipped;
            stereo[2*(offset+i)]=std::clamp(l,-1.f,1.f);stereo[2*(offset+i)+1]=std::clamp(r,-1.f,1.f);
            stats_.outputPeak=std::max(stats_.outputPeak,std::max(std::abs(l),std::abs(r)));
        }
    }
}
