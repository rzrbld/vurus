#include "render/first_run_screen.h"
#define main discImportFixtureMain
#include "disc_import_test.cpp"
#undef main
#include <iostream>
int main(int argc,char** argv){
 try {
  if(argc!=2&&!(argc==3&&std::string(argv[2])=="--native"))throw std::runtime_error("usage: first_run_screen_audit OUTPUT [--native]");
  const bool native=argc==3;
  fs::path output=fs::absolute(argv[1]);fs::create_directories(output);
  if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
  int captures=0;
  for(auto size:{std::pair{640,480},std::pair{800,480},std::pair{1280,800}}){
   if(native&&size.first!=640)continue;
   auto* window=SDL_CreateWindow("first run audit",0,0,size.first,size.second,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
   auto* renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE):nullptr;
   if(!(SDL_GetWindowFlags(window)&SDL_WINDOW_RESIZABLE))throw std::runtime_error("window must resize");
   if(!renderer)throw std::runtime_error(SDL_GetError());
   SDL_RenderSetLogicalSize(renderer,640,480);vyruz::FirstRunScreen screen(renderer,"/games/Vurus/original-disc.cue","/games/Vurus/data");
   for(int stage=0;stage<7;++stage){
    vyruz::ImportProgress progress;std::string error;
    if(stage==1)progress={vyruz::ImportStage::Resources,"vz-data.rsc",42000000,100000000,42000000,750000000};
    if(stage==2)progress={vyruz::ImportStage::Music,"CD track 5 (4 of 7)",76000000,100000000,476000000,750000000};
    if(stage==3)error="No game data found. Put your own Vyruz ISO or BIN/CUE beside launch.sh, or use --data with a resource directory or disc image.";
    if(stage==5)error="Not enough free space for game files. Need 730 MiB in /games/Vurus/data. Free space, then Retry.";
    if(stage==6)error="Cannot write imported resource: /games/Vurus/data/vz-data.rsc. Permission denied. Check the destination and Retry.";
    screen.draw(progress,error,stage==4);
    const SDL_Rect bounds{0,0,size.first,size.second};
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,size.first,size.second,32,SDL_PIXELFORMAT_RGBA32);
    if(!surface||SDL_RenderReadPixels(renderer,&bounds,surface->format->format,surface->pixels,surface->pitch)!=0)throw std::runtime_error(SDL_GetError());
    const auto name=output/(std::to_string(size.first)+"-"+std::to_string(stage)+".bmp");
    if(SDL_SaveBMP(surface,name.c_str())!=0)throw std::runtime_error(SDL_GetError());
    SDL_FreeSurface(surface);++captures;
   }
   if(!native){
   SDL_SetWindowSize(window,size.first+100,size.second+100);SDL_PumpEvents();
   screen.draw({vyruz::ImportStage::Finishing,"Verifying track08.wav",80,100,980,1000});
   const SDL_Rect bounds{0,0,size.first+100,size.second+100};
   auto* resized=SDL_CreateRGBSurfaceWithFormat(0,size.first+100,size.second+100,32,SDL_PIXELFORMAT_RGBA32);
   if(!resized||SDL_RenderReadPixels(renderer,&bounds,resized->format->format,resized->pixels,resized->pitch)!=0)throw std::runtime_error(SDL_GetError());
   if(SDL_SaveBMP(resized,(output/(std::to_string(size.first)+"-resized.bmp")).c_str())!=0)throw std::runtime_error(SDL_GetError());
   SDL_FreeSurface(resized);++captures;
   }
   SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
  }
  SDL_Quit();
  // Actual worker/UI bootstrap with independently generated ISO/RSC fixture.
  auto fixture=output/"fixture";fs::create_directories(fixture);write(fixture/"disc.iso",iso(true));
  vyruz::DataLocation location;std::string error;
  if(!vyruz::prepareDataWithScreen((fixture/"disc.iso").string(),(fixture/"data").string(),location,error))throw std::runtime_error(error);
  vyruz::Vfs vfs;Bytes bytes;
  if(!location.imported||!vfs.init(location.directory)||!vfs.read("Intro.avi",bytes)||bytes!=Bytes({10,11,12,13,14}))throw std::runtime_error("SDL bootstrap did not publish exact fixture");
  if(!vyruz::prepareDataWithScreen(fixture.string(),(fixture/"data").string(),location,error)||location.imported)throw std::runtime_error("cached SDL bootstrap repeated import");
  std::cout<<"PASS "<<captures<<" first-run captures, real worker import and cached relaunch\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
