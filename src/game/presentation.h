#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace vyruz {
enum class PresentationPage { None, Story, Movie, Mission, Ending, EndingSecond, EndingTerminal,Credits,Scores,SlideShow,GameOver,DeathPrompt,ControlsHelp };
// Modal text is separate from gameplay pause/menu state. The original briefing
// reveals characters through a display-driven loop, not the popup's 50ms timer.
// 4282a0: each byte gets two presents; LF is immediate, then two final presents.
class Presentation {
public:
    PresentationPage page=PresentationPage::None;
    bool suspended=false;
    void show(PresentationPage value,std::string text) {
        text.erase(std::remove(text.begin(),text.end(),'\r'),text.end());
        text_=std::move(text);page=text_.empty() && value!=PresentationPage::Movie && value!=PresentationPage::Scores && value!=PresentationPage::SlideShow && value!=PresentationPage::GameOver && value!=PresentationPage::DeathPrompt && value!=PresentationPage::ControlsHelp?PresentationPage::None:value;
        elapsed_=0;revealed_=0;missionRepeat_=false;missionTail_=0;missionDone_=false;dismissPending_=false;suspended=false;terminalRows_={};terminalNext_=0;terminalDelay_=0;
    }
    bool open() const {return page!=PresentationPage::None;}
    bool revealing() const {return page==PresentationPage::Mission && !missionDone_;}
    std::string_view visibleText() const {
        return std::string_view(text_).substr(0,page==PresentationPage::Mission?revealed_:text_.size());
    }
    void requestDismiss() { dismissPending_=true; }
    bool dismissalReady() const { return dismissPending_ && (page!=PresentationPage::Mission || !missionRepeat_); }
    // Called once for each rendered presentation, even with a zero elapsed delta.
    // Returns whether this byte needs one bip before its first present.
    bool advance(double ms) {
        if(!open()||suspended||ms<0||!std::isfinite(ms)||(ms==0&&page!=PresentationPage::EndingTerminal&&page!=PresentationPage::Mission))return false;
        elapsed_+=ms;
        if(page==PresentationPage::EndingTerminal){advanceTerminal(ms);return false;}
        if(page==PresentationPage::Mission && !missionDone_) {
            if(missionRepeat_){missionRepeat_=false;return false;}
            while(revealed_<text_.size() && text_[revealed_]=='\n')++revealed_;
            if(revealed_==text_.size()){missionDone_=++missionTail_>=2;return false;}
            const char c=text_[revealed_++];missionRepeat_=true;
            return c!=' ' && c!='\t';
        }
        return false;
    }
    bool timedOut() const {return (page==PresentationPage::DeathPrompt && elapsed_>=10000) || (page==PresentationPage::GameOver && elapsed_>=5000) || (page==PresentationPage::Story && elapsed_>=30000) ||
        (page==PresentationPage::EndingTerminal && elapsed_>=10000);}
    //446120/450d50: real time controls row admission; notices update once
    //per rounded refresh tick AND once per render, including the render timer.
    void setTerminalRefresh(int hz) {terminalTickMs_=std::max(1,1000/(hz>0?hz:60));}
    std::string_view terminalLine(size_t row) const {
        if(page!=PresentationPage::EndingTerminal || row>=5 || !terminalRows_[row].created)return {};
        return terminalText(row).substr(0,terminalRows_[row].shown);
    }
    bool terminalCursor(size_t row) const {
        return page==PresentationPage::EndingTerminal && row<5 && terminalRows_[row].cursorShown;
    }
    double elapsedMilliseconds() const {return elapsed_;}
    void close() {page=PresentationPage::None;text_.clear();revealed_=0;elapsed_=0;suspended=false;dismissPending_=false;}
private:
    bool missionRepeat_=false,missionDone_=false,dismissPending_=false;
    int missionTail_=0;
    struct TerminalRow {
        size_t revealed=0,shown=0;
        int revealMs=50,cursorMs=500;
        bool created=false,cursor=false,cursorShown=false;
    };
    std::array<TerminalRow,5> terminalRows_{};
    size_t terminalNext_=0;
    double terminalDelay_=0;
    int terminalTickMs_=16;
    std::string_view terminalText(size_t row) const {
        size_t begin=0;
        for(size_t i=0;i<row;++i){const auto end=text_.find('\n',begin);if(end==std::string::npos)return {};begin=end+1;}
        const auto end=text_.find('\n',begin);
        return std::string_view(text_).substr(begin,end==std::string::npos?end:end-begin);
    }
    void advanceTerminal(double ms) {
        if(terminalNext_<5) {
            if(terminalDelay_<=0) {
                if(terminalNext_>0){auto& prev=terminalRows_[terminalNext_-1];prev.cursor=false;prev.cursorShown=false;}
                terminalRows_[terminalNext_].created=!terminalText(terminalNext_).empty();
                ++terminalNext_;terminalDelay_=1000;
            } else terminalDelay_-=ms;
        }
        const int updates=ms>0?std::max(1,static_cast<int>((ms+terminalTickMs_/2)/terminalTickMs_)):0;
        for(int step=0;step<=updates;++step)for(size_t i=0;i<5;++i) {
            auto& row=terminalRows_[i];if(!row.created)continue;
            const bool active=i+1==terminalNext_;
            //4315f0 draws the current prefix/cursor before advancing either.
            if(step==updates){row.shown=row.revealed;row.cursorShown=active&&row.cursor;}
            row.revealMs-=terminalTickMs_;row.cursorMs-=terminalTickMs_;
            if(row.revealMs<=0){row.revealed=std::min(row.revealed+1,terminalText(i).size());row.revealMs+=50;}
            if(row.cursorMs<=0){if(active)row.cursor=!row.cursor;row.cursorMs+=500;}
        }
    }
    std::string text_;
    size_t revealed_=0;
    double elapsed_=0;
};
inline std::string endingTerminalText(std::string_view language) {
    return language=="FRENCH"?"... ERREUR RESEAU FATALE ...\n\n... DECONNECTION ...\n\n... SYSTEME ARRETE ...":
        "... GENERAL NETWORK FAILURE ...\n\n... DISCONNECTED ...\n\n... SYSTEM HALTED ...";
}
inline std::string missionTextPath(std::string level,std::string_view language) {
    const auto dot=level.find_last_of('.');
    if(dot==std::string::npos)return {};
    return level.substr(0,dot)+(language=="FRENCH"?"-FR.TXT":"-EN.TXT");
}
}
