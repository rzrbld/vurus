// Original assets + production App flow in isolated temporary save directories.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <fstream>
#include <stdexcept>
static void need(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static std::vector<uint8_t> bytes(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static std::vector<uint8_t> worldBytes(const App&a){vyruz::SaveGame s;s.currentLevel=levelKey(a.content.mapPath);s.levels[s.currentLevel].world=a.world.snapshot();std::vector<uint8_t>b;std::string e;need(vyruz::encodeSave(s,b,e),e);return b;}
static void waitCatalog(App& a) {
 const auto start=SDL_GetTicks64();
 do{refreshSaveCatalog(a);need(SDL_GetTicks64()-start<30000,"catalog worker completes");if(a.saveCatalogDirty||a.saveCatalogFuture.valid())SDL_Delay(1);}while(a.saveCatalogDirty||a.saveCatalogFuture.valid());
}
static void catalogConcurrency(App& a,const std::filesystem::path& out) {
 const auto original=a.options.saveProfile;const bool sessionStarted=a.sessionStarted;
 refreshSaveCatalog(a,true);waitCatalog(a);
 // A worker held at a deterministic gate must not delay menu input/Back.
 invalidateSaveCatalog(a);const auto oldGeneration=a.saveCatalogGeneration;const auto oldProfile=a.options.storageProfile();
 std::promise<SaveCatalogResult> pending;a.saveCatalogFuture=pending.get_future();
 a.menu.close(a.paused);a.menu.show(a.paused,false);a.menu.showSavedSessions();refreshSaveCatalog(a);
 need(a.saveCatalogFuture.valid()&&a.menu.savedSessionsLoading,"unfinished catalog is polled, never joined");
 a.menu.back(a.paused);need(a.menu.page==vyruz::MenuPage::Game,"Back while catalog blocked");
 // Same main-menu Load Game route used after the death flow, still gated.
 a.menu.close(a.paused);a.sessionStarted=false;a.menu.show(a.paused,true);a.menuAction=vyruz::MenuAction::LoadGame;
 need(!applyMenuAction(a)&&a.menu.page==vyruz::MenuPage::SavedSessions&&a.menu.savedSessionsLoading,"post-death main-menu browser opens before catalog finishes");
 need(a.menu.entries()[a.menu.selected].label=="Back","loading main-menu browser defaults Back");
 render(a);need(!a.savePreviewTexture&&a.savePreviewPath.empty(),"loading browser never bulk-loads previews");
 need(saveScreenshot((out/(std::to_string(a.gfx.drawableWidth())+"-loading.ppm")).c_str(),a.gfx.drawableWidth(),a.gfx.drawableHeight()),"loading browser capture");
 a.menu.activate(a.paused);
 need(a.menu.page==vyruz::MenuPage::Main,"main-menu Back remains available during refresh");
 a.options.saveProfile="Different audit profile";refreshSaveCatalog(a);
 SaveCatalogResult old;old.base=a.savePath;old.profile=oldProfile;old.generation=oldGeneration;old.entries.push_back({});old.labels={"STALE PROFILE"};pending.set_value(std::move(old));
 refreshSaveCatalog(a);need(a.saveCatalog.empty(),"late old generation cannot populate new profile");waitCatalog(a);need(a.saveCatalog.size()==1&&a.saveCatalog.front().legacy&&a.menu.saveLabels.front()!="STALE PROFILE","new profile retains only the shared legacy slot, never another profile history");
 a.options.saveProfile=original;refreshSaveCatalog(a);waitCatalog(a);
 // Even a ready catalog cannot publish while an atomic save is pending.
 invalidateSaveCatalog(a);std::promise<SaveCatalogResult> ready;auto result=readSaveCatalog(a.vfs,a.savePath,a.options.storageProfile(),a.saveCatalogGeneration);ready.set_value(std::move(result));a.saveCatalogFuture=ready.get_future();
 std::promise<SaveResult> writing;a.saveFuture=writing.get_future();refreshSaveCatalog(a);
 need(a.saveCatalog.empty()&&a.saveCatalogFuture.valid(),"catalog publication defers during save transaction");
 writing.set_value({});a.saveFuture.get();invalidateSaveCatalog(a);
 refreshSaveCatalog(a);need(a.saveCatalog.empty(),"pre-commit catalog generation discarded");waitCatalog(a);
 // Invalidating rows also invalidates pending row identity.
 a.menu.selectedSave=2;invalidateSaveCatalog(a);need(a.menu.selectedSave==-1,"queued selection cannot address a replacement list");refreshSaveCatalog(a);waitCatalog(a);
 const auto safe=worldBytes(a);a.menuAction=vyruz::MenuAction::LoadSelected;need(!applyMenuAction(a)&&worldBytes(a)==safe,"late queued selection cannot load a different checkpoint");
 // Tooling Apps with no save path have an empty catalog, not endless loading.
 const auto base=a.savePath;a.savePath.clear();refreshSaveCatalog(a);waitCatalog(a);
 need(a.saveCatalog.empty()&&!a.menu.savedSessionsLoading,"empty save path finishes without a worker");a.savePath=base;refreshSaveCatalog(a);waitCatalog(a);
 a.menu.close(a.paused);a.sessionStarted=sessionStarted;
}
int main(int argc,char**argv){try{
 need(argc==3,"usage: save_browser_audit DATA OUTPUT");const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);std::ofstream report(out/"result.txt");need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0,SDL_GetError());
 const auto temp=std::filesystem::temp_directory_path()/("vyruz-save-browser-"+std::to_string(SDL_GetPerformanceCounter()));std::filesystem::create_directories(temp);
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
 using P=vyruz::MenuPage;using A=vyruz::MenuAction;
 for(int width:{640,1280}){int height=width==640?480:720;App a;a.window=SDL_CreateWindow("Saved sessions audit",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());a.savePath=(temp/std::to_string(width)/"slot1.vzs").string();a.options.playerName="Browser audit";a.scorePath=(temp/"scores").string();a.optionsPath=(temp/"options").string();need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);a.presentation.close();
#if VYRUZ_GLES
 a.gfx.setSwapEnabled(false);
#else
 glDrawBuffer(GL_FRONT_AND_BACK);glReadBuffer(GL_FRONT);
#endif
std::string error;const auto initial=a.world.snapshot();
 const auto setCheckpoint=[&](int n){auto s=initial;s.difficulty=n%4;s.elapsedMs=(3600+n*61)*1000;s.actors[s.player].vitals.money=10000+n;s.actors[s.player].vitals.energy=5000+n;s.actors[s.player].vitals.shield=6000+n;s.objects[s.player].position.x+=n*2;s.objects[s.player].position.y+=n;need(a.world.restoreSnapshot(s,error),error);};
 setCheckpoint(0);a.saveHistory=false;need(saveSession(a,0,error),error);const auto legacy=bytes(a.savePath);need(!legacy.empty(),"legacy save created");a.saveHistory=true;
 for(int n=1;n<=9;++n){setCheckpoint(n);render(a);need(saveSession(a,0,error),error);
#if VYRUZ_GLES
 need(a.gfx.presentedPreview().empty(),"save requests a fresh thumbnail");render(a);
#endif
 }need(bytes(a.savePath)==legacy,"numbered history preserves legacy slot bytes");refreshSaveCatalog(a,true);waitCatalog(a);need(a.saveCatalog.size()==8,"retains seven numbered saves plus legacy");need(std::count_if(a.saveCatalog.begin(),a.saveCatalog.end(),[](const auto&e){return e.legacy;})==1,"legacy remains browsable");
 for(const auto&e:a.saveCatalog){need(e.valid(),"saved row decodes");if(!e.legacy){auto preview=e.path;preview.replace_extension(".tga");vyruz::Image image;need(vyruz::loadTga(bytes(preview),image)&&image.width==224&&image.height==168,"native-sized saved preview");need(std::count_if(image.rgba.begin(),image.rgba.end(),[](uint8_t v){return v>24&&v<255;})>1000,"preview contains gameplay pixels");}}
 a.menu.show(a.paused,true);a.menuAction=A::LoadGame;need(!applyMenuAction(a)&&a.menu.page==P::SavedSessions,"Load opens browser without loading");waitCatalog(a);need(a.menu.entries()[a.menu.selected].label=="Back","browser defaults Back");

#if VYRUZ_GLES
 a.gfx.setSwapEnabled(false);
#else
 glDrawBuffer(GL_FRONT_AND_BACK);glReadBuffer(GL_FRONT);
#endif
SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
 const auto capture=[&](const std::string&name){render(a);glFinish();need(saveScreenshot((out/(std::to_string(width)+"-"+name+".ppm")).c_str(),width,height),"capture");};capture("back");
 const auto pixels=[&](){glFinish();need(glGetError()==GL_NO_ERROR,"browser render has no GL errors");
#if VYRUZ_GLES
 std::vector<uint8_t>p(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
#else
 std::vector<uint8_t>p(size_t(width)*height*3);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,p.data());
#endif
 need(glGetError()==GL_NO_ERROR,"browser pixel read has no GL errors");return p;};
 for(int row=0;row<2;++row){a.menu.selected=row+1;SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_UP;need(SDL_PushEvent(&e)==1,"push key");handleInput(a);need(a.menu.selected==row,"keyboard selection");render(a);auto keyboard=pixels();const auto help=a.menu.selectedHelp();need(help.find("Energy : "+std::to_string(int(a.saveCatalog[row].energy)))!=std::string::npos,"selected metadata uses checkpoint vitals");need(a.menu.saveLabels[row].starts_with(a.content.levelName),"original level name: expected "+a.content.levelName+" label "+a.menu.saveLabels[row]);a.menu.selected=row+1;auto v=a.gfx.viewport();e={};e.type=SDL_MOUSEMOTION;e.motion.x=int((a.menu.rowLeft()+15+v.uiX())*width/v.width);e.motion.y=int((a.menu.rowTop()-30+row*a.menu.rowHeight()+10+v.uiY())*height/v.height);need(SDL_PushEvent(&e)==1,"push mouse");handleInput(a);need(a.menu.selected==row&&a.menu.selectedHelp()==help,"hover metadata matches keyboard");
 // Mouse intentionally reveals the rocket pointer; compare identical page content.
 a.menuPointerVisible=false;render(a);need(pixels()==keyboard,"keyboard and hover render identical");capture("selected-"+std::to_string(row));}
 need(a.menu.saveHelp[0]!=a.menu.saveHelp[1]&&a.menu.saveLabels[0]!=a.menu.saveLabels[1],"checkpoint labels and metadata are distinct");
 const size_t chosen=2;const auto selectedEntry=a.saveCatalog[chosen];vyruz::SaveGame expected;need(vyruz::readSaveFile(selectedEntry.path,expected,error),error);const auto expectedWorld=expected.levels.at(expected.currentLevel).world;a.menu.selected=int(chosen);a.menuAction=a.menu.activate(a.paused);need(a.menuAction==A::LoadSelected&&a.menu.selectedSave==int(chosen),"selected older row requests exact save");need(applyMenuAction(a),"older session loads");need(a.world.player().vitals.money==selectedEntry.money&&a.world.snapshot().difficulty==selectedEntry.difficulty,"selected money and difficulty restored");const auto pos=a.content.objects[a.world.playerIndex()].position,epos=expectedWorld.objects[expectedWorld.player].position;need(pos.x==epos.x&&pos.y==epos.y,"selected old position restored");
 // In-game selection must default to Cancel and leave the complete world intact.
 a.menu.close(a.paused);a.menu.show(a.paused,false);a.menuAction=A::LoadGame;applyMenuAction(a);waitCatalog(a);const auto safe=worldBytes(a);a.menu.selected=0;a.menuAction=a.menu.activate(a.paused);need(a.menu.page==P::Confirm&&a.menu.selected==0&&a.menuAction==A::None,"in-game load asks with Cancel default");a.menuAction=a.menu.activate(a.paused);applyMenuAction(a);need(a.menu.page==P::SavedSessions&&worldBytes(a)==safe,"Cancel preserves live world");
 // A file may disappear or become corrupt after catalog capture.
 a.menu.close(a.paused);a.menu.show(a.paused,true);a.menuAction=A::LoadGame;applyMenuAction(a);waitCatalog(a);const auto doomed=a.saveCatalog[0].path;const auto original=bytes(doomed);std::filesystem::remove(doomed);a.menu.selected=0;a.menuAction=a.menu.activate(a.paused);need(!applyMenuAction(a)&&worldBytes(a)==safe&&!a.menu.error.empty(),"missing selected save preserves world and reports error");{std::ofstream bad(doomed,std::ios::binary);bad<<"broken save";}a.menuAction=A::LoadSelected;need(!applyMenuAction(a)&&worldBytes(a)==safe,"corrupt selected save preserves world");{std::ofstream restored(doomed,std::ios::binary);restored.write(reinterpret_cast<const char*>(original.data()),std::streamsize(original.size()));}need(bytes(a.savePath)==legacy,"legacy still unchanged after all browser operations");
 report<<"PASS "<<width<<"x"<<height<<": nine saves retain seven plus untouched legacy; Back default; original level names; keyboard/hover pixels and metadata; exact older checkpoint load; in-game Cancel; missing/corrupt transactional safety.\n";
 catalogConcurrency(a,out);
 a.gfx.deleteTexture(a.savePreviewTexture);a.savePreviewTexture=0;a.loading.clear(a.gfx);a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);
 }
 SDL_Quit();std::filesystem::remove_all(temp);report<<"Port integration captures using original resources, not native runtime screenshots.\n";return 0;
}catch(const std::exception&e){std::fprintf(stderr,"save browser audit: %s\n",e.what());return 1;}}
