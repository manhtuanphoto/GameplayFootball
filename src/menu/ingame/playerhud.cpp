// written by bastiaan konings schuiling 2008 - 2015
// Broadcast Bottom-Corner Player Nameplates & Stamina Bars (PES / eFootball style)

#include "playerhud.hpp"

#include "utils/gui2/windowmanager.hpp"
#include "base/geometry/triangle.hpp"
#include "base/geometry/line.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "../../onthepitch/player/player.hpp"
#include "../../onthepitch/AIsupport/ai_adaptive_pipeline.hpp"

using namespace blunted;

static void FillPlateQuad(blunted::intrusive_ptr<Image2D> &img,
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

Gui2PlayerHUD::Gui2PlayerHUD(Gui2WindowManager *windowManager, Match *match)
    : Gui2View(windowManager, "player_broadcast_hud", 0.0f, 0.0f, 100.0f, 100.0f),
      match(match) {

  cardWidthPct = 21.0f;
  cardHeightPct = 4.45f;
  leftCardXPct = 4.2f;
  rightCardXPct = 95.8f - cardWidthPct;
  cardYPct = 90.8f;
  crestWidthPct = cardHeightPct / windowManager->GetAspectRatio();

  for (int s = 0; s < 2; s++) {
    float cx = (s == 0) ? leftCardXPct : rightCardXPct;
    plateImg[s] = new Gui2Image(windowManager, "game_playerhud_plate_" + int_to_str(s), cx, cardYPct, cardWidthPct, cardHeightPct);
    this->AddView(plateImg[s]);
    plateImg[s]->Show();

    nameCaption[s] = new Gui2Caption(windowManager, "game_playerhud_name_" + int_to_str(s), cx, cardYPct + 1.55f, cardWidthPct * 0.82f, 2.15f, " ");
    nameCaption[s]->SetColor(Vector3(244, 247, 252));
    nameCaption[s]->SetOutlineColor(Vector3(8, 11, 16));
    this->AddView(nameCaption[s]);
    nameCaption[s]->Show();

    cachedTeamID[s] = -1;
    cachedPlayerID[s] = -1;
    cachedStaminaPct[s] = -1;
  }

  for (int t = 0; t < 2; t++) {
    teamCrestImg[t] = new Gui2Image(windowManager, "game_playerhud_crest_team_" + int_to_str(t), 0.0f, cardYPct, crestWidthPct, cardHeightPct);
    this->AddView(teamCrestImg[t]);

    std::string logoUrl = match->GetTeam(t)->GetTeamData()->GetLogoUrl();
    std::error_code ec;
    if (!logoUrl.empty() && std::filesystem::exists(logoUrl, ec)) {
      teamCrestImg[t]->LoadImage(logoUrl);
    } else {
      DrawFallbackCrest(teamCrestImg[t], match->GetTeam(t)->GetTeamData()->GetColor1(), match->GetTeam(t)->GetTeamData()->GetColor2());
    }
    teamCrestImg[t]->Show();
  }

  // Initial layout before first snapshot
  RedrawSidePlate(0, 0, 100);
  RedrawSidePlate(1, 1, 100);
  teamCrestImg[0]->SetPosition(leftCardXPct, cardYPct);
  teamCrestImg[1]->SetPosition(rightCardXPct + cardWidthPct - crestWidthPct, cardYPct);

  // [F7] Live Vision Cone Telemetry Card (sits cleanly below top-left Scoreboard when [F7] is ON)
  visionCardBg = new Gui2Image(windowManager, "game_vision_telemetry_bg", 2.4f, 6.65f, 38.5f, 4.15f);
  this->AddView(visionCardBg);
  cachedVisionAccent = Vector3(95, 210, 255);
  RedrawVisionCardBg(cachedVisionAccent);
  visionCardBg->Hide();

  visionLine1Cap = new Gui2Caption(windowManager, "game_vision_telemetry_l1", 3.15f, 6.92f, 37.0f, 1.58f, " ");
  visionLine1Cap->SetColor(Vector3(245, 250, 255));
  visionLine1Cap->SetOutlineColor(Vector3(8, 12, 18));
  this->AddView(visionLine1Cap);
  visionLine1Cap->Hide();

  visionLine2Cap = new Gui2Caption(windowManager, "game_vision_telemetry_l2", 3.15f, 8.75f, 37.0f, 1.52f, " ");
  visionLine2Cap->SetColor(Vector3(165, 232, 255));
  visionLine2Cap->SetOutlineColor(Vector3(8, 12, 18));
  this->AddView(visionLine2Cap);
  visionLine2Cap->Hide();

  cachedVisionVisible = false;

  this->Show();
}

Gui2PlayerHUD::~Gui2PlayerHUD() {
}

void Gui2PlayerHUD::DrawFallbackCrest(Gui2Image *crestImg, const Vector3 &col1, const Vector3 &col2) {
  int px = 0, py = 0, pw = 0, ph = 0;
  float xPct = 0, yPct = 0, wPct = 0, hPct = 0;
  crestImg->GetPosition(xPct, yPct);
  crestImg->GetSize(wPct, hPct);
  windowManager->GetCoordinates(xPct, yPct, wPct, hPct, px, py, pw, ph);
  if (pw <= 4 || ph <= 4) return;

  blunted::intrusive_ptr<Image2D> canvas = crestImg->GetImage2D();
  canvas->DrawRectangle(0, 0, pw, ph, Vector3(0, 0, 0), 0);

  float cx = pw * 0.5f;
  float topY = ph * 0.08f;
  float midY = ph * 0.62f;
  float botY = ph * 0.94f;
  float leftX = pw * 0.12f;
  float rightX = pw * 0.88f;

  // Classic two-tone football shield badge
  FillPlateQuad(canvas, leftX, cx, leftX + pw * 0.06f, cx, topY, midY, col1, 245);
  FillPlateQuad(canvas, cx, rightX, cx, rightX - pw * 0.06f, topY, midY, col2, 245);

  Triangle botLeft;
  botLeft.SetVertex(0, Vector3(leftX + pw * 0.06f, midY, 0));
  botLeft.SetVertex(1, Vector3(cx, midY, 0));
  botLeft.SetVertex(2, Vector3(cx, botY, 0));
  canvas->DrawTriangle(botLeft, col1, 245);

  Triangle botRight;
  botRight.SetVertex(0, Vector3(cx, midY, 0));
  botRight.SetVertex(1, Vector3(rightX - pw * 0.06f, midY, 0));
  botRight.SetVertex(2, Vector3(cx, botY, 0));
  canvas->DrawTriangle(botRight, col2, 245);

  canvas->OnChange();
}

Player *Gui2PlayerHUD::ResolveDisplayedPlayer(Team *team) const {
  if (!team) return nullptr;

  if (team->GetHumanGamerCount() > 0) {
    std::vector<Player*> active;
    team->GetActivePlayers(active);
    // Prefer human-controlled player who currently has possession, else first human-controlled player
    Player *firstHuman = nullptr;
    for (size_t i = 0; i < active.size(); i++) {
      if (team->IsHumanControlled(active[i]->GetID())) {
        if (active[i]->HasPossession()) return active[i];
        if (!firstHuman) firstHuman = active[i];
      }
    }
    if (firstHuman) return firstHuman;
  }

  if (match->IsInSetPiece() && match->GetRefereeBuffer().taker &&
      match->GetRefereeBuffer().taker->IsActive() &&
      match->GetRefereeBuffer().taker->GetTeamID() == team->GetID()) {
    return match->GetRefereeBuffer().taker;
  }

  Player *desig = team->GetDesignatedTeamPossessionPlayer();
  if (desig && desig->IsActive()) return desig;

  std::vector<Player*> active;
  team->GetActivePlayers(active);
  return active.empty() ? nullptr : active[0];
}

int Gui2PlayerHUD::ComputeSquadNumber(Team *team, Player *player) const {
  if (!team || !player) return 9;
  const std::vector<Player*> &all = team->GetAllPlayers();
  int lineupIndex = 0;
  for (size_t i = 0; i < all.size(); i++) {
    if (all[i] == player) {
      lineupIndex = (int)i;
      break;
    }
  }
  if (lineupIndex >= 11) return lineupIndex + 1;

  // Authentic role-aware starting XI numbering when unique, otherwise clean 1..11 lineup number
  e_PlayerRole role = player->GetFormationEntry().role;
  if (role == e_PlayerRole_GK) return 1;

  static const int classicMap[11] = { 1, 2, 4, 5, 3, 6, 8, 7, 10, 11, 9 };
  if (role == e_PlayerRole_CF && lineupIndex >= 8) {
    return (lineupIndex == 10) ? 9 : ((lineupIndex == 9) ? 11 : 10);
  }
  return classicMap[lineupIndex];
}

std::string Gui2PlayerHUD::FormatBroadcastPlayerName(Player *player, int squadNum, bool isRightSide) const {
  if (!player) return " ";
  std::string first = player->GetPlayerData()->GetFirstName();
  std::string last = player->GetPlayerData()->GetLastName();

  // Trim leading spaces on first name to extract clean initial ('K. BENZEMA')
  std::string initialPrefix = "";
  for (size_t i = 0; i < first.size(); i++) {
    unsigned char ch = static_cast<unsigned char>(first[i]);
    if (!std::isspace(ch)) {
      // Only use ASCII single-char initial cleanly; for UTF-8 multi-byte leading char, skip initial to avoid broken bytes
      if (ch < 128) {
        initialPrefix = std::string(1, static_cast<char>(std::toupper(ch))) + ". ";
      }
      break;
    }
  }

  std::string fullShortName = initialPrefix + last;
  if (fullShortName.size() > 16) {
    fullShortName = last.substr(0, 16);
  }

  if (!isRightSide) {
    return int_to_str(squadNum) + "  " + fullShortName;
  } else {
    return fullShortName + "  " + int_to_str(squadNum);
  }
}

void Gui2PlayerHUD::RedrawSidePlate(int slot, int teamID, int staminaPct) {
  if (slot < 0 || slot > 1 || !plateImg[slot]) return;

  float cx = (slot == 0) ? leftCardXPct : rightCardXPct;
  int px = 0, py = 0, pw = 0, ph = 0;
  windowManager->GetCoordinates(cx, cardYPct, cardWidthPct, cardHeightPct, px, py, pw, ph);
  if (pw <= 12 || ph <= 8) return;

  int crestPxW = std::max(8, int(std::round(pw * (crestWidthPct / cardWidthPct))));
  float W = (float)pw;
  float H = (float)ph;
  float plateTopY = std::round(H * 0.14f);
  float plateBotY = std::round(H * 0.92f);
  float slant = std::max(8.0f, std::round((plateBotY - plateTopY) * 0.34f));

  blunted::intrusive_ptr<Image2D> canvas = plateImg[slot]->GetImage2D();
  canvas->DrawRectangle(0, 0, pw, ph, Vector3(0, 0, 0), 0);

  Vector3 darkPlate(16, 20, 24);
  Vector3 borderHighlight(62, 70, 80);
  Vector3 teamAccent = (teamID >= 0 && teamID <= 1) ? match->GetTeam(teamID)->GetTeamData()->GetColor1() : Vector3(90, 160, 255);

  float barTrackX0 = 0.0f;
  float barTrackX1 = 0.0f;

  if (slot == 0) {
    // Bottom-Left plate: starts behind left crest, slanted '/' on right outer tip
    float xTopL = crestPxW * 0.52f;
    float xBotL = crestPxW * 0.52f;
    float xTopR = W;
    float xBotR = W - slant;
    FillPlateQuad(canvas, xTopL, xTopR, xBotL, xBotR, plateTopY, plateBotY, darkPlate, 218);

    Line botAccent;
    botAccent.SetVertex(0, Vector3(xBotL, plateBotY - 1.0f, 0));
    botAccent.SetVertex(1, Vector3(xBotR, plateBotY - 1.0f, 0));
    canvas->DrawLine(botAccent, teamAccent * 0.75f + borderHighlight * 0.25f, 185);

    barTrackX0 = (float)crestPxW + std::max(6.0f, std::round(W * 0.025f));
    barTrackX1 = xTopR - slant - std::max(8.0f, std::round(W * 0.035f));
  } else {
    // Bottom-Right plate: slanted '\' on left outer tip, extends behind right crest
    float xTopL = 0.0f;
    float xBotL = slant;
    float xTopR = W - crestPxW * 0.52f;
    float xBotR = W - crestPxW * 0.52f;
    FillPlateQuad(canvas, xTopL, xTopR, xBotL, xBotR, plateTopY, plateBotY, darkPlate, 218);

    Line botAccent;
    botAccent.SetVertex(0, Vector3(xBotL, plateBotY - 1.0f, 0));
    botAccent.SetVertex(1, Vector3(xBotR, plateBotY - 1.0f, 0));
    canvas->DrawLine(botAccent, teamAccent * 0.75f + borderHighlight * 0.25f, 185);

    barTrackX0 = slant + std::max(8.0f, std::round(W * 0.035f));
    barTrackX1 = (W - (float)crestPxW) - std::max(6.0f, std::round(W * 0.025f));
  }

  // Recessed Stamina Gauge Strip along top of dark plate
  int trackX = (int)std::round(barTrackX0);
  int trackW = std::max(10, (int)std::round(barTrackX1 - barTrackX0));
  int trackY = (int)std::round(plateTopY + std::max(3.0f, H * 0.07f));
  int trackH = std::max(4, (int)std::round(H * 0.115f));

  cachedBarTrackX[slot] = trackX;
  cachedBarTrackY[slot] = trackY;
  cachedBarTrackW[slot] = trackW;
  cachedBarTrackH[slot] = trackH;

  canvas->DrawRectangle(trackX - 1, trackY - 1, trackW + 2, trackH + 2, Vector3(6, 8, 10), 240);
  UpdateSidePlateStaminaOnly(slot, staminaPct);
}

void Gui2PlayerHUD::UpdateSidePlateStaminaOnly(int slot, int staminaPct) {
  if (slot < 0 || slot > 1 || !plateImg[slot] || cachedBarTrackW[slot] <= 0) return;

  const int trackX = cachedBarTrackX[slot];
  const int trackY = cachedBarTrackY[slot];
  const int trackW = cachedBarTrackW[slot];
  const int trackH = cachedBarTrackH[slot];

  blunted::intrusive_ptr<Image2D> canvas = plateImg[slot]->GetImage2D();
  canvas->DrawRectangle(trackX, trackY, trackW, trackH, Vector3(28, 34, 38), 235);

  float stamNorm = clamp((float)staminaPct / 100.0f, 0.0f, 1.0f);
  int fillW = (int)std::round(trackW * stamNorm);
  if (fillW > 0) {
    Vector3 stamCol(82, 226, 62); // PES vibrant green
    if (stamNorm < 0.28f) {
      stamCol = Vector3(235, 62, 52); // Exhausted red
    } else if (stamNorm < 0.55f) {
      stamCol = Vector3(245, 190, 44); // Tired amber
    }
    canvas->DrawRectangle(trackX, trackY, fillW, trackH, stamCol, 248);
    if (trackH >= 4) {
      Vector3 glossCol(
          std::min(255.0f, stamCol.coords[0] + 45.0f),
          std::min(255.0f, stamCol.coords[1] + 25.0f),
          std::min(255.0f, stamCol.coords[2] + 45.0f));
      canvas->DrawRectangle(trackX, trackY, fillW, 1, glossCol, 215);
    }
  }

  canvas->OnChange();
}

void Gui2PlayerHUD::RedrawVisionCardBg(const Vector3 &accentColor) {
  if (!visionCardBg) return;
  int px = 0, py = 0, pw = 0, ph = 0;
  float xPct = 0, yPct = 0, wPct = 0, hPct = 0;
  visionCardBg->GetPosition(xPct, yPct);
  visionCardBg->GetSize(wPct, hPct);
  windowManager->GetCoordinates(xPct, yPct, wPct, hPct, px, py, pw, ph);
  if (pw <= 8 || ph <= 6) return;

  blunted::intrusive_ptr<Image2D> canvas = visionCardBg->GetImage2D();
  canvas->DrawRectangle(0, 0, pw, ph, Vector3(0, 0, 0), 0);
  float slant = std::max(6.0f, std::round(ph * 0.28f));
  FillPlateQuad(canvas, 0.0f, (float)pw, 0.0f, (float)pw - slant, 0.0f, (float)ph, Vector3(12, 18, 28), 188);
  canvas->DrawRectangle(0, 0, 3, ph, accentColor, 240);
  canvas->OnChange();
}

void Gui2PlayerHUD::PreparePutBuffers() {
  if (!match) return;

  VisionHudSnapshot vSnap;
  vSnap.visible = AIAdaptiveRegistry::Instance().BuildMentalImageHudSummary(
      match, vSnap.line1, vSnap.line2, vSnap.accentColor);
  buf_visionHud = vSnap;

  for (int t = 0; t < 2; t++) {
    Team *team = match->GetTeam(t);
    if (!team) continue;

    // Map each team to Left [0] or Right [1] according to its active pitch half at this exact moment!
    int slot = (team->GetSide() <= 0) ? 0 : 1;
    Player *p = ResolveDisplayedPlayer(team);

    SidePanelSnapshot snap;
    snap.valid = (p != nullptr);
    snap.teamID = t;
    if (p) {
      snap.playerID = p->GetID();
      snap.staminaPct = clamp((int)std::round(p->GetFatigueFactorInv() * 100.0f), 0, 100);
      if (prepCachedPlayerID[slot] != snap.playerID || prepCachedTeamID[slot] != t) {
        int squadNum = ComputeSquadNumber(team, p);
        prepCachedLabel[slot] = FormatBroadcastPlayerName(p, squadNum, slot == 1);
        prepCachedPlayerID[slot] = snap.playerID;
        prepCachedTeamID[slot] = t;
      }
      snap.displayLabel = prepCachedLabel[slot];
    } else {
      snap.playerID = -1;
      snap.staminaPct = 100;
      snap.displayLabel = " ";
      prepCachedPlayerID[slot] = -1;
    }
    buf_panels[slot] = snap;
  }
}

void Gui2PlayerHUD::FetchPutBuffers() {
  fetched_panels[0] = buf_panels[0];
  fetched_panels[1] = buf_panels[1];
  fetched_visionHud = buf_visionHud;
}

void Gui2PlayerHUD::Put() {
  bool justShownVision = false;
  if (fetched_visionHud.visible != cachedVisionVisible) {
    cachedVisionVisible = fetched_visionHud.visible;
    if (cachedVisionVisible) {
      visionCardBg->Show();
      visionLine1Cap->Show();
      visionLine2Cap->Show();
      justShownVision = true;
    } else {
      visionCardBg->Hide();
      visionLine1Cap->Hide();
      visionLine2Cap->Hide();
    }
  }
  if (fetched_visionHud.visible) {
    if ((fetched_visionHud.accentColor - cachedVisionAccent).GetLength() > 2.0f) {
      cachedVisionAccent = fetched_visionHud.accentColor;
      RedrawVisionCardBg(cachedVisionAccent);
    }
    const unsigned long nowWall_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const bool line1Changed = (fetched_visionHud.line1 != cachedVisionLine1);
    const bool canRefreshText = justShownVision || line1Changed ||
                                (long)(nowWall_ms - lastVisionTextUpdate_ms) >= 120 ||
                                (long)(nowWall_ms - lastVisionTextUpdate_ms) < 0;
    if (canRefreshText) {
      lastVisionTextUpdate_ms = nowWall_ms;
      if (line1Changed) {
        cachedVisionLine1 = fetched_visionHud.line1;
        visionLine1Cap->SetCaption(cachedVisionLine1);
      }
      if (fetched_visionHud.line2 != cachedVisionLine2) {
        cachedVisionLine2 = fetched_visionHud.line2;
        visionLine2Cap->SetCaption(cachedVisionLine2);
        float neededW = clamp(std::max(visionLine1Cap->GetTextWidthPercent(), visionLine2Cap->GetTextWidthPercent()) + 2.1f, 28.0f, 48.0f);
        float curW = 0, curH = 0;
        visionCardBg->GetSize(curW, curH);
        if (fabs(neededW - curW) > 1.2f) {
          visionCardBg->SetSize(neededW, curH);
          RedrawVisionCardBg(cachedVisionAccent);
        }
      }
    }
  }

  for (int slot = 0; slot < 2; slot++) {
    const SidePanelSnapshot &snap = fetched_panels[slot];
    if (!snap.valid) continue;

    bool teamSwapped = (cachedTeamID[slot] != snap.teamID);
    bool staminaChanged = (cachedStaminaPct[slot] != snap.staminaPct);
    bool labelChanged = (cachedPlayerID[slot] != snap.playerID || cachedLabel[slot] != snap.displayLabel);

    if (teamSwapped && snap.teamID >= 0 && snap.teamID <= 1) {
      if (slot == 0) {
        teamCrestImg[snap.teamID]->SetPosition(leftCardXPct, cardYPct);
      } else {
        teamCrestImg[snap.teamID]->SetPosition(rightCardXPct + cardWidthPct - crestWidthPct, cardYPct);
      }
    }

    if (teamSwapped) {
      RedrawSidePlate(slot, snap.teamID, snap.staminaPct);
      cachedTeamID[slot] = snap.teamID;
      cachedStaminaPct[slot] = snap.staminaPct;
    } else if (staminaChanged) {
      UpdateSidePlateStaminaOnly(slot, snap.staminaPct);
      cachedStaminaPct[slot] = snap.staminaPct;
    }

    if (labelChanged || teamSwapped) {
      nameCaption[slot]->SetCaption(snap.displayLabel);
      float textY = cardYPct + 1.58f;
      if (slot == 0) {
        nameCaption[slot]->SetPosition(leftCardXPct + crestWidthPct + 0.65f, textY);
      } else {
        float rightTextAnchor = rightCardXPct + cardWidthPct - crestWidthPct - 0.65f;
        nameCaption[slot]->SetPosition(rightTextAnchor - nameCaption[slot]->GetTextWidthPercent(), textY);
      }
      cachedPlayerID[slot] = snap.playerID;
      cachedLabel[slot] = snap.displayLabel;
    }
  }
}

void Gui2PlayerHUD::GetImages(std::vector < blunted::intrusive_ptr<Image2D> > &target) {
  Gui2View::GetImages(target);
}

void Gui2PlayerHUD::Redraw() {
  for (int slot = 0; slot < 2; slot++) {
    if (cachedTeamID[slot] >= 0) {
      RedrawSidePlate(slot, cachedTeamID[slot], std::max(0, cachedStaminaPct[slot]));
    }
  }
}
