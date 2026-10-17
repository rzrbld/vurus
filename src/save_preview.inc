// Native42b360 writes a224x168 TGA;427ef0 draws it at(8,150).
// Capture the last presented gameplay frame. Preview failure must never turn a
// successfully committed paid checkpoint into a reported save failure.
void captureSavePreview(App& a,std::filesystem::path path) {
    if(!a.window || a.menu.open())return;
    const int w=a.gfx.drawableWidth(),h=a.gfx.drawableHeight();if(w<=0||h<=0)return;
    GLint readBuffer=0,alignment=0;glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glReadBuffer(GL_FRONT);glPixelStorei(GL_PACK_ALIGNMENT,1);
    std::vector<uint8_t> pixels(size_t(w)*h*3);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    glReadBuffer(readBuffer);glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    const auto view=a.gfx.viewport();
    const int cw=std::clamp(int(w*std::min(640.f,view.width)/view.width),1,w);
    const int ch=std::clamp(int(h*std::min(480.f,view.height)/view.height),1,h);
    std::vector<uint8_t> tga(18+224*168*3);tga[2]=2;tga[12]=224;tga[14]=168;tga[16]=24;
    for(int y=0;y<168;++y)for(int x=0;x<224;++x) {
        const size_t source=(size_t(h-ch+y*ch/168)*w+x*cw/224)*3,destination=18+(size_t(y)*224+x)*3;
        tga[destination]=pixels[source+2];tga[destination+1]=pixels[source+1];tga[destination+2]=pixels[source];
    }
    path.replace_extension(".tga");
    if(FILE* file=std::fopen(path.c_str(),"wbx")) {
        const bool written=std::fwrite(tga.data(),1,tga.size(),file)==tga.size();
        const bool closed=std::fclose(file)==0;
        if(!written || !closed){std::error_code error;std::filesystem::remove(path,error);}
    }
}
void drawSavePreview(App& a) {
    if(a.menu.page!=vyruz::MenuPage::SavedSessions || a.menu.selected<0 || size_t(a.menu.selected)>=a.saveCatalog.size())return;
    auto path=a.saveCatalog[a.menu.selected].path;path.replace_extension(".tga");
    if(a.savePreviewPath!=path.string()) {
        a.gfx.deleteTexture(a.savePreviewTexture);a.savePreviewTexture=0;a.savePreviewPath=path.string();
        std::error_code error;
        if(std::filesystem::is_regular_file(std::filesystem::symlink_status(path,error)) && !error && std::filesystem::file_size(path,error)<=1024*1024 && !error) {
            std::ifstream input(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
            vyruz::Image image;
            if(vyruz::loadTga(bytes,image) && image.width==224 && image.height==168)
                a.savePreviewTexture=a.gfx.uploadTexture(image.rgba.data(),image.width,image.height);
        }
    }
    if(a.savePreviewTexture) {
        a.gfx.beginQuads(a.savePreviewTexture);
        a.gfx.quad(8,150,0,0,232,150,1,0,232,318,1,1,8,318,0,1);a.gfx.endQuads();
    }
}
