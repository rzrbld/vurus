#include "game/menu.h"
#include "game/surprise.h"
#include <algorithm>
namespace vyruz {
std::string MenuView::selectedHelp() const {
    const auto rows=entries();
    if(selected<0 || size_t(selected)>=rows.size())return {};
    if(page==MenuPage::Options)return size_t(selected)<optionHelp.size()?optionHelp[selected]:std::string{};
    if(page==MenuPage::SavedSessions)return savedSessionsLoading?std::string{}:size_t(selected)<saveHelp.size()?saveHelp[selected]:std::string{};
    if(page!=MenuPage::Main && page!=MenuPage::Game && page!=MenuPage::Extras && page!=MenuPage::Benchmark)return {};
    switch(rows[selected].action) {
    case MenuAction::NewGame:return newGameHelp;
    case MenuAction::Restart:return page==MenuPage::Main?retryHelp:std::string{};
    case MenuAction::Options:return "Video, Audio, Control, User settings, Language.";
    case MenuAction::Extras:return "Scores, Credits, Intro, Slide Show, Demo, Benchmark.";
    case MenuAction::LoadGame:if(savedSessionsLoading)return "Reading saved sessions...";return savedSessions?std::to_string(savedSessions)+" saved session"+(savedSessions==1?"":"s")+" available.":"No saved session.";
    case MenuAction::Benchmark:return "Seven performance scenes, score and offline reports.";
    case MenuAction::BenchmarkDefault:return "Fixed 640x480 benchmark preset. VSync off, FPS uncapped. Saves and settings unchanged.";
    case MenuAction::BenchmarkUser:return "Your saved video settings. VSync off, FPS uncapped. Saves and settings unchanged.";
    case MenuAction::Demo:return "Auto-playing game demo.";
    case MenuAction::Intro:return "Select to view intro animation.";
    default:return {};
    }
}
const std::vector<std::string>& debugLevels() {
    // Shipped map inventory, including the demo map. No campaign unlock filter.
    static const std::vector<std::string> levels={
        "UNDERGROUND/T1U1.3DG","CAVE/T1C1.3DG","CAVE/T1C2.3DG","HITEK/T1H1.3DG","HITEK/T1H2.3DG",
        "UNDERGROUND/T2U1.3DG","UNDERGROUND/T2U2.3DG","CAVE/T2C1.3DG","CAVE/T2C2.3DG","CAVE/T2C3.3DG",
        "HITEK/T2H1.3DG","HITEK/T2H2.3DG","CPU/T2P1.3DG",
        "UNDERGROUND/T3U1.3DG","UNDERGROUND/T3U2.3DG","UNDERGROUND/T3U3.3DG",
        "CAVE/T3C1.3DG","CAVE/T3C2.3DG","CAVE/T3C3.3DG",
        "HITEK/T3H1.3DG","HITEK/T3H2.3DG","HITEK/T3H3.3DG","CPU/T3P1.3DG","CPU/T3P2.3DG","CAVE/DEMO.3DG"};
    return levels;
}
void MenuView::showDebug(bool& paused) {
    if(!debugEnabled)return;
    if(!open())show(paused);
    parent_=MenuPage::Game;page=MenuPage::Debug;selected=0;error.clear();
}
std::vector<MenuEntry> MenuView::debugActions() const {
    return {{"Level select",MenuAction::DebugLevels},{debugAI?"Enemy AI: ON":"Enemy AI: OFF",MenuAction::DebugAI},{debugGod?"God mode: ON":"God mode: OFF",MenuAction::DebugGod},{"Fill all ammo",MenuAction::DebugAmmo},{"Add 1,000,000 money",MenuAction::DebugMoney},{"Surprises",MenuAction::DebugSurprises},{debugInfo?"On-screen info: ON":"On-screen info: OFF",MenuAction::DebugInfo},{"Back",MenuAction::None}};
}
std::vector<MenuEntry> MenuView::debugPreview() const {
    if(debugSection()!=0&&debugSection()!=5)return {};
    MenuView list;list.page=debugSection()==0?MenuPage::DebugLevels:MenuPage::DebugSurprises;
    return list.entries();
}
int MenuView::debugPreviewSelection() const {
    return debugListFocused()?selected:debugSection()==0?debugLevelSelected_:debugSurpriseSelected_;
}
int MenuView::debugPreviewFirst() const {
    if(debugListFocused())return firstRow();
    int first=std::clamp(debugSection()==0?debugLevelFirst_:debugSurpriseFirst_,0,std::max(0,int(debugPreview().size())-DebugVisible));
    const int selected=debugPreviewSelection();
    if(selected<first)first=selected;
    if(selected>=first+DebugVisible)first=selected-DebugVisible+1;
    return first;
}
std::string MenuView::debugDescription() const {
    switch(debugSection()) {
    case 0:return "Start a fresh test session on the selected level. Disk saves stay unchanged.";
    case 1:return "Enable or stop enemy AI. The setting carries into debug level changes.";
    case 2:return "Protect the player from damage. The setting carries into debug level changes.";
    case 3:return "Fill every authored weapon and ammunition stock for this player.";
    case 4:return "Add 1,000,000 to the current balance. Repeat to add more.";
    case 5:return "Apply a Surprise through its normal activation delay, then resume play.";
    case 6:return "Show or hide the debug FPS, frame timing, memory and object overlay, plus debug status labels. Debug controls stay available.";
    default:return "Return to the game menu. Escape again resumes play.";
    }
}
void MenuView::debugFocusLeft() {
    if(!debugListFocused())return;
    const int section=debugSection();
    (section==0?debugLevelFirst_:debugSurpriseFirst_)=firstRow();
    (section==0?debugLevelSelected_:debugSurpriseSelected_)=selected;
    page=MenuPage::Debug;selected=section;
}
void MenuView::debugFocusRight() {
    if(page!=MenuPage::Debug)return;
    const int first=debugPreviewFirst();
    if(selected==0){page=MenuPage::DebugLevels;selected=debugLevelSelected_;}
    else if(selected==5){page=MenuPage::DebugSurprises;selected=debugSurpriseSelected_;}
    debugFirst_=first;
}
void MenuView::show(bool& paused,bool main) {
    if(open())return;
    wasPaused_=paused;paused=true;page=main?MenuPage::Main:MenuPage::Game;
    selected=0;error.clear();pending_=MenuAction::None;
}
void MenuView::showOptions(){parent_=page;page=MenuPage::Options;selected=0;error.clear();}
void MenuView::showSavedSessions(){saveFirst_=0;saveParent_=page;page=MenuPage::SavedSessions;selected=int(entries().size())-1;selectedSave=-1;error.clear();}
void MenuView::close(bool& paused) {
    if(!open())return;
    page=MenuPage::Closed;paused=wasPaused_;error.clear();
}
std::vector<MenuEntry> MenuView::entries() const {
    switch(page) {
    case MenuPage::Main:{std::vector<MenuEntry> rows;
        if(retryAvailable)rows.push_back({"Restart Level",MenuAction::Restart});
        for(const auto& row:std::vector<MenuEntry>{{"Start New Game",MenuAction::NewGame},{"Load game",MenuAction::LoadGame},{"Options",MenuAction::Options},{"Extras",MenuAction::Extras},{"Exit",MenuAction::Quit}})rows.push_back(row);
        return rows;}
    case MenuPage::Extras:{std::vector<MenuEntry> rows={{"Scores",MenuAction::Scores},{"Credits",MenuAction::Credits},{"Intro",MenuAction::Intro}};if(slidesAvailable)rows.push_back({"Slide Show",MenuAction::SlideShow});rows.push_back({"Demo",MenuAction::Demo});rows.push_back({"Controls help",MenuAction::ControlsHelp});rows.push_back({"Benchmark",MenuAction::Benchmark});rows.push_back({"Back",MenuAction::None});return rows;}
    case MenuPage::Benchmark:return {{"Default",MenuAction::BenchmarkDefault},{"User Settings",MenuAction::BenchmarkUser}};
    case MenuPage::Game:{std::vector<MenuEntry> rows={{"Resume",MenuAction::Resume},{"Load game",MenuAction::LoadGame},{"Restart level",MenuAction::Restart},{"Options",MenuAction::Options},{"Controls",MenuAction::Controls},{"End game",MenuAction::EndGame}};
        if(debugEnabled)rows.push_back({"Debug / testing (F2)",MenuAction::Debug});
        return rows;}
    case MenuPage::Options:{std::vector<MenuEntry> rows;for(const auto& label:optionLabels)rows.push_back({label,MenuAction::None});return rows;}
    case MenuPage::SavedSessions:{if(savedSessionsLoading)return {{"Reading saved sessions...",MenuAction::None},{"Back",MenuAction::None}};std::vector<MenuEntry> rows;for(const auto& label:saveLabels)rows.push_back({label,MenuAction::LoadSelected});if(rows.size()<7)rows.push_back({"",MenuAction::None});rows.push_back({"Back",MenuAction::None});return rows;}
    case MenuPage::Debug:return debugActions();
    case MenuPage::DebugLevels:{std::vector<MenuEntry> rows;for(const auto& path:debugLevels())rows.push_back({path.substr(0,path.size()-4),MenuAction::DebugSelectLevel});return rows;}
    case MenuPage::DebugSurprises:{std::vector<MenuEntry> rows={{"Clear active Surprise",MenuAction::DebugSelectSurprise}};for(int id=1;id<=int(SurpriseKind::Nop);++id)rows.push_back({surpriseName(static_cast<SurpriseKind>(id)),MenuAction::DebugSelectSurprise});return rows;}
    case MenuPage::Controls:{
        std::vector<MenuEntry> rows;size_t begin=0;
        while(begin<controlsHelp.size()){
            const auto end=controlsHelp.find('\n',begin);
            auto label=controlsHelp.substr(begin,end==std::string::npos?end:end-begin);
            if(!label.empty()&&label.back()=='\r')label.pop_back();
            if(!label.empty())rows.push_back({std::move(label),MenuAction::None});
            if(end==std::string::npos)break;
            begin=end+1;
        }
        if(rows.size()<ListVisible)rows.push_back({"",MenuAction::None});
        rows.push_back({"Back",MenuAction::None});return rows;
    }
    case MenuPage::Confirm:
        if(pending_==MenuAction::Quit)return {{"Yes, bye.",pending_},{"No !",MenuAction::None}};
        if(pending_==MenuAction::EndGame)return {{"Yes",pending_},{"No",MenuAction::None}};
        return {{"Cancel",MenuAction::None},{"Confirm",pending_}};
    default:return {};
    }
}
std::string MenuView::title() const {
    if(page==MenuPage::Extras)return "Extras";
    if(page==MenuPage::Benchmark)return "Benchmark";
    if(page==MenuPage::SavedSessions)return "Saved sessions";
    if(page==MenuPage::Options)return optionTitle;
    if(page==MenuPage::Debug)return "Debug / testing";
    if(page==MenuPage::DebugLevels)return "Start fresh test level";
    if(page==MenuPage::DebugSurprises)return "Test a Surprise";
    if(page==MenuPage::Controls)return "Controls";
    if(page==MenuPage::Confirm) {
        if(pending_==MenuAction::LoadGame || pending_==MenuAction::LoadSelected)return "Load saved game?";
        if(pending_==MenuAction::Quit)return "Exit Now ?";
        if(pending_==MenuAction::EndGame)return "End this Game ?";
        return pending_==MenuAction::NewGame?"Start a new game?":"Restart this level?";
    }
    return page==MenuPage::Main?"":"Game menu";
}
void MenuView::back(bool& paused) {
    error.clear();
    if(page==MenuPage::SavedSessions){page=saveParent_;selected=page==MenuPage::Main&&retryAvailable?2:1;}
    else if(page==MenuPage::Benchmark){page=MenuPage::Extras;selected=slidesAvailable?6:5;}
    else if(page==MenuPage::Extras){page=MenuPage::Main;selected=retryAvailable?4:3;}
    else if(debugListFocused())debugFocusLeft();
    else if(page==MenuPage::Debug){page=MenuPage::Game;selected=0;}
    else if(page==MenuPage::Game)close(paused);
    else if(page==MenuPage::Controls||page==MenuPage::Confirm||page==MenuPage::Options){page=parent_;selected=0;}
}
MenuAction MenuView::escape(bool& paused) {
    // 4228a0 stores argument7+blank rows at descriptor+0x5c. 421e15
    // uses that row for Escape/Backspace: both Exit and End Game pass0.
    if(exitConfirmation())return pending_;
    back(paused);return MenuAction::None;
}
int MenuView::listRowCount() const {
    if(page==MenuPage::SavedSessions)return int(saveLabels.size());
    const auto rows=entries();int count=int(rows.size())-1;
    if(count>0&&rows[count-1].label.empty())--count;
    return std::max(0,count);
}
int MenuView::firstRow() const {
    if(debugListFocused()) {
        const int count=int(entries().size());debugFirst_=std::clamp(debugFirst_,0,std::max(0,count-DebugVisible));
        if(selected<debugFirst_)debugFirst_=selected;
        if(selected>=debugFirst_+DebugVisible)debugFirst_=selected-DebugVisible+1;
        return debugFirst_;
    }
    if(!panelList())return paginated()?selected/10*10:0;
    const int count=listRowCount();
    saveFirst_=std::clamp(saveFirst_,0,std::max(0,count-7));
    if(selected>=0 && selected<count){
        if(selected<saveFirst_)saveFirst_=selected;
        if(selected>=saveFirst_+7)saveFirst_=selected-6;
    }
    return saveFirst_;
}
std::vector<int> MenuView::visibleRows() const {
    const int count=int(entries().size()),first=firstRow();std::vector<int> result;
    if(panelList()){
        const int size=listRowCount();
        for(int i=first;i<std::min(size,first+ListVisible);++i)result.push_back(i);
        if(size<ListVisible)result.push_back(count-2); // Native blank spacer.
        result.push_back(count-1); // Back stays in the original panel.
    }else for(int i=first;i<std::min(count,first+10);++i)result.push_back(i);
    return result;
}
int MenuView::rowBaseline(int index) const {
    if(panelList() && index==int(entries().size())-1)
        return rowTop()+std::min(int(entries().size())-1,7)*rowHeight();
    return rowTop()+(index-firstRow())*rowHeight();
}
void MenuView::move(int delta) {
    const auto rows=entries();const int n=rows.size();if(!n)return;
    do{selected=((selected+delta)%n+n)%n;}while(delta && rows[selected].label.empty());
}
bool MenuView::hover(float x,float y) {
    if(debugScreen()) {
        if(x>=18&&x<310) {
            const int hit=int((y-(DebugTop-19))/30);
            if(y<DebugTop-19||hit>=int(debugActions().size())||y>=DebugTop-19+hit*30+26)return false;
            debugFocusLeft();selected=hit;return true;
        }
        if(x>=332&&x<614&&!debugPreview().empty()) {
            const int row=int((y-(DebugTop-19))/26),first=debugPreviewFirst();
            if(y<DebugTop-19||row>=DebugVisible||first+row>=int(debugPreview().size()))return false;
            debugFocusRight();selected=first+row;return true;
        }
        return false;
    }
    const auto rows=entries();
    if(x<rowLeft()||x>=(panelList()?ListRight:rowLeft()+Width))return false;
    for(int hit:visibleRows()){
        const int top=rowBaseline(hit)-30;
        if(y>=top && y<top+rowHeight()){
            if(rows[hit].label.empty())return false;
            selected=hit;return true;
        }
    }
    return false;
}
MenuAction MenuView::activate(bool& paused) {
    const auto rows=entries();if(selected<0||static_cast<size_t>(selected)>=rows.size())return MenuAction::None;
    const auto action=rows[selected].action;error.clear();
    if(page==MenuPage::SavedSessions) {
        if(savedSessionsLoading && selected==0)return MenuAction::None;
        if(action==MenuAction::None){back(paused);return MenuAction::None;}
        selectedSave=selected;
        if(saveParent_==MenuPage::Main)return action;
        parent_=page;pending_=action;page=MenuPage::Confirm;selected=0;return MenuAction::None;
    }
    if(action==MenuAction::Benchmark){page=MenuPage::Benchmark;selected=0;return MenuAction::None;}
    if(page==MenuPage::Benchmark)return action;
    if(page==MenuPage::Extras){if(action==MenuAction::None)back(paused);return action;}
    if(action==MenuAction::Extras){page=MenuPage::Extras;selected=0;return MenuAction::None;}
    if(page==MenuPage::Options)return MenuAction::None;
    if(page==MenuPage::DebugLevels || page==MenuPage::DebugSurprises)return action;
    if(page==MenuPage::Debug){
        if(action==MenuAction::DebugLevels || action==MenuAction::DebugSurprises){debugFocusRight();return MenuAction::None;}
        if(action==MenuAction::None)back(paused);
        return action;
    }
    if(action==MenuAction::Debug){showDebug(paused);return MenuAction::None;}
    if(page==MenuPage::Confirm) {
        if(action==MenuAction::None)back(paused);
        return action;
    }
    if(page==MenuPage::Controls){if(selected==int(rows.size())-1)back(paused);return MenuAction::None;}
    if(action==MenuAction::Options)return action;
    if(action==MenuAction::Resume){close(paused);return MenuAction::Resume;}
    if(action==MenuAction::Controls){parent_=page;page=MenuPage::Controls;saveFirst_=0;selected=int(entries().size())-1;return MenuAction::None;}
    if(action==MenuAction::LoadGame || (action==MenuAction::NewGame&&page==MenuPage::Main))return action;
    parent_=page;pending_=action;page=MenuPage::Confirm;selected=0;
    return MenuAction::None;
}
}
