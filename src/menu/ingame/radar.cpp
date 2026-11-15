// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "radar.hpp"

#include "utils/gui2/windowmanager.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL2_rotozoom.h>

#include "../../gamedefines.hpp"

#include "../../onthepitch/match.hpp"

namespace blunted {

  Gui2Radar::Gui2Radar(Gui2WindowManager *windowManager, const std::string &name, float x_percent, float y_percent, float width_percent, float height_percent, Match *match, const Vector3 &color1_1, const Vector3 &color1_2, const Vector3 &color2_1, const Vector3 &color2_2) : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent), match(match), color1_1(color1_1), color1_2(color1_2), color2_1(color2_1), color2_2(color2_2) {

    // todo: use colors!

    bg = new Gui2Image(windowManager, "bg_radar", 0, 0, width_percent, height_percent);
    this->AddView(bg);
    bg->LoadImage("media/menu/radar/radar.png");
    bg->Show();

    float ballH = 1.3f;
    float ballW = ballH / windowManager->GetAspectRatio();
    ball = new Gui2Image(windowManager, "radar_ball", 0, 0, ballW, ballH);
    this->AddView(ball);
    ball->LoadImage("media/menu/radar/ball.png");
    ball->Show();

    this->Show();
  }

  Gui2Radar::~Gui2Radar() {
  }

  void Gui2Radar::ReloadAvatars(int teamID, unsigned int playerCount) {
    float avatarH = 1.6f;
    float avatarW = avatarH / windowManager->GetAspectRatio();

    if (teamID == 0) {
      for (unsigned int i = 0; i < team1avatars.size(); i++) {
        team1avatars.at(i)->Exit();
        delete team1avatars.at(i);
      }
      team1avatars.clear();
      for (unsigned int i = 0; i < playerCount; i++) {
        Gui2Image *avatar = new Gui2Image(windowManager, "radar_avatar_" + int_to_str(teamID) + "_" + int_to_str(i), 0, 0, avatarW, avatarH);
        this->AddView(avatar);
        avatar->LoadImage("media/menu/radar/p1.png");
        avatar->Show();
        team1avatars.push_back(avatar);
      }
    }

    // oof ugly c/p'ed code
    if (teamID == 1) {
      for (unsigned int i = 0; i < team2avatars.size(); i++) {
        team2avatars.at(i)->Exit();
        delete team2avatars.at(i);
      }
      team2avatars.clear();
      for (unsigned int i = 0; i < playerCount; i++) {
        Gui2Image *avatar = new Gui2Image(windowManager, "radar_avatar_" + int_to_str(teamID) + "_" + int_to_str(i), 0, 0, avatarW, avatarH);
        this->AddView(avatar);
        avatar->LoadImage("media/menu/radar/p2.png");
        avatar->Show();
        team2avatars.push_back(avatar);
      }
    }

  }

  void Gui2Radar::Process() {
  }

  void Gui2Radar::Put() {
    float ballH = 1.3f;
    float ballW = ballH / windowManager->GetAspectRatio();
    float avatarH = 1.6f;
    float avatarW = avatarH / windowManager->GetAspectRatio();

    Vector3 position = match->GetBall()->Predict(0).Get2D();
    Vector3 pos2d = position * Vector3(1 / (pitchHalfW * 2), - (1 / (pitchHalfH * 2)), 0);
    pos2d = pos2d + Vector3(0.5, 0.5, 0);
    pos2d = pos2d * Vector3(0.96f, 0.96f, 0) + Vector3(0.02f, 0.02f, 0); // margin
    ball->SetPosition(pos2d.coords[0] * width_percent - ballW * 0.5f, pos2d.coords[1] * height_percent - ballH * 0.5f);

    // get player positions
    team1PlayersScratch.clear();
    match->GetActiveTeamPlayers(0, team1PlayersScratch);
    team2PlayersScratch.clear();
    match->GetActiveTeamPlayers(1, team2PlayersScratch);

    bool avatarsReloaded = false;
    if (team1PlayersScratch.size() != team1avatars.size()) {
      ReloadAvatars(0, team1PlayersScratch.size());
      avatarsReloaded = true;
    }
    if (team2PlayersScratch.size() != team2avatars.size()) {
      ReloadAvatars(1, team2PlayersScratch.size());
      avatarsReloaded = true;
    }
    if (!ballZInitialized || avatarsReloaded) {
      ball->SetZPriority(1); // ball on top
      ballZInitialized = true;
    }

    const float invPitchW = 1.0f / (pitchHalfW * 2.0f);
    const float invPitchH = -1.0f / (pitchHalfH * 2.0f);

    for (unsigned int i = 0; i < team1PlayersScratch.size(); i++) {
      Vector3 pPos = team1PlayersScratch[i]->GetPosition();
      float px = ((pPos.coords[0] * invPitchW + 0.5f) * 0.96f + 0.02f) * width_percent - avatarW * 0.5f;
      float py = ((pPos.coords[1] * invPitchH + 0.5f) * 0.96f + 0.02f) * height_percent - avatarH * 0.5f;
      team1avatars[i]->SetPosition(px, py);
    }

    for (unsigned int i = 0; i < team2PlayersScratch.size(); i++) {
      Vector3 pPos = team2PlayersScratch[i]->GetPosition();
      float px = ((pPos.coords[0] * invPitchW + 0.5f) * 0.96f + 0.02f) * width_percent - avatarW * 0.5f;
      float py = ((pPos.coords[1] * invPitchH + 0.5f) * 0.96f + 0.02f) * height_percent - avatarH * 0.5f;
      team2avatars[i]->SetPosition(px, py);
    }

  }

}
