#pragma once
#include <array>
#include <string_view>
namespace vyruz {
//434130: font3 annotations appended after the normal HUD/cursor pass.
//Text callback returns the final horizontal cursor. Coordinates remain native
//logical HUD pixels; caller supplies existing aspect-frame transform/font.
template<class Text,class Line>
void nativePanelHelpLayout(int width,int height,int language,Text&& text,Line&& line){
    using Point=std::array<int,2>;
    const int c=width>>1;const bool fr=language==2;
    const auto path=[&](Point a,Point b,Point d,Point e){line(std::array<Point,4>{a,b,d,e},4,250);};
    path({c-80,80},{c-95,80},{c-95,30},{c-80,30});
    text(fr?"ENERGIE":"ENERGY",c-72,80);
    path({c-80,100},{c-105,100},{c-105,20},{c-80,20});
    text(fr?"BOUCLIER":"SHIELD",c-72,100);
    path({c-80,120},{140,120},{140,28},{120,28});
    text(fr?"ARGENT":"MONEY",c-72,120);
    int end=text(fr?"MUNITIONS":"AMMUNITIONS",c+30,80);
    path({end+8,80},{width-140,80},{width-140,8},{width-110,8});
    end=text(fr?"VITESSE LENTE":"LOW SPEED",c+30,100);
    path({end+8,100},{width-130,100},{width-130,20},{width-25,20});
    end=text(fr?"VITESSE RAPIDE":"HIGH SPEED",c+30,120);
    path({end+8,120},{width-120,120},{width-120,35},{width-25,35});
    end=text(fr?"ARME COURANTE":"CURRENT WEAPON",c+30,140);
    path({end+8,140},{width-110,140},{width-110,55},{width-70,55});
    path({150,height-170},{130,height-170},{130,height-60},{90,height-60});
    text(fr?"RADAR (ROUGE: ENNEMI, JAUNE: CLEF/CODE, BLEU: ASCENSEUR)":"RADAR (RED: ENEMY, YELLOW: KEY/CODE, BLUE: LIFT)",158,height-170);
    end=text(fr?"INDICATEUR DE DANGER":"DANGER WARNING",c+30,height-130);
    line(std::array<Point,4>{{{end+8,height-130},{width-10,height-130},{width-10,height-55},{0,0}}},3,250);
    end=text(fr?"COORDONNEES (X/Y)":"COORDINATES (X/Y)",c-72,height-88);
    path({end+8,height-88},{width-150,height-88},{width-150,height-73},{width-120,height-73});
    end=text(fr?"ENNEMIS DETRUITS":"DESTROYED ENEMIES",c-72,height-68);
    path({end+8,height-68},{width-155,height-68},{width-155,height-63},{width-120,height-63});
    end=text(fr?"ENNEMIS RESTANTS":"REMAINING ENEMIES",c-72,height-48);
    path({end+8,height-48},{width-155,height-48},{width-155,height-53},{width-120,height-53});
    end=text(fr?"TEMPS ECOULE":"ELAPSED TIME",c-72,height-28);
    path({end+8,height-28},{width-150,height-28},{width-150,height-43},{width-120,height-43});
}
}
