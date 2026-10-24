#pragma once
#include <string>
#include <vector>
namespace vyruz {
enum class MenuPage { Closed,Main,Game,Controls,Confirm,Options,Debug,DebugLevels,DebugSurprises,Extras,SavedSessions,Benchmark };
enum class MenuAction { None,Resume,LoadGame,Restart,NewGame,Quit,Controls,EndGame,Options,Debug,DebugLevels,DebugSelectLevel,DebugAI,DebugGod,DebugAmmo,DebugMoney,DebugSurprises,DebugSelectSurprise,Extras,Scores,Credits,Intro,SlideShow,Demo,LoadSelected,ControlsHelp,Benchmark,BenchmarkDefault,BenchmarkUser,DebugInfo };
const std::vector<std::string>& debugLevels();
struct MenuEntry { std::string label;MenuAction action; };
// Port menu flow. Shared keyboard/mouse selection, with pause restoration.
class MenuView {
public:
    MenuPage page=MenuPage::Closed;
    int selected=0;
    std::string error;
    std::string optionTitle="Options";
    int optionRowHeight=30;
    std::vector<std::string> optionLabels;
    std::vector<std::string> optionHelp;
    std::vector<std::string> saveLabels,saveHelp;
    int selectedSave=-1;
    void showSavedSessions();
    std::string newGameHelp,retryHelp,controlsHelp;
    std::string selectedHelp() const;
    void showOptions();
    void showDebug(bool& paused);
    bool debugEnabled=false;
    bool slidesAvailable=true,retryAvailable=false;
    int savedSessions=0;
    bool savedSessionsLoading=false;
    bool debugAI=true,debugGod=false,debugInfo=true;
    bool debugScreen() const { return page==MenuPage::Debug || page==MenuPage::DebugLevels || page==MenuPage::DebugSurprises; }
    bool debugListFocused() const { return page==MenuPage::DebugLevels || page==MenuPage::DebugSurprises; }
    int debugSection() const { return page==MenuPage::DebugLevels?0:page==MenuPage::DebugSurprises?5:selected; }
    std::vector<MenuEntry> debugActions() const;
    std::vector<MenuEntry> debugPreview() const;
    int debugPreviewSelection() const;
    int debugPreviewFirst() const;
    std::string debugDescription() const;
    void debugFocusLeft();
    void debugFocusRight();
    static constexpr int DebugLeft=26,DebugRight=340,DebugTop=123,DebugVisible=10;
    bool paginated() const { return page==MenuPage::Options; }
    int firstRow() const;
    std::vector<int> visibleRows() const;
    int rowBaseline(int index) const;
    bool panelList() const { return page==MenuPage::SavedSessions || page==MenuPage::Controls; }
    int listRowCount() const;
    static constexpr int ListLeft=293,ListRight=630,ListTop=40,ListBottom=320,ListVisible=7;
    int rowHeight() const { return debugScreen()?(debugListFocused()?26:30):page==MenuPage::Options?optionRowHeight:panelList()?35:(paginated()||page==MenuPage::Extras)?30:RowHeight; }
    bool exitConfirmation() const { return page==MenuPage::Confirm && (pending_==MenuAction::Quit || pending_==MenuAction::EndGame); }
    int rowLeft() const { return debugScreen()?(debugListFocused()?DebugRight:DebugLeft):exitConfirmation()?Left+30:panelList()?ListLeft:Left; }
    bool open() const { return page!=MenuPage::Closed; }
    void show(bool& paused,bool main=false);
    void close(bool& paused);
    void back(bool& paused);
    MenuAction escape(bool& paused);
    void move(int delta);
    bool hover(float x,float y);
    MenuAction activate(bool& paused);
    std::vector<MenuEntry> entries() const;
    std::string title() const;
    int rowTop() const { return debugScreen()?DebugTop:exitConfirmation()?120:panelList()?70:paginated()?100:page==MenuPage::Main&&!retryAvailable?135:Top; }
    static constexpr int Left=270,Top=90,Width=360,RowHeight=45;
private:
    mutable int saveFirst_=0;
    mutable int debugFirst_=0;
    int debugLevelSelected_=0,debugSurpriseSelected_=0;
    int debugLevelFirst_=0,debugSurpriseFirst_=0;
    bool wasPaused_=false;
    MenuPage parent_=MenuPage::Game;
    MenuPage saveParent_=MenuPage::Main;
    MenuAction pending_=MenuAction::None;
};
}
