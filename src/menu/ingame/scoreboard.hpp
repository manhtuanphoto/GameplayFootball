// written by bastiaan konings schuiling 2008 - 2015
// Broadcast HUD Scoreboard & Match Clock (PES / eFootball style)

#ifndef _HPP_GUI2_VIEW_SCOREBOARD
#define _HPP_GUI2_VIEW_SCOREBOARD

#include "utils/gui2/view.hpp"

#include "scene/objects/image2d.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;

class Gui2ScoreBoard : public Gui2View {

  public:
    Gui2ScoreBoard(Gui2WindowManager *windowManager, Match *match);
    virtual ~Gui2ScoreBoard();

    void GetImages(std::vector < blunted::intrusive_ptr<Image2D> > &target);

    virtual void Redraw();

    void SetTimeStr(const std::string &timeStr);
    void SetGoalCount(int teamID, int goalCount);

  protected:
    void DrawBroadcastBackground();
    void UpdateScoreCaptionLayout();
    void UpdateTimeCaptionLayout();

    Match *match;

    std::string timeStr;
    int goalCount[2];

    float team1CellCenterPct;
    float scoreCellCenterPct;
    float team2CellCenterPct;
    float clockCellCenterPct;
    float textTopPct;

    Gui2Image *bg;
    Gui2Caption *teamNameCaption[2];
    Gui2Caption *scoreCaption;
    Gui2Caption *timeCaption;
    Gui2Image *tvLogo;
};

#endif
