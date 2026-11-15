// written by bastiaan konings schuiling 2008 - 2015
// Broadcast HUD Scoreboard & Match Clock (PES / eFootball style)

#include "scoreboard.hpp"

#include "utils/gui2/windowmanager.hpp"
#include "base/geometry/triangle.hpp"
#include "base/geometry/line.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

#include "../../onthepitch/match.hpp"

using namespace blunted;

static void FillSlantedQuad(blunted::intrusive_ptr<Image2D> &img,
                            float xTopL, float xTopR, float xBotL, float xBotR,
                            float yTop, float yBot,
                            const Vector3 &color, int alpha) {
  Triangle t1;
  t1.SetVertex(0, Vector3(xTopL, yTop, 0));
  t1.SetVertex(1, Vector3(xTopR, yTop, 0));
  t1.SetVertex(2, Vector3(xBotR, yBot, 0));
  img->DrawTriangle(t1, color, alpha);

  Triangle t2;
  t2.SetVertex(0, Vector3(xTopL, yTop, 0));
  t2.SetVertex(1, Vector3(xBotR, yBot, 0));
  t2.SetVertex(2, Vector3(xBotL, yBot, 0));
  img->DrawTriangle(t2, color, alpha);
}

Gui2ScoreBoard::Gui2ScoreBoard(Gui2WindowManager *windowManager, Match *match)
    : Gui2View(windowManager, "scoreboard", 2.4f, 2.5f, 25.4f, 3.55f),
      match(match),
      bg(0),
      scoreCaption(0),
      timeCaption(0),
      tvLogo(0) {

  goalCount[0] = 0;
  goalCount[1] = 0;
  timeStr = "0:00";

  const float totalW = width_percent;
  const float barH = height_percent;
  const float fontH = barH * 0.68f;
  textTopPct = (barH - fontH) * 0.5f - 0.04f;

  // Proportional broadcast cells: [Accent | RMA (28%) | 1 - 0 (20%) | FCB (28%) | Accent] [/ 7:02 (24%) /]
  team1CellCenterPct = totalW * 0.160f;
  scoreCellCenterPct = totalW * 0.395f;
  team2CellCenterPct = totalW * 0.625f;
  clockCellCenterPct = totalW * 0.875f;

  bg = new Gui2Image(windowManager, "image_scoreboard_broadcast_bg", 0, 0, totalW, barH);
  this->AddView(bg);
  DrawBroadcastBackground();
  bg->Show();

  Vector3 whiteText(246, 249, 255);
  Vector3 navyOutline(10, 15, 26);
  Vector3 clockText(22, 30, 44);
  Vector3 clockOutline(218, 224, 234);

  std::string short0 = match->GetTeam(0)->GetTeamData()->GetShortName();
  std::string short1 = match->GetTeam(1)->GetTeamData()->GetShortName();
  if (short0.empty()) short0 = "HOM";
  if (short1.empty()) short1 = "AWY";

  teamNameCaption[0] = new Gui2Caption(windowManager, "game_scoreboard_team1name", 0, textTopPct, totalW * 0.25f, fontH, short0);
  teamNameCaption[0]->SetColor(whiteText);
  teamNameCaption[0]->SetOutlineColor(navyOutline);
  this->AddView(teamNameCaption[0]);
  teamNameCaption[0]->SetPosition(team1CellCenterPct - teamNameCaption[0]->GetTextWidthPercent() * 0.5f, textTopPct);
  teamNameCaption[0]->Show();

  scoreCaption = new Gui2Caption(windowManager, "game_scoreboard_score", 0, textTopPct, totalW * 0.20f, fontH, "0 - 0");
  scoreCaption->SetColor(Vector3(255, 255, 255));
  scoreCaption->SetOutlineColor(Vector3(6, 10, 18));
  this->AddView(scoreCaption);
  UpdateScoreCaptionLayout();
  scoreCaption->Show();

  teamNameCaption[1] = new Gui2Caption(windowManager, "game_scoreboard_team2name", 0, textTopPct, totalW * 0.25f, fontH, short1);
  teamNameCaption[1]->SetColor(whiteText);
  teamNameCaption[1]->SetOutlineColor(navyOutline);
  this->AddView(teamNameCaption[1]);
  teamNameCaption[1]->SetPosition(team2CellCenterPct - teamNameCaption[1]->GetTextWidthPercent() * 0.5f, textTopPct);
  teamNameCaption[1]->Show();

  timeCaption = new Gui2Caption(windowManager, "game_scoreboard_timecaption", 0, textTopPct, totalW * 0.24f, fontH, "0:00");
  timeCaption->SetColor(clockText);
  timeCaption->SetOutlineColor(clockOutline);
  this->AddView(timeCaption);
  UpdateTimeCaptionLayout();
  timeCaption->Show();

  this->Show();
}

Gui2ScoreBoard::~Gui2ScoreBoard() {
}

void Gui2ScoreBoard::DrawBroadcastBackground() {
  if (!bg) return;
  int px = 0, py = 0, pw = 0, ph = 0;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, px, py, pw, ph);
  if (pw <= 8 || ph <= 6) return;

  blunted::intrusive_ptr<Image2D> canvas = bg->GetImage2D();
  canvas->DrawRectangle(0, 0, pw, ph, Vector3(0, 0, 0), 0);

  const float H = (float)ph;
  const float W = (float)pw;
  const float slant = std::max(6.0f, std::round(H * 0.28f)); // PES-style forward '/' slant (top shifted right relative to bottom)
  const float stripW = std::max(5.0f, std::round(W * 0.016f));

  Vector3 colTeam0 = match->GetTeam(0)->GetTeamData()->GetColor1();
  Vector3 colTeam1 = match->GetTeam(1)->GetTeamData()->GetColor1();
  Vector3 navyPlate(24, 36, 58);
  Vector3 darkScoreBox(11, 17, 29);
  Vector3 silverClockBox(224, 230, 238);
  Vector3 topHighlight(85, 118, 168);

  float xMainTopL = slant;
  float xMainBotL = 0.0f;

  float xScoreTopL = std::round(W * 0.305f) + slant * 0.5f;
  float xScoreBotL = xScoreTopL - slant;
  float xScoreTopR = std::round(W * 0.495f) + slant * 0.5f;
  float xScoreBotR = xScoreTopR - slant;

  float xMainTopR = std::round(W * 0.745f) + slant * 0.5f;
  float xMainBotR = xMainTopR - slant;

  float xClockTopL = xMainTopR + std::max(2.0f, std::round(W * 0.005f));
  float xClockBotL = xClockTopL - slant;
  float xClockTopR = W;
  float xClockBotR = W - slant;

  // 1. Main navy broadcast bar (Home + Score + Away)
  FillSlantedQuad(canvas, xMainTopL, xMainTopR, xMainBotL, xMainBotR, 0.0f, H, navyPlate, 232);

  // 2. Center high-contrast Score trapezoid ('1 - 0')
  FillSlantedQuad(canvas, xScoreTopL, xScoreTopR, xScoreBotL, xScoreBotR, 0.0f, H, darkScoreBox, 246);

  // 3. Team 0 (Left) & Team 1 (Right) kit-color broadcast tabs
  FillSlantedQuad(canvas, xMainTopL, xMainTopL + stripW, xMainBotL, xMainBotL + stripW, 0.0f, H, colTeam0, 248);
  FillSlantedQuad(canvas, xMainTopR - stripW, xMainTopR, xMainBotR - stripW, xMainBotR, 0.0f, H, colTeam1, 248);

  // 4. Subtle top edge sheen on main bar
  Line topEdge;
  topEdge.SetVertex(0, Vector3(xMainTopL + stripW, 1.0f, 0));
  topEdge.SetVertex(1, Vector3(xMainTopR - stripW, 1.0f, 0));
  canvas->DrawLine(topEdge, topHighlight, 145);

  // 5. Brushed silver-white Clock trapezoid ('7:02')
  FillSlantedQuad(canvas, xClockTopL, xClockTopR, xClockBotL, xClockBotR, 0.0f, H, silverClockBox, 242);

  Line clockTopSheen;
  clockTopSheen.SetVertex(0, Vector3(xClockTopL + 1.0f, 1.0f, 0));
  clockTopSheen.SetVertex(1, Vector3(xClockTopR - 1.0f, 1.0f, 0));
  canvas->DrawLine(clockTopSheen, Vector3(250, 252, 255), 210);

  canvas->OnChange();
}

void Gui2ScoreBoard::UpdateScoreCaptionLayout() {
  if (!scoreCaption) return;
  std::string scoreText = int_to_str(goalCount[0]) + "-" + int_to_str(goalCount[1]);
  scoreCaption->SetCaption(scoreText);
  scoreCaption->SetPosition(scoreCellCenterPct - scoreCaption->GetTextWidthPercent() * 0.5f, textTopPct);
}

void Gui2ScoreBoard::UpdateTimeCaptionLayout() {
  if (!timeCaption) return;
  timeCaption->SetCaption(timeStr);
  timeCaption->SetPosition(clockCellCenterPct - timeCaption->GetTextWidthPercent() * 0.5f, textTopPct);
}

void Gui2ScoreBoard::GetImages(std::vector < blunted::intrusive_ptr<Image2D> > &target) {
  Gui2View::GetImages(target);
}

void Gui2ScoreBoard::Redraw() {
  DrawBroadcastBackground();
}

void Gui2ScoreBoard::SetTimeStr(const std::string &newTimeStr) {
  if (this->timeStr != newTimeStr) {
    this->timeStr = newTimeStr;
    UpdateTimeCaptionLayout();
  }
}

void Gui2ScoreBoard::SetGoalCount(int teamID, int newGoalCount) {
  if (teamID < 0 || teamID > 1) return;
  if (this->goalCount[teamID] != newGoalCount) {
    this->goalCount[teamID] = newGoalCount;
    UpdateScoreCaptionLayout();
  }
}
