// Bounded JSON reader for our versioned offline reports. Never applies settings.
namespace {
struct ReportJson {
    enum Kind { Null,Boolean,Number,String,Array,Object } kind=Null;
    bool flag=false;double number=0;std::string text;
    std::vector<ReportJson> array;std::map<std::string,ReportJson> object;
    const ReportJson& at(const char* key) const {
        if(kind!=Object)throw std::runtime_error("Expected report object");
        const auto i=object.find(key);if(i==object.end())throw std::runtime_error(std::string("Missing report field: ")+key);return i->second;
    }
    std::string str() const {if(kind!=String)throw std::runtime_error("Expected report string");return text;}
    double num() const {if(kind!=Number)throw std::runtime_error("Expected report number");return number;}
    bool boolean() const {if(kind!=Boolean)throw std::runtime_error("Expected report boolean");return flag;}
    size_t integer(size_t max=10000000) const {const auto n=num();if(n<0||n>max||std::floor(n)!=n)throw std::runtime_error("Invalid report integer");return size_t(n);}
};
class ReportJsonReader {
    std::string_view s;size_t pos=0,nodes=0;
    [[noreturn]] void bad() const {throw std::runtime_error("Invalid benchmark JSON");}
    void space(){while(pos<s.size()&&(s[pos]==' '||s[pos]=='\n'||s[pos]=='\r'||s[pos]=='\t'))++pos;}
    bool take(char c){space();if(pos<s.size()&&s[pos]==c){++pos;return true;}return false;}
    unsigned hex(){unsigned n=0;for(int i=0;i<4;++i){if(pos>=s.size())bad();const char c=s[pos++];n<<=4;if(c>='0'&&c<='9')n+=c-'0';else if(c>='a'&&c<='f')n+=c-'a'+10;else if(c>='A'&&c<='F')n+=c-'A'+10;else bad();}return n;}
    std::string string(){
        if(!take('"'))bad();
        std::string out;
        while(pos<s.size()) {
            unsigned char c=s[pos++];if(c=='"')return out;if(c<32)bad();
            if(c!='\\'){out+=char(c);continue;}if(pos==s.size())bad();c=s[pos++];
            switch(c){case '"':case '\\':case '/':out+=char(c);break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{unsigned u=hex();if(u>=0xd800&&u<=0xdbff){if(pos+2>s.size()||s.substr(pos,2)!="\\u")bad();pos+=2;const auto low=hex();if(low<0xdc00||low>0xdfff)bad();u=0x10000+((u-0xd800)<<10)+(low-0xdc00);}else if(u>=0xdc00&&u<=0xdfff)bad();
                if(u<128)out+=char(u);else if(u<2048){out+=char(0xc0|(u>>6));out+=char(0x80|(u&63));}else if(u<65536){out+=char(0xe0|(u>>12));out+=char(0x80|((u>>6)&63));out+=char(0x80|(u&63));}else{out+=char(0xf0|(u>>18));out+=char(0x80|((u>>12)&63));out+=char(0x80|((u>>6)&63));out+=char(0x80|(u&63));}break;}
            default:bad();}
        }bad();
    }
    ReportJson value(unsigned depth){
        if(depth>32||++nodes>20000)bad();
        space();if(pos==s.size())bad();ReportJson v;
        if(s[pos]=='"'){v.kind=ReportJson::String;v.text=string();return v;}
        if(take('{')){v.kind=ReportJson::Object;if(take('}'))return v;do{auto key=string();if(!take(':'))bad();if(!v.object.emplace(std::move(key),value(depth+1)).second)bad();}while(take(','));if(!take('}'))bad();return v;}
        if(take('[')){v.kind=ReportJson::Array;if(take(']'))return v;do{v.array.push_back(value(depth+1));}while(take(','));if(!take(']'))bad();return v;}
        for(auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(s.substr(pos,literal.size())==literal){pos+=literal.size();v.kind=literal=="null"?ReportJson::Null:ReportJson::Boolean;v.flag=literal=="true";return v;}
        const auto begin=pos;if(s[pos]=='-')++pos;
        const auto digits=[&]{const auto start=pos;while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')++pos;if(pos==start)bad();};
        if(pos<s.size()&&s[pos]=='0')++pos;else digits();
        if(pos<s.size()&&s[pos]=='.'){++pos;digits();}
        if(pos<s.size()&&(s[pos]=='e'||s[pos]=='E')){++pos;if(pos<s.size()&&(s[pos]=='+'||s[pos]=='-'))++pos;digits();}
        // The ARM libc++ toolchain has no floating-point from_chars overload.
        // JSON always uses '.', independently of the user's numeric locale.
        v.kind=ReportJson::Number;
        std::istringstream input{std::string(s.substr(begin,pos-begin))};
        input.imbue(std::locale::classic());input>>v.number;
        if(!input||!input.eof()||!std::isfinite(v.number))bad();
        return v;
    }
public:
    explicit ReportJsonReader(std::string_view text):s(text){}
    ReportJson parse(){if(s.size()>2*1024*1024)bad();auto v=value(0);space();if(pos!=s.size())bad();return v;}
};
Stats reportStats(const ReportJson& v){
    Stats s;s.count=v.at("samples").integer(36000);s.mean=v.at("mean_ms").num();s.p50=v.at("p50_ms").num();s.p95=v.at("p95_ms").num();s.p99=v.at("p99_ms").num();s.maximum=v.at("max_ms").num();
    if(s.mean<0||s.p50<0||s.p95<s.p50||s.p99<s.p95||s.maximum<s.p99||s.mean>s.maximum||(s.count&&s.mean<=0))throw std::runtime_error("Invalid report timings");
    return s;
}
}
bool readReport(std::string_view text,Report& output,std::string& error){try{
    const auto j=ReportJsonReader(text).parse();if(j.at("schema_version").integer()!=3||j.at("protocol").str()!=protocol)throw std::runtime_error("Unsupported benchmark report");
    Report r;r.startedUtc=j.at("started_utc").str();r.build=j.at("build").str();r.complete=j.at("complete").boolean();r.standard=j.at("standard").boolean();r.error=j.at("error").str();r.nonstandardReason=j.at("nonstandard_reason").str();
    const auto& d=j.at("device");r.platform=d.at("platform").str();r.cpu=d.at("cpu").str();r.gpu=d.at("gpu").str();r.glVersion=d.at("gl_version").str();r.audioBackend=d.at("audio_backend").str();r.drawableWidth=int(d.at("drawable_width").integer(65536));r.drawableHeight=int(d.at("drawable_height").integer(65536));
    const auto& c=j.at("conditions");r.settingsPreset=c.at("settings_preset").str();r.settingsSource=c.at("settings_source").str();r.warmup=int(c.at("warmup_steps").integer(36000));r.measured=int(c.at("measured_steps").integer(36000));
    if(r.settingsPreset!="default"&&r.settingsPreset!="user")throw std::runtime_error("Unknown benchmark preset");
    r.options.width=int(c.at("width").integer(65536));r.options.height=int(c.at("height").integer(65536));r.options.windowMode=int(c.at("window_mode").integer(3));r.options.gamma=int(c.at("gamma").integer(100));r.options.hudAspect=int(c.at("hud_aspect").integer(2));r.options.spriteFiltering=int(c.at("filtering").integer(2));r.options.vsync=false;r.options.frameCap=0;
    for(auto [key,dest]:{std::pair{"shadows",&r.options.shadows},std::pair{"reactor",&r.options.reactorFx},std::pair{"vignette",&r.options.vignette},std::pair{"ground_details",&r.options.groundDetails},std::pair{"crt",&r.options.crt},std::pair{"crt_retro",&r.options.crtRetro},std::pair{"crt_bright",&r.options.crtBright},std::pair{"fxaa",&r.options.fxaa},std::pair{"smaa",&r.options.smaa},std::pair{"bloom",&r.options.bloom},std::pair{"sharpen",&r.options.sharpen}})*dest=c.at(key).boolean();
    const auto& rows=j.at("scenes");if(rows.kind!=ReportJson::Array||rows.array.size()>scenes.size()||(r.complete&&rows.array.size()!=scenes.size()))throw std::runtime_error("Invalid report scenes");
    for(const auto& row:rows.array){Result s;s.id=row.at("id").str();s.map=row.at("map").str();s.mapFingerprint=row.at("map_fingerprint_fnv1a64").str();
        if(s.id!=scenes[r.results.size()].id)throw std::runtime_error("Unexpected report scene order");
        const auto x=row.at("x").num(),y=row.at("y").num();if(std::abs(x)>1e8||std::abs(y)>1e8)throw std::runtime_error("Invalid report position");s.x=float(x);s.y=float(y);
        s.nearbyEnemies=row.at("nearby_enemies_at_start").integer();s.initialRows=row.at("initial_rows").integer();s.finalRows=row.at("final_rows").integer();s.finalLive=row.at("final_live").integer();s.maxProjectiles=row.at("max_projectiles").integer();s.burnerCrossfire=row.at("burner_crossfire").boolean();s.maxBurnersFiring=row.at("max_burners_firing_recent_100ms").integer();s.burnerCrossfireSteps=row.at("steps_with_two_burners_firing").integer();
        for(auto [key,dest]:{std::pair{"startup_rss_mib",&s.startupRssMiB},std::pair{"sampled_peak_rss_mib",&s.sampledPeakRssMiB}}){const auto& v=row.at(key);if(v.kind!=ReportJson::Null){const auto n=v.num();if(n<0)throw std::runtime_error("Invalid report memory");*dest=n;}}
        s.world=reportStats(row.at("world"));s.animation=reportStats(row.at("animation"));s.audio=reportStats(row.at("audio"));s.update=reportStats(row.at("cpu_update_total"));s.render=reportStats(row.at("render_present"));s.frame=reportStats(row.at("frame"));if(!s.frame.count)throw std::runtime_error("Missing report frames");r.results.push_back(std::move(s));
    }
    const auto& score=j.at("score");if(score.kind==ReportJson::Null){if(r.score())throw std::runtime_error("Missing report score");}else if(!r.score()||score.integer(UINT32_MAX)!=*r.score())throw std::runtime_error("Inconsistent report score");
    output=std::move(r);error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
std::optional<SavedReport> latestReport(const std::filesystem::path& root){
    std::error_code ec;std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> paths;size_t visited=0;
    for(std::filesystem::directory_iterator i(root,ec),end;!ec&&i!=end&&visited++<10000;i.increment(ec)){
        const auto path=i->path()/"report.json";std::error_code e;
        if(std::filesystem::is_regular_file(path,e)&&std::filesystem::file_size(path,e)<=2*1024*1024){const auto time=std::filesystem::last_write_time(path,e);if(!e)paths.emplace_back(time,path);}
    }
    std::sort(paths.rbegin(),paths.rend());
    for(const auto& [time,path]:paths){std::ifstream f(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{}),error;Report r;if(f&&readReport(text,r,error))return SavedReport{std::move(r),path.parent_path()};}
    return std::nullopt;
}
