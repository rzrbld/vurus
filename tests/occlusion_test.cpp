#include "game/level.h"
#include "render/occlusion.h"
#include <cstdlib>
#include <iostream>

static void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    {
        vyruz::Level marker;marker.mapW=2;marker.mapH=1;
        marker.layers.assign(5,std::vector<uint32_t>(2));
        marker.tiles.resize(3);marker.blockFlags={1,2,UINT32_MAX};
        marker.tileDrawFlags={0,0,0};
        check(!vyruz::coveredPlayerCenter(marker,32,16),"open cell needs no marker");
        marker.layers[2][0]=1;
        check(!vyruz::coveredPlayerCenter(marker,32,16),"phase1 does not trigger marker");
        marker.layers[2][0]=3;
        check(!vyruz::coveredPlayerCenter(marker,32,16),"signed negative phase does not trigger marker");
        marker.layers[2][0]=0;marker.layers[4][0]=2;
        check(vyruz::coveredPlayerCenter(marker,32,16),"marker scans past empty intermediate layers");
        check(!vyruz::coveredPlayerCenter(marker,64,16),"marker tests centre cell, not nearby tiles");
        marker.tileDrawFlags[1]=1;marker.layers[0][0]=1;
        check(!vyruz::coveredPlayerCenter(marker,32,16),"keyed foreground over base does not trigger native marker");
        marker.layers[0][0]=0;
        check(vyruz::coveredPlayerCenter(marker,32,16),"keyed foreground without base triggers marker");
        check(!vyruz::coveredPlayerCenter(marker,-1,16)&&!vyruz::coveredPlayerCenter(marker,128,16),"outside map has no marker");
    }
    vyruz::Level level;
    level.mapW = 1; level.mapH = 8;
    level.layers.assign(6, std::vector<uint32_t>(8));
    level.tiles.resize(6);
    level.blockFlags = {UINT32_MAX, 0, 1, 2, 3, 4};
    // A face drawn two rows below its bitmap row covers a body behind it.
    level.layers[2][2] = 4;
    auto select = [&](float y) { return vyruz::foregroundTiles(level, y, 0, 64, 64, 96); };
    check(select(96).size() == 1, "phase 2 wall should cover body in row 3");
    check(select(127.99f).size() == 1, "coverage must persist up to row boundary");
    check(select(128).empty(), "body at row 4 must follow the row 4 tile pass");
    level.terrain.assign(8, vyruz::TerrainRoof | vyruz::TerrainGround);
    check(select(96).size() == 1, "passable roof metadata must not erase baked coverage");
    level.layers[2][2] = 1;
    check(select(0).empty(), "signed -1 descriptor must never be redrawn");
    level.layers[3][2] = 6;
    check(select(160).size() == 1, "phase 4 must redraw; signed -1 must not stop scan");
    level.layers[2][2] = 0;
    check(select(0).empty(), "empty foreground layer must terminate cell scan");
    level.layers[2][2] = 999;
    check(select(0).empty(), "invalid bitmap reference must be bounded");
    level.layers[2][2] = 3;
    level.layers[3][2] = 4;
    level.layers[2][3] = 2;
    const auto ordered = vyruz::foregroundTiles(level, 0, 0, 64, 64, 128);
    check(ordered.size() == 3 && ordered[0].y == 2 && ordered[0].phase == 1 &&
          ordered[1].y == 3 && ordered[1].phase == 0 && ordered[2].phase == 2,
          "redraws must follow row step then descending phase");
    check(vyruz::foregroundTiles(level, 0, 64, 64, 96, 96).empty(),
          "half-open bounds must exclude adjacent cell");
    check(vyruz::foregroundTiles(level, 0, 0, 96, 64, 96).empty(),
          "empty sprite rectangle must produce no redraws");
    check(vyruz::foregroundTiles(level,0,0,48,64,64).empty(),
          "row2 starts at y64, not the former half-tile-shifted y48");
    check(!vyruz::foregroundTiles(level,0,32,64,64,96).empty(),
          "right half of raster tile0 still belongs to tile0");
    level.layers.assign(3,std::vector<uint32_t>(8));
    level.layers[2][2]=4; // row2, phase2: redraw at row4
    std::vector<vyruz::SpriteBounds> sprites{{96*4,12,66,52,91},{128*4,12,66,52,91}};
    auto queue=vyruz::composeObjects(level,sprites,0,0,64,256);
    check(queue.size()==3 && queue[0].isSprite && queue[0].sprite==0 &&
          !queue[1].isSprite && queue[2].isSprite && queue[2].sprite==1,
          "wall must separate two overlapping actors at strict row boundary");
    sprites={{96*4,12,66,52,91},{96*4+1,12,66,52,91},{224*4+3,12,66,52,91}};
    queue=vyruz::composeObjects(level,sprites,0,0,64,256);
    check(queue.size()==4 && queue[0].sprite==2 && queue[1].sprite==1 && queue[2].sprite==0,
          "in-ground precedes row sweep; shadow precedes body");
    sprites={{96*4,64,66,96,91}};
    check(vyruz::composeObjects(level,sprites,0,0,64,256).empty(),
          "culled sprite must not mark foreground dirty");
}
