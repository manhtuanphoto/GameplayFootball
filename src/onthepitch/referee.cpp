// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "referee.hpp"

#include "scene/objectfactory.hpp"
#include "managers/resourcemanagerpool.hpp"

#include "match.hpp"
#include "AIsupport/AIfunctions.hpp"

#include "../main.hpp"

Referee::Referee(Match *match) : match(match) {
  buffer.desiredSetPiece = e_SetPiece_KickOff;
  buffer.teamID = 0;
  buffer.stopTime = 0;
  buffer.prepareTime = 0;
  buffer.startTime = buffer.prepareTime + 2000;
  buffer.restartPos = Vector3(0);
  buffer.taker = 0;
  buffer.endPhase = true;
  buffer.active = true;

  foul.foulPlayer = 0;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foulTime = 0;
  foul.hasBeenProcessed = true;

  afterSetPieceRelaxTime_ms = 0;
  freeKickPlayStart_ms = 0;
  gkRetainStart_ms = 0;

  if (match && match->IsFreeKickMode()) {
    match->IncrementFreeKickAttemptCount();
    buffer.desiredSetPiece = e_SetPiece_FreeKick;
    buffer.teamID = 0;
    buffer.stopTime = 0;
    buffer.prepareTime = 0;
    buffer.startTime = 1400;
    buffer.restartPos = GenerateFreeKickSpot();
    buffer.taker = 0;
    buffer.endPhase = true;
    buffer.active = true;
  }


  // whistle

  blunted::intrusive_ptr < Resource<SoundBuffer> > soundBufferRes = ResourceManagerPool::GetInstance().GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)->Fetch("media/sounds/whistle2.wav", true, true);
  whistle[1] = blunted::static_pointer_cast<Sound>(ObjectFactory::GetInstance().CreateObject("whistle1", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(whistle[1]);
  whistle[1]->SetSoundBuffer(soundBufferRes);
  //whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
  whistle[1]->SetLoop(false);
  GetScene3D()->AddObject(whistle[1]);

  soundBufferRes = ResourceManagerPool::GetInstance().GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)->Fetch("media/sounds/whistle3.wav", true, true);
  whistle[3] = blunted::static_pointer_cast<Sound>(ObjectFactory::GetInstance().CreateObject("whistle3", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(whistle[3]);
  whistle[3]->SetSoundBuffer(soundBufferRes);
  //whistle[3]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
  whistle[3]->SetLoop(false);
  GetScene3D()->AddObject(whistle[3]);

  // for usage in destructor
  scene3D = GetScene3D();
}

Referee::~Referee() {
  if (Verbose()) printf("exiting referee.. ");

  scene3D->DeleteObject(whistle[1]);
  scene3D->DeleteObject(whistle[3]);
  whistle[1].reset();
  whistle[3].reset();

  if (Verbose()) printf("done\n");
}

void Referee::Process() {
  //printf("%i", match->GetMatchState());

  if (match->IsInPlay() && !match->IsInSetPiece()) {

    Vector3 ballPos = match->GetBall()->Predict(0);


    // some phase is over :[ (disabled in Endless Sandbox & Free Kick modes)

    if (!match->IsSandboxEndless() && !match->IsFreeKickMode() &&
        ((match->GetMatchPhase() == e_MatchPhase_1stHalf && match->GetMatchTime_ms() > 2700000) ||
         (match->GetMatchPhase() == e_MatchPhase_2ndHalf && match->GetMatchTime_ms() > 5400000) ||
         (match->GetMatchPhase() == e_MatchPhase_1stExtraTime && match->GetMatchTime_ms() > 6300000) ||
         (match->GetMatchPhase() == e_MatchPhase_2ndExtraTime && match->GetMatchTime_ms() > 7200000)) &&
        ballPos.coords[0] < 10 && ballPos.coords[0] > -10) {

      foul.advantage = false;
      if (!CheckFoul()) {

        match->StopPlay();
        whistle[3]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[3]->Poke(e_SystemType_Audio);

        buffer.desiredSetPiece = e_SetPiece_KickOff;
        buffer.stopTime = match->GetActualTime_ms();
        buffer.prepareTime = match->GetActualTime_ms() + 3000;
        buffer.startTime = buffer.prepareTime + 2000;
        buffer.restartPos = Vector3(0);
        buffer.active = true;
        buffer.endPhase = true;
        if (match->GetMatchPhase() == e_MatchPhase_1stHalf || match->GetMatchPhase() == e_MatchPhase_1stExtraTime) {
          buffer.teamID = 1;
        } else {
          buffer.teamID = 0;
        }

        e_MatchPhase nextPhase;
        if (match->GetMatchPhase() == e_MatchPhase_1stHalf) nextPhase = e_MatchPhase_2ndHalf;
        if (match->GetMatchPhase() == e_MatchPhase_2ndHalf) nextPhase = e_MatchPhase_1stExtraTime;
        if (match->GetMatchPhase() == e_MatchPhase_1stExtraTime) nextPhase = e_MatchPhase_2ndExtraTime;
        if (match->GetMatchPhase() == e_MatchPhase_2ndExtraTime) nextPhase = e_MatchPhase_Penalties;
        match->SetMatchPhase(nextPhase);
      }
    }


    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11 || match->IsGoalScored()) {

      foul.advantage = false;
      bool isFoul = false;
      if (!match->IsGoalScored()) isFoul = CheckFoul(); else foul.foulType = 0;
      if (isFoul == false) {

        match->StopPlay();

        // corner, goal kick or kick off?
        signed int lastSide = -1;
        Team *lastTouchTeam = match->GetLastTouchTeam();
        if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(0);
        lastSide = lastTouchTeam->GetSide();

        if (match->IsGoalScored()) {
          bool isAISpectator = (match->GetTeam(0)->GetHumanGamerCount() == 0 &&
                                match->GetTeam(1)->GetHumanGamerCount() == 0);
          if (match->IsFreeKickMode()) {
            match->IncrementFreeKickAttemptCount();
            buffer.desiredSetPiece = e_SetPiece_FreeKick;
            buffer.stopTime = match->GetActualTime_ms();
            buffer.prepareTime = match->GetActualTime_ms() + 6800;
            buffer.startTime = buffer.prepareTime + 1400;
            buffer.restartPos = GenerateFreeKickSpot();
            buffer.teamID = 0;
          } else {
            buffer.desiredSetPiece = e_SetPiece_KickOff;
            buffer.stopTime = match->GetActualTime_ms();
            buffer.prepareTime = match->GetActualTime_ms() + (isAISpectator ? 9000 : 10800);
            buffer.startTime = buffer.prepareTime + 2000;
            buffer.restartPos = Vector3(0, 0, 0);
            buffer.teamID = abs(match->GetLastGoalTeamID() - 1);
          }

        } else if (match->IsFreeKickMode()) {
          QueueNextFreeKickScenario(false);
          return;

        } else if ((ballPos.coords[0] > 0 && lastSide > 0) || (ballPos.coords[0] < 0 && lastSide < 0)) {
          buffer.desiredSetPiece = e_SetPiece_Corner;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          float y = ballPos.coords[1];
          if (y > 0) y = pitchHalfH; else
                     y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);

        } else {
          buffer.desiredSetPiece = e_SetPiece_GoalKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
        }

        buffer.active = true;
      }
    }


    // over sideline

    if (afterSetPieceRelaxTime_ms == 0) {
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        foul.advantage = false;
        if (!CheckFoul()) {
          if (match->IsFreeKickMode()) {
            QueueNextFreeKickScenario(false);
            return;
          }
          match->StopPlay();
          Team *lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
          buffer.desiredSetPiece = e_SetPiece_ThrowIn;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos.coords[0] = clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] >  0) buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0) buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          buffer.active = true;
        }
      }
    }

    if (match->IsFreeKickMode() && !match->IsGoalScored() && !buffer.active) {
      signed int attackDir = -match->GetTeam(0)->GetSide();
      Player *retainer = match->GetBallRetainer();
      if (retainer != 0 && retainer->GetTeamID() == 1) {
        if (gkRetainStart_ms == 0) gkRetainStart_ms = match->GetActualTime_ms();
        if (match->GetActualTime_ms() - gkRetainStart_ms >= 1100) {
          match->SpamMessage("Saved & Held by Keeper! Next Free Kick...", 1600);
          QueueNextFreeKickScenario(false);
          return;
        }
      } else {
        gkRetainStart_ms = 0;
      }

      bool clearedAway = (ballPos.coords[0] * attackDir < 10.0f);
      bool playTimedOut = (freeKickPlayStart_ms > 0 && (match->GetActualTime_ms() - freeKickPlayStart_ms > 9000));
      if (clearedAway || playTimedOut) {
        QueueNextFreeKickScenario(false);
        return;
      }
    }

    CheckFoul();

  } else { // not in play, maybe something needs to happen?

    if (!match->IsInPlay() && !match->IsInSetPiece() && buffer.active == true) {

      if (buffer.stopTime + 300 == match->GetActualTime_ms() && buffer.endPhase == false && buffer.desiredSetPiece != e_SetPiece_KickOff) {
        whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[1]->Poke(e_SystemType_Audio);
      }

      if (buffer.prepareTime == match->GetActualTime_ms()) {

        if (buffer.endPhase == true) {
          if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
            match->SetMatchPhase(e_MatchPhase_1stHalf);
          } else {
            // game over conditions
            if (match->GetMatchPhase() == e_MatchPhase_1stExtraTime) {
              if (match->GetScore(0) != match->GetScore(1)) {
                match->GameOver();
                return;
              }
            }
            if (match->GetMatchPhase() == e_MatchPhase_Penalties) {
              match->GameOver();
              return;
            }
            match->sig_OnMatchPhaseChange(match);
          }
          buffer.endPhase = false;
        }

        PrepareSetPiece(buffer.desiredSetPiece);
      }

      if (buffer.startTime == match->GetActualTime_ms()) {
        // blow whistle and wait for set piece taker to touch the ball
        whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[1]->Poke(e_SystemType_Audio);
        match->StartPlay();
        match->StartSetPiece();
      }
    }
  }

  if (match->IsInSetPiece()) {
    // check if set piece has been taken
    if (buffer.taker->TouchAnim() && !buffer.taker->TouchPending()) {
      buffer.active = false;
      match->StopSetPiece();
      match->GetTeam(0)->GetController()->PrepareSetPiece(e_SetPiece_None);
      match->GetTeam(1)->GetController()->PrepareSetPiece(e_SetPiece_None);
      afterSetPieceRelaxTime_ms = 400;
      freeKickPlayStart_ms = match->GetActualTime_ms();
      gkRetainStart_ms = 0;
      foul.foulPlayer = 0;
      foul.foulType = 0;

      if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
        match->SetMatchPhase(e_MatchPhase_1stHalf);
      }
    }
  }

  if (afterSetPieceRelaxTime_ms > 0) afterSetPieceRelaxTime_ms -= 10;
}

Vector3 Referee::GenerateFreeKickSpot() {
  signed int attackDir = -match->GetTeam(0)->GetSide(); // +1: attacks right goal (+pitchHalfW)
  int preset = match->GetFreeKickPreset();

  float distFromGoalLine = 21.5f;
  float yPos = 0.0f;

  if (preset == 1) {
    // Central Edge of Box (18.5m - 22.5m, slight central offset)
    distFromGoalLine = random(18.5f, 22.5f);
    yPos = random(-5.5f, 5.5f);
  } else if (preset == 2) {
    // Left / Right Wing Curlers (20.5m - 25.5m, wide angle outside penalty arc)
    distFromGoalLine = random(20.5f, 25.5f);
    float sideSign = (match->GetFreeKickAttemptCount() % 2 == 0) ? 1.0f : -1.0f;
    yPos = sideSign * random(8.5f, 16.5f);
  } else if (preset == 3) {
    // Long-Range Rockets (27.0m - 32.5m)
    distFromGoalLine = random(27.0f, 32.5f);
    yPos = random(-13.0f, 13.0f);
  } else {
    // Varied Spots (rotates across central, left curler, right curler, and 26-29m power spots)
    int cycle = match->GetFreeKickAttemptCount() % 4;
    if (cycle == 1) {
      distFromGoalLine = random(18.5f, 21.5f);
      yPos = random(-4.8f, 4.8f);
    } else if (cycle == 2) {
      distFromGoalLine = random(21.0f, 24.8f);
      yPos = random(7.5f, 15.0f);
    } else if (cycle == 3) {
      distFromGoalLine = random(21.0f, 24.8f);
      yPos = -random(7.5f, 15.0f);
    } else {
      distFromGoalLine = random(25.5f, 29.5f);
      yPos = random(-10.5f, 10.5f);
    }
  }

  // Ensure strictly outside the penalty box (16.5m x +-20.15m) so it is always a true Free Kick
  if (distFromGoalLine < 17.8f && fabs(yPos) < 20.5f) {
    distFromGoalLine = 17.8f;
  }

  float xPos = (pitchHalfW - distFromGoalLine) * (float)attackDir;
  return Vector3(xPos, yPos, 0.0f);
}

void Referee::QueueNextFreeKickScenario(bool immediate) {
  foul.advantage = false;
  foul.foulPlayer = 0;
  foul.foulType = 0;
  foul.hasBeenProcessed = true;
  gkRetainStart_ms = 0;
  freeKickPlayStart_ms = 0;

  match->StopPlay();
  match->StopSetPiece();
  match->IncrementFreeKickAttemptCount();

  buffer.desiredSetPiece = e_SetPiece_FreeKick;
  buffer.teamID = 0;
  buffer.stopTime = match->GetActualTime_ms();
  buffer.prepareTime = immediate ? match->GetActualTime_ms() : (match->GetActualTime_ms() + 1400);
  buffer.startTime = buffer.prepareTime + 1300;
  buffer.restartPos = GenerateFreeKickSpot();
  buffer.endPhase = false;
  buffer.active = true;
}

void Referee::PrepareSetPiece(e_SetPiece setPiece) {
  // position players for set piece situation

  match->ResetSituation(buffer.restartPos);

  match->GetTeam(0)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);
  match->GetTeam(1)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);

  buffer.taker = match->GetTeam(buffer.teamID)->GetController()->GetPieceTaker();
  if (buffer.taker && match->GetTeam(buffer.teamID)->GetHumanGamerCount() > 0) {
    match->GetTeam(buffer.teamID)->SelectPlayer(buffer.taker);
  }

  if (match->IsFreeKickMode() && setPiece == e_SetPiece_FreeKick) {
    signed int attackDir = -match->GetTeam(0)->GetSide();
    float distM = (Vector3(pitchHalfW * (float)attackDir, 0, 0) - buffer.restartPos).GetLength();
    std::string takerName = buffer.taker ? buffer.taker->GetPlayerData()->GetLastName() : "Taker";
    match->SpamMessage("Free Kick #" + int_to_str(match->GetFreeKickAttemptCount()) + " (" + int_to_str((int)round(distM)) + "m) - " + takerName + "  |  [D] Shoot  [A] Lob  [S] Pass  [F5] New Spot", 3200);
  }
}

void Referee::AlterSetPiecePrepareTime(unsigned long newTime_ms) {
  if (buffer.active) {
    buffer.prepareTime = newTime_ms;
    buffer.startTime = buffer.prepareTime + 2000;
  }
}

void Referee::BallTouched() {

  // check for offside player receiving the ball

  int lastTouchTeamID = match->GetLastTouchTeamID();
  if (lastTouchTeamID == -1) return; // shouldn't happen really ;)
  if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false && match->GetTeam(abs(lastTouchTeamID - 1))->GetActivePlayerCount() > 1) { // disable if only 1 player: that's debug mode with only keeper
    std::map<Player*, Vector3>::iterator playerIter = offsidePlayers.begin();
    while (playerIter != offsidePlayers.end()) {
      if (match->GetTeam(lastTouchTeamID)->GetLastTouchPlayer() == playerIter->first) {
        foul.advantage = false;
        if (!CheckFoul()) {
          // uooooga uooooga offside!
          if (match->IsFreeKickMode()) {
            match->SpamMessage("offside! Next Free Kick...", 1800);
            QueueNextFreeKickScenario(false);
            break;
          }
          match->StopPlay();
          buffer.desiredSetPiece = e_SetPiece_FreeKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = playerIter->second;
          buffer.teamID = abs(lastTouchTeamID - 1);
          buffer.active = true;
          match->SpamMessage("offside!");
          break;
        } else break;
      }
      playerIter++;
    }
  }

  offsidePlayers.clear();

  if (match->IsInPlay() &&
      (buffer.active == false ||
       (buffer.active == true && buffer.desiredSetPiece != e_SetPiece_ThrowIn))) {
    // check for offside players at moment of touch
    float offside = AI_GetOffsideLine(match, match->GetMentalImage(0), abs(lastTouchTeamID - 1));
    std::vector<Player*> players;
    Team *team = match->GetTeam(lastTouchTeamID);
    match->GetTeam(lastTouchTeamID)->GetActivePlayers(players);
    for (unsigned int i = 0; i < players.size(); i++) {
      if (players.at(i) != team->GetLastTouchPlayer()) {
        if (players.at(i)->GetPosition().coords[0] * team->GetSide() < offside * team->GetSide() - 0.20/*relax*/) {
          offsidePlayers.insert(std::pair<Player*, Vector3>(players.at(i), players.at(i)->GetPosition()));
        }
      }
    }
  }

}

void Referee::TripNotice(Player *tripee, Player *tripper, int tackleType) {

  if (buffer.active) return;

  if (tackleType == 2) { // standing tackle
    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripper->GetCurrentFunctionType() == e_FunctionType_Interfere || tripper->GetCurrentFunctionType() == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - match->GetBall()->Predict(0).Get2D()).GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      // uooooga uooooga foul!
      foul.foulType = 1;
      foul.advantage = true;
      foul.foulPlayer = tripper;
      foul.foulVictim = tripee;
      foul.foulTime = match->GetActualTime_ms();
      foul.foulPosition = tripee->GetPosition();
      foul.hasBeenProcessed = false;
      if (!IsReleaseVersion()) match->SpamMessage("advantage", 2000);
    }

  } else if (tackleType == 3 && (tripper != foul.foulPlayer || foul.foulType == 0)) { // sliding tackle

    if (match->GetActualTime_ms() - tripper->GetLastTouchTime_ms() > 600 &&
        tripper->GetCurrentFunctionType() == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() && (match->GetBall()->Predict(0) - tripee->GetPosition()).GetLength() < 8.0) {
      float severity = 1.0;
      if (tripper->TouchAnim()) {
        severity = std::pow(clamp(fabs(tripper->GetTouchFrame() -
                                       tripper->GetCurrentFrame()) /
                                      tripper->GetTouchFrame(),
                                  0.0, 1.0),
                            0.7) *
                   0.5;
        severity += NormalizedClamp((match->GetBall()->Predict(0) - tripper->GetTouchPos()).GetLength(), 0.0, 2.0) * 0.5;
      }
      // from behind?
      severity += (tripee->GetPosition() - tripper->GetPosition()).GetNormalized(0).GetDotProduct(tripee->GetDirectionVec()) * 0.5 + 0.5;

      if (severity > 1.0) {
        // uooooga uooooga foul!
        //printf("sliding! %lu ms ago\n", match->GetActualTime_ms() - tripper->GetLastTouchTime_ms());
        foul.foulType = 1;
        foul.advantage = true;
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foulTime = match->GetActualTime_ms();
        foul.foulPosition = tripee->GetPosition();
        foul.hasBeenProcessed = false;
        if (severity > 1.4) foul.foulType = 2;
        if (severity > 2.0) {
          foul.foulType = 3;
          foul.advantage = false;
        } else {
          if (!IsReleaseVersion()) match->SpamMessage("advantage", 3000);
        }
      }
    }

  }
}

bool Referee::CheckFoul() {

  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15 - lineHalfW && foul.foulPosition.coords[0] * -foul.foulVictim->GetTeam()->GetSide() > pitchHalfW - 16.5 + lineHalfW) penalty = true;
  }

  if (foul.advantage) {
    if (penalty) {
      foul.advantage = false;
    } else {
      if (match->GetActualTime_ms() - 600 > foul.foulTime) {
        if (match->GetActualTime_ms() - 3000 > foul.foulTime) {
          // cancel foul, advantage took long enough
          // todo: yellow cards need to be remembered though ;)
          foul.foulPlayer = 0;
          foul.foulType = 0;
        } else {
          // calculate if there's advantage still
          if (foul.foulVictim->GetTeam()->GetFadingTeamPossessionAmount() < 1.0) {
            foul.advantage = false;
          }
        }
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {

    if (match->IsFreeKickMode() && foul.foulVictim && foul.foulVictim->GetTeam()->GetID() != 0) {
      foul.hasBeenProcessed = true;
      QueueNextFreeKickScenario(false);
      return true;
    }

    match->StopPlay();
    if (!penalty) {
      buffer.desiredSetPiece = e_SetPiece_FreeKick;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (foul.foulType >= 2) buffer.prepareTime += 10000;
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos = foul.foulPosition;
    } else {
      buffer.desiredSetPiece = e_SetPiece_Penalty;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (foul.foulType >= 2) buffer.prepareTime += 10000;
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos = Vector3((pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetSide(), 0, 0);
    }
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    buffer.active = true;
    std::string spamMessage = "foul!";
    if (foul.foulType == 2) {
      spamMessage.append(" yellow card");
      foul.foulPlayer->GiveYellowCard(match->GetActualTime_ms() + 6000); // need to find out proper moment
    }
    if (foul.foulType == 3) {
      spamMessage.append(" red card!!!");
      foul.foulPlayer->GiveRedCard(match->GetActualTime_ms() + 6000); // need to find out proper moment
    }
    match->SpamMessage(spamMessage);

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}
