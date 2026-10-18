// Native427120/42b710 mode2 selects a separate per-profile default session.
// Gameplay captures the entry state; startup scenery and demos must not replace it.
void persistRetrySession(App& a) {
    if(!a.retryPersistence || a.demoActive || !a.restartLevel || a.restartPath=="CAVE/DEMO.3DG")return;
    vyruz::SaveGame save;save.currentLevel=a.restartPath;save.levels=a.visited;
    save.levels[save.currentLevel]=*a.restartLevel;
    std::string error;
    if(!vyruz::writeDefaultRetry(a.savePath,a.options.storageProfile(),save,error))
        std::fprintf(stderr,"Could not store default retry session: %s (in-memory retry retained)\n",error.c_str());
    a.retryDiskPath.clear();a.retryCheckedProfile=a.options.storageProfile();
}
void recoverRetrySession(App& a) {
    if(a.savePath.empty())return;
    const auto& profile=a.options.storageProfile();
    if(a.restartLevel && a.restartProfile==profile){a.retryCheckedProfile=profile;return;}
    if(a.retryCheckedProfile==profile)return;
    a.retryCheckedProfile=profile;a.restartLevel.reset();a.restartPath.clear();a.restartProfile.clear();a.retryDiskPath.clear();
    vyruz::SaveGame saved;std::string error;
    if(!vyruz::readDefaultRetry(a.savePath,profile,saved,error))return;
    auto current=saved.levels.find(saved.currentLevel);
    if(current==saved.levels.end())return;
    const auto& world=current->second.world;
    if(world.player>=world.actors.size() || !world.actors[world.player].alive || world.transition)return;
    a.restartPath=saved.currentLevel;a.restartProfile=profile;
    a.restartLevel=std::move(current->second);
    a.retryDiskPath=vyruz::defaultRetryPath(a.savePath,profile).string();
}
std::string retryLevelTitle(App& a) {
    if(a.restartPath==levelKey(a.content.mapPath))return a.content.levelName;
    if(a.retryTitlePath==a.restartPath)return a.retryTitle;
    a.retryTitlePath=a.restartPath;a.retryTitle=a.restartPath;
    auto path=a.restartPath;const auto dot=path.rfind('.');if(dot!=std::string::npos)path.replace(dot,std::string::npos,".3DO");
    std::vector<uint8_t> bytes;vyruz::TextDocument doc;std::string error;
    if(a.vfs.read(path,bytes) && vyruz::parseContentText(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),path,doc,error))
        if(const auto* title=vyruz::contentProperty(doc.nodes,"level-name")) {
            if(!title->values.empty()) {
                a.retryTitle.clear();for(const auto& word:title->values){if(!a.retryTitle.empty())a.retryTitle+=' ';a.retryTitle+=word;}
            } else if(title->children.size()==1 && title->children.front().kind==vyruz::TextKind::Bare) {
                a.retryTitle=title->children.front().key;for(const auto& word:title->children.front().arguments)a.retryTitle+=' '+word;
            }
        }
    return a.retryTitle;
}
