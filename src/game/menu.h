#pragma once
#include <string>
#include <vector>
namespace vyruz {
enum class MenuPage { Closed,Main,Game,Controls,Confirm,Options,Debug,DebugLevels,DebugSurprises,Extras,SavedSessions };
enum class MenuAction { None,Resume,LoadGame,Restart,NewGame,Quit,Controls,EndGame,Options,Debug,DebugLevels,DebugSelectLevel,DebugAI,DebugGod,DebugAmmo,DebugMoney,DebugSurprises,DebugSelectSurprise,Extras,Scores,Credits,Intro,SlideShow,Demo,LoadSelected,ControlsHelp };
const std::vector<std::string>& debugLevels();
struct MenuEntry { std::string label;MenuAction action; };
// Port menu flow. Shared keyboard/mouse selection, with pause restoration.
class MenuView {
public:
    MenuPage page=MenuPage::Closed;
    int selected=0;
    std::string error;
    std::string optionTitle="Options";
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
    bool debugAI=true,debugGod=false;
    bool paginated() const { return page==MenuPage::Options || page==MenuPage::DebugLevels || page==MenuPage::DebugSurprises; }
    int firstRow() const { return paginated()?selected/10*10:0; }
    int rowHeight() const { return page==MenuPage::SavedSessions?35:paginated()?30:page==MenuPage::Debug?40:RowHeight; }
    int rowLeft() const { return page==MenuPage::SavedSessions?265:Left; }
    bool open() const { return page!=MenuPage::Closed; }
    void show(bool& paused,bool main=false);
    void close(bool& paused);
    void back(bool& paused);
    void move(int delta);
    bool hover(float x,float y);
    MenuAction activate(bool& paused);
    std::vector<MenuEntry> entries() const;
    std::string title() const;
    int rowTop() const { return page==MenuPage::SavedSessions?70:paginated()?100:page==MenuPage::Controls?405:page==MenuPage::Main&&!retryAvailable?135:Top; }
    static constexpr int Left=270,Top=90,Width=360,RowHeight=45;
private:
    bool wasPaused_=false;
    MenuPage parent_=MenuPage::Game;
    MenuPage saveParent_=MenuPage::Main;
    MenuAction pending_=MenuAction::None;
};
}
