// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "../../../main.hpp"

#include "elizacontroller.hpp"

#include <cmath>

#include "../../AIsupport/mentalimage.hpp"
#include "../../AIsupport/AIfunctions.hpp"
#include "../../AIsupport/ai_adaptive_pipeline.hpp"

#include "../humanoid/humanoid_utils.hpp"

#include "strategies/strategy.hpp"

#include "strategies/offtheball/default_def.hpp"
#include "strategies/offtheball/default_mid.hpp"
#include "strategies/offtheball/default_off.hpp"
#include "strategies/offtheball/goalie_default.hpp"
#include "../playerofficial.hpp"

ElizaController::ElizaController(Match *match) : PlayerController(match) {
  lastDesiredDirection = Vector3(0);
  lastDesiredVelocity = 0;
  pendingTakeOnKnockOn = false;
}

ElizaController::~ElizaController() {
  if (Verbose()) printf("exiting elizacontroller.. ");
  if (defenseStrategy) delete defenseStrategy;
  if (midfieldStrategy) delete midfieldStrategy;
  if (offenseStrategy) delete offenseStrategy;
  if (goalieStrategy) delete goalieStrategy;
  if (Verbose()) printf("done\n");
}

void ElizaController::RequestCommand(PlayerCommandQueue &commandQueue) {

  lastSwitchTimeDuration_ms = 0;
  lastSwitchTime_ms = 0;
  pendingTakeOnKnockOn = false;

  CastPlayer()->SetDesiredTimeToBall_ms(0);

  _CalculateSituation();

  _Preprocess(); // calculate some variables


  FormationEntry entry = CastPlayer()->GetDynamicFormationEntry();
  float mindSet = AI_GetMindSet(entry.role);


  // input

  Vector3 rawInputDirection = player->GetDirectionVec();
  float rawInputVelocityFloat = idleVelocity;
  Vector3 manualMovementDirection = player->GetDirectionVec();
  float manualMovementVelocityFloat = idleVelocity;

  bool manualMovement = false;
  bool extraHaste = false;


  // celebrate good times come on!

  if (!match->IsInPlay() && match->IsGoalScored()) {
    _AddCelebration(commandQueue);
    return;
  }


  // look at referee

  else if (!match->IsInPlay() && match->GetReferee()->GetBuffer().active == true &&
           (match->GetReferee()->GetCurrentFoulType() == 2 || match->GetReferee()->GetCurrentFoulType() == 3) &&
           match->GetReferee()->GetBuffer().stopTime < match->GetActualTime_ms() - 1000 &&
           match->GetReferee()->GetBuffer().prepareTime > match->GetActualTime_ms()) {

    // look at referee
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.useDesiredLookAt = true;
    command.desiredDirection = CastPlayer()->GetDirectionVec();
    assert(command.desiredDirection.coords[2] == 0.0f);
    command.desiredVelocityFloat = idleVelocity;
    command.desiredLookAt = match->GetOfficials()->GetReferee()->GetPosition();
    commandQueue.push_back(command);

    return;
  }


  // stand still

  else if (!match->IsInPlay()) { // this whole if/then/else structure is ugly and unclear

    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    if (match->GetBallRetainer() == player) {
      command.desiredDirection = (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec());
    } else {
      command.desiredDirection = player->GetDirectionVec();
    }
    command.desiredVelocityFloat = idleVelocity;
    if (!match->IsInSetPiece()) {
      command.desiredDirection = (player->GetDirectionVec() * 0.6f + (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec()) * 0.4f).GetNormalized(player->GetDirectionVec());
      command.desiredVelocityFloat = ClampVelocity(player->GetFloatVelocity() * 0.95f - random(0.0f, 3.2f));
    }
    command.useDesiredLookAt = true;
    command.desiredLookAt = player->GetPosition() + (match->GetBall()->Predict(0).Get2D() - player->GetPosition()).GetNormalized(command.desiredDirection) * 10.0f;
    commandQueue.push_back(command);

    return;
  }


  // set piece taking

  else if ((match->IsInSetPiece() && team->GetController()->GetPieceTaker() == player) || match->GetBallRetainer() == player) {

    PlayerCommand actionCommand;

    if (team->GetController()->GetSetPieceType() == e_SetPiece_Penalty) {

      float sideSign = (random(0.0f, 1.0f) < 0.5f) ? -1.0f : 1.0f;
      float targetY = sideSign * random(1.9f, 3.3f);
      if (random(0.0f, 1.0f) < 0.15f) targetY = random(-0.6f, 0.6f); // occasional compose down the middle
      actionCommand.desiredFunctionType = e_FunctionType_Shot;
      actionCommand.useDesiredMovement = false;
      actionCommand.useDesiredLookAt = false;
      actionCommand.touchInfo.desiredDirection = (Vector3(-team->GetSide() * pitchHalfW, targetY, 0) - CastPlayer()->GetPosition()).GetNormalized(Vector3(-team->GetSide(), 0, 0));
      actionCommand.touchInfo.desiredPower = random(0.55f, 0.86f);
      commandQueue.push_back(actionCommand);

    } else {

      Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
      Vector3 oppGoalCenter(-team->GetSide() * pitchHalfW, 0, 0);
      float distToOppGoal = (ballPos - oppGoalCenter).GetLength();

      // Direct free-kick shot when within dangerous range & good angle
      float maxDirectFKDist = match->IsFreeKickMode() ? 33.5f : 29.0f;
      float directFKChance = match->IsFreeKickMode()
                                 ? (0.82f + 0.15f * CastPlayer()->GetStat("technical_shot"))
                                 : (0.58f + 0.35f * CastPlayer()->GetStat("technical_shot"));
      if (team->GetController()->GetSetPieceType() == e_SetPiece_FreeKick &&
          distToOppGoal < maxDirectFKDist && fabs(ballPos.coords[1]) < 20.0f &&
          random(0.0f, 1.0f) < directFKChance) {
        float targetY = (ballPos.coords[1] > 0.0f) ? random(-3.20f, -1.25f) : random(1.25f, 3.20f);
        if (random(0.0f, 1.0f) < 0.38f) targetY = -targetY;
        actionCommand.desiredFunctionType = e_FunctionType_Shot;
        actionCommand.useDesiredMovement = false;
        actionCommand.useDesiredLookAt = false;
        actionCommand.desiredVelocityFloat = dribbleVelocity;
        actionCommand.touchInfo.desiredDirection = (Vector3(-team->GetSide() * (pitchHalfW + 1.0f), targetY, 0) - ballPos).GetNormalized(Vector3(-team->GetSide(), 0, 0));
        actionCommand.touchInfo.autoDirectionBias = 1.0f;
        actionCommand.touchInfo.desiredPower = clamp(0.56f + (distToOppGoal / 44.0f) * 0.35f, 0.55f, 0.92f);
        commandQueue.push_back(actionCommand);
      } else {

        actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
        actionCommand.useDesiredMovement = false;
        actionCommand.useDesiredLookAt = false;
        actionCommand.touchInfo.inputDirection = player->GetDirectionVec();
        actionCommand.touchInfo.inputPower = 0.5f;
        actionCommand.touchInfo.autoDirectionBias = 1.0f;
        actionCommand.touchInfo.autoPowerBias = 1.0f;

        actionCommand.touchInfo.forcedTargetPlayer = 0;

        Vector3 desiredTargetPosition = player->GetPosition() + Vector3(-team->GetSide() * 12.0f, 0, 0);
        bool doCommand = true;

        std::vector<Player*> mates;
        team->GetActivePlayers(mates);

        if (team->GetController()->GetSetPieceType() == e_SetPiece_GoalKick) {
          Player *bestTarget = 0;
          float bestScore = -100.0f;
          bool goLong = (random(0.0f, 1.0f) > 0.55f && team->GetHumanGamerCount() == 0);
          actionCommand.desiredFunctionType = goLong ? e_FunctionType_HighPass : e_FunctionType_ShortPass;
          for (unsigned int i = 0; i < mates.size(); i++) {
            if (mates.at(i) == CastPlayer()) continue;
            float d = (mates.at(i)->GetPosition() - ballPos).GetLength();
            if (!goLong && (d < 10.0f || d > 34.0f)) continue;
            if (goLong && (d < 28.0f || d > 62.0f)) continue;
            float freeSpace = AI_CalculateFreeSpace(match, _mentalImage, team->GetID(), mates.at(i)->GetPosition(), 10.0f, 0.6f, true);
            float score = freeSpace * 2.0f + random(0.0f, 0.35f);
            if (score > bestScore) {
              bestScore = score;
              bestTarget = mates.at(i);
            }
          }
          if (bestTarget) actionCommand.touchInfo.forcedTargetPlayer = bestTarget;
          else desiredTargetPosition = Vector3(player->GetPosition().coords[0] * 0.7f, random(-pitchHalfH * 0.7f, pitchHalfH * 0.7f), 0.0f);

        } else if (team->GetController()->GetSetPieceType() == e_SetPiece_KickOff) {
          actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
          desiredTargetPosition = player->GetPosition() + Vector3(team->GetSide() * 3.0f, random(-4.0f, 4.0f), 0.0f);

        } else if (team->GetController()->GetSetPieceType() == e_SetPiece_FreeKick) {
          if (distToOppGoal < 42.0f && random(0.0f, 1.0f) > 0.38f) {
            actionCommand.desiredFunctionType = e_FunctionType_HighPass;
            desiredTargetPosition = Vector3((pitchHalfW - random(7.5f, 13.0f)) * -team->GetSide(), random(-6.5f, 6.5f), 0.0f);
          } else {
            actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
            Player *bestShort = 0;
            float bestScore = -100.0f;
            for (unsigned int i = 0; i < mates.size(); i++) {
              if (mates.at(i) == CastPlayer() || mates.at(i)->GetFormationEntry().role == e_PlayerRole_GK) continue;
              float d = (mates.at(i)->GetPosition() - ballPos).GetLength();
              if (d < 6.0f || d > 28.0f) continue;
              float freeSpace = AI_CalculateFreeSpace(match, _mentalImage, team->GetID(), mates.at(i)->GetPosition(), 9.0f, 0.5f, true);
              float prog = (mates.at(i)->GetPosition().coords[0] - ballPos.coords[0]) * -team->GetSide() * 0.03f;
              if (freeSpace + prog > bestScore) {
                bestScore = freeSpace + prog;
                bestShort = mates.at(i);
              }
            }
            if (bestShort) actionCommand.touchInfo.forcedTargetPlayer = bestShort;
            else desiredTargetPosition = player->GetPosition() + Vector3(-team->GetSide() * 12.0f, random(-8.0f, 8.0f), 0.0f);
          }

        } else if (team->GetController()->GetSetPieceType() == e_SetPiece_Corner) {
          if (random(0.0f, 1.0f) > 0.22f) {
            actionCommand.desiredFunctionType = e_FunctionType_HighPass;
            // Deliver dangerous ball into near-post, penalty spot, or far-post zone
            desiredTargetPosition = Vector3((pitchHalfW - random(6.0f, 11.5f)) * -team->GetSide(), random(-5.0f, 5.0f), 0.0f);
          } else {
            actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
            desiredTargetPosition = Vector3((pitchHalfW * -team->GetSide()) * 0.82f, player->GetPosition().coords[1] * 0.78f, 0.0f);
          }

        } else if (team->GetController()->GetSetPieceType() == e_SetPiece_ThrowIn) {
          actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
          Player *bestThrowTarget = 0;
          float bestScore = -100.0f;
          for (unsigned int i = 0; i < mates.size(); i++) {
            if (mates.at(i) == CastPlayer() || mates.at(i)->GetFormationEntry().role == e_PlayerRole_GK) continue;
            float d = (mates.at(i)->GetPosition() - ballPos).GetLength();
            if (d > 22.0f) continue;
            float freeSpace = AI_CalculateFreeSpace(match, _mentalImage, team->GetID(), mates.at(i)->GetPosition(), 8.0f, 0.45f, true);
            float score = freeSpace * 1.6f - d * 0.03f + ((mates.at(i)->GetPosition().coords[0] - ballPos.coords[0]) * -team->GetSide()) * 0.02f;
            if (score > bestScore) {
              bestScore = score;
              bestThrowTarget = mates.at(i);
            }
          }
          if (bestThrowTarget) actionCommand.touchInfo.forcedTargetPlayer = bestThrowTarget;
          else desiredTargetPosition = player->GetPosition();

        } else if (match->GetBallRetainer() == player) { // keeper fetched ball
          actionCommand.desiredFunctionType = (random(0.0f, 1.0f) > 0.45f) ? e_FunctionType_ShortPass : e_FunctionType_HighPass;
          desiredTargetPosition = Vector3(pitchHalfW * team->GetSide() * 0.45f, random(-pitchHalfH * 0.8f, pitchHalfH * 0.8f), 0.0f);
          Player *targetPlayer = AI_GetClosestPlayer(team, desiredTargetPosition, false, CastPlayer());

          if (targetPlayer) {
            Player *closestOpp = AI_GetClosestPlayer(match->GetTeam(abs(team->GetID() - 1)), targetPlayer->GetPosition(), false, 0);
            if (closestOpp) {
              if (((closestOpp->GetPosition() + closestOpp->GetMovement() * 0.1f) - targetPlayer->GetPosition()).GetLength() > 9.5f || CastPlayer()->GetPossessionDuration_ms() > 3800) {
                actionCommand.touchInfo.forcedTargetPlayer = targetPlayer;
              } else {
                doCommand = false;
              }
            }
          }
        }

        if (doCommand) {
          if (actionCommand.touchInfo.forcedTargetPlayer == 0) actionCommand.touchInfo.forcedTargetPlayer = AI_GetClosestPlayer(team, desiredTargetPosition, false, CastPlayer());
          AI_GetPass(CastPlayer(), actionCommand.desiredFunctionType, actionCommand.touchInfo.inputDirection, actionCommand.touchInfo.inputPower, actionCommand.touchInfo.autoDirectionBias, actionCommand.touchInfo.autoPowerBias, actionCommand.touchInfo.desiredDirection, actionCommand.touchInfo.desiredPower, actionCommand.touchInfo.targetPlayer, actionCommand.touchInfo.forcedTargetPlayer);
          commandQueue.push_back(actionCommand);
        }
      }
    }

    // remember, we are still in the 'set piece / ballretainer' if{}

    if (match->GetBallRetainer() != player) { // must be set piece taker, then
      PlayerCommand command;
      command.desiredFunctionType = e_FunctionType_Movement;
      command.useDesiredMovement = true;
      command.useDesiredLookAt = true;

      AI_GetBallControlMovement(_mentalImage, CastPlayer(), player->GetDirectionVec(), walkVelocity, command.desiredDirection, command.desiredVelocityFloat, command.desiredLookAt);
      assert(command.desiredDirection.coords[2] == 0.0f);

      commandQueue.push_back(command);
      return;
    } else { // must be ball retainer
      PlayerCommand command;
      command.desiredFunctionType = e_FunctionType_Movement;
      command.useDesiredMovement = true;
      command.desiredDirection = (actionCommand.touchInfo.desiredDirection.GetLength() > 0.0f) ? actionCommand.touchInfo.desiredDirection.Get2D().GetNormalized(player->GetDirectionVec()) : (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec());
      command.desiredVelocityFloat = idleVelocity;
      command.useDesiredLookAt = true;
      command.desiredLookAt = player->GetPosition() + command.desiredDirection * 10.0f;
      commandQueue.push_back(command);
      if (team->GetController()->GetSetPieceType() == e_SetPiece_ThrowIn) {
        //printf("elizacontroller throw in doCommand (green pilon == target)\n");
        //SetGreenDebugPilon(command.desiredLookAt);
      }
      return;
    }

  }


  // default strategies for defensively, midfielders and offensively positioned players

  else if (match->IsInPlay() && !match->IsInSetPiece() && match->GetBallRetainer() != player && match->GetDesignatedPossessionPlayer() != player && CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) { //(match->GetDesignatedPossessionPlayer() != player || (CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK && !CastPlayer()->HasPossession()))) {
    if      (CastPlayer()->GetFormationEntry().role == e_PlayerRole_LB ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_CB ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_RB) {
      defenseStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    }
    else if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_DM ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_LM ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_CM ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_RM ||
             CastPlayer()->GetFormationEntry().role == e_PlayerRole_AM) {
      midfieldStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    }
    else if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_CF) {
      offenseStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    }

  }

  // dribble, pass, etcetera
  else if (match->IsInPlay() && !match->IsInSetPiece() && match->GetDesignatedPossessionPlayer() == player && team->GetHumanGamerCount() == 0 && CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    if (CastPlayer()->GetTimeNeededToGetToBall_ms() < 1000) {
      GetOnTheBallCommands(commandQueue, rawInputDirection, rawInputVelocityFloat);
    }
    extraHaste = false; // todo: extra haste when touch anim is queued?
  }

  else if (match->IsInPlay() && !match->IsInSetPiece() && CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK) {
    // keeper's mental image for deflections is near-instant; let's just call it premonition ;)
    const MentalImage *keeperMentalImage = _mentalImage;
    static_cast<GoalieDefaultStrategy*>(goalieStrategy)->CalculateIfBallIsBoundForGoal(keeperMentalImage);
    bool boundForGoal = static_cast<GoalieDefaultStrategy*>(goalieStrategy)->IsBallBoundForGoal();
    if (boundForGoal) manualMovement = true;
    if (CastPlayer() != match->GetDesignatedPossessionPlayer()) manualMovement = true;
    goalieStrategy->RequestInput(keeperMentalImage, manualMovementDirection, manualMovementVelocityFloat);

    float ballSpeed = match->GetBall()->GetMovement().GetLength();
    float oppDist = (_oppPlayer ? (_oppPlayer->GetPosition() - player->GetPosition()).GetLength() : 30.0f);

    if (boundForGoal || !hasUniquePossession || possessionAmount < 4.2f || ballSpeed > 2.0f || oppDist < 18.0f) {
      bool onlyPickupAnims = (!boundForGoal && ballSpeed < 10.5f && possessionAmount > 1.25f);
      _KeeperDeflectCommand(commandQueue, onlyPickupAnims);
    }

    if (CastPlayer()->GetTimeNeededToGetToBall_ms() < 1000 && match->GetDesignatedPossessionPlayer() == player) {
      GetOnTheBallCommands(commandQueue, rawInputDirection, rawInputVelocityFloat);
    }
  }

  bool forceMagnet = false;
  bool isSecondPresser = false;

  // collective team pressure (1st ball presser + 2nd passing-lane cover shadow)
  float ballDistance = _mentalImage->GetBallPrediction(0).Get2D().GetDistance(player->GetPosition());
  if (match->IsInPlay() && !match->IsInSetPiece() &&
      !teamHasBestPossession && match->GetDesignatedPossessionPlayer() != player && CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    if (team->GetController()->GetEndApplyTeamPressure_ms() > match->GetActualTime_ms()) {
      if (team->GetController()->GetTeamPressurePlayer() == player) {
        forceMagnet = true;
      } else if (team->GetController()->GetSecondTeamPressurePlayer() == player) {
        isSecondPresser = true;
      }
    }
  }

  bool byAnyMeans = false;
  if (match->IsInPlay() && !match->IsInSetPiece() && match->GetDesignatedPossessionPlayer() != player) {
    AIAdaptiveRegistry::Instance().AdaptOffBallPursuitAndLunge(
        match, _mentalImage, CastPlayer(), teamHasBestPossession, possessionAmount,
        rawInputDirection, rawInputVelocityFloat, forceMagnet, extraHaste, byAnyMeans);
  }

  _SetInput(rawInputDirection, rawInputVelocityFloat);
  if (rawInputDirection.GetLength() != 0) lastDesiredDirection = rawInputDirection; else
                                          lastDesiredDirection = player->GetDirectionVec();
  lastDesiredVelocity = rawInputVelocityFloat;

  if (match->IsInPlay() && !match->IsInSetPiece()) {

    // ball control?
    _BallControlCommand(commandQueue, false, pendingTakeOnKnockOn, false); // last param true == enable sticky run direction.

    // trap?
    _TrapCommand(commandQueue);

    // interfere?
    _InterfereCommand(commandQueue, byAnyMeans);

    // sliding?
    _SlidingCommand(commandQueue);
  }

  // movement
  if (!manualMovement && match->IsInPlay() && !match->IsInSetPiece()) {

    Player *opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();

    if (CastPlayer() != match->GetDesignatedPossessionPlayer()) {

      float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
      float huntDistanceThreshold = 10.0f + (1.0f - mindSet) * 10.0f; // 10 + .. * 10
      huntDistanceThreshold *= 0.5f * CastPlayer()->GetFatigueFactorInv() +
                               0.5f * (1.0f - NormalizedClamp(CastPlayer()->GetAverageVelocity(10), idleVelocity, sprintVelocity));
      huntDistanceThreshold *= 0.3f + GetMatch()->GetMatchDifficulty() * 0.7f;

      if (forceMagnet) {

        // Active 1st-defender ball press: close down along goal-side angle at decisive speed
        float closeDist = (opp->GetPosition() - CastPlayer()->GetPosition()).GetLength();
        float standOff = clamp(0.18f + closeDist * 0.04f, 0.18f, 0.65f);
        Vector3 defendPosition = GetDefendPosition(opp, standOff);
        Vector3 toDefend = defendPosition - CastPlayer()->GetPosition();
        inputDirection = toDefend.GetNormalized(CastPlayer()->GetDirectionVec());
        float pressSpeedBoost = 0.78f + 0.22f * GetMatch()->GetMatchDifficulty();
        inputVelocityFloat = clamp(toDefend.GetLength() * distanceToVelocityMultiplier * 1.25f * pressSpeedBoost, dribbleVelocity, sprintVelocity);
        if (closeDist < 4.8f && possessionAmount > 0.65f) {
          extraHaste = true;
        }

      } else if (isSecondPresser) {

        // 2nd-defender cover shadow: cut off the highest-danger short passing lane
        int targetID = team->GetController()->GetSecondTeamPressureTargetID();
        Player *passTargetOpp = (targetID >= 0) ? match->GetPlayer(targetID) : 0;
        if (passTargetOpp) {
          Vector3 oppPos = opp->GetPosition() + opp->GetMovement() * 0.18f;
          Vector3 targetPos = passTargetOpp->GetPosition() + passTargetOpp->GetMovement() * 0.22f;
          Vector3 coverSpot = oppPos * 0.36f + targetPos * 0.64f;
          // Stay slightly goal-side of the passing lane
          coverSpot.coords[0] += team->GetSide() * 0.8f;
          Vector3 toCover = coverSpot - CastPlayer()->GetPosition();
          if (toCover.GetLength() > 0.45f) {
            inputDirection = toCover.GetNormalized(inputDirection);
            inputVelocityFloat = clamp(toCover.GetLength() * distanceToVelocityMultiplier * 1.1f, walkVelocity * 0.8f, sprintVelocity);
          }
        }

      } else if (!teamHasBestPossession && CastPlayer()->GetManMarkingID() == -1 && ((opp->GetPosition() + opp->GetMovement() * 0.12f) - (CastPlayer()->GetPosition() + CastPlayer()->GetMovement() * 0.04f)).GetLength() < huntDistanceThreshold) { // defend player

          if (player == team->GetDesignatedTeamPossessionPlayer() && possessionAmount > 0.8f) {
            forceMagnet = true; // don't give up battles too easily
            extraHaste = true;
          }

          // or, defend an opponent, if we don't have anything better to do anyway

          Player *opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();

          int huntingPlayersNum = 2; // remember, this includes players with man marking id that are not controlled by this hunting code
          std::vector<Player*> closestPlayers;
          AI_GetClosestPlayers(team, opp->GetPosition() + opp->GetMovement() * 0.1f, false, closestPlayers, huntingPlayersNum);
          bool close = false;
          for (unsigned int i = 0; i < closestPlayers.size(); i++) {
            if (closestPlayers.at(i) == player) {
              close = true;
              break;
            }
          }

          if (close) {

            /*
            // 'easy off' method
            Vector3 defendPosition_easy = CastPlayer()->GetPosition();
            AddDefensiveComponent(defendPosition_easy, 1.0f, opp->GetID());
            inputDirection = (defendPosition - CastPlayer()->GetPosition()).GetNormalized(inputDirection);
            inputVelocityFloat = clamp((defendPosition - CastPlayer()->GetPosition()).GetLength() * distanceToVelocityMultiplier, 0.0f, sprintVelocity);
            //forceMagnet = true; // more aggressive!
            */

            // more 'hunting' method
            Vector3 defendPosition = GetDefendPosition(opp);
            if (NeedDefendingMovement(team->GetSide(), player->GetPosition(), defendPosition)) {
              inputDirection = (defendPosition - CastPlayer()->GetPosition()).GetNormalized(inputDirection);
              inputVelocityFloat = clamp((defendPosition - CastPlayer()->GetPosition()).GetLength() * distanceToVelocityMultiplier, 0.0f, sprintVelocity);
              forceMagnet = true; // more aggressive!
            }

          } // close

      } // else (!forceMagnet)

    } // !designated

    inputVelocityFloat = RangeVelocity(inputVelocityFloat); // emulate controller quantization

    _MovementCommand(commandQueue, forceMagnet, extraHaste);

    if (!commandQueue.empty()) {
      bool isCarrier = (match->GetDesignatedPossessionPlayer() == player);
      AIAdaptiveRegistry::Instance().AdaptFinalMovementCommand(
          match, _mentalImage, CastPlayer(), isCarrier, commandQueue.back());
    }

  } else { // keeper?

    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.desiredDirection = manualMovementDirection;
    assert(command.desiredDirection.coords[2] == 0.0f);
    command.desiredVelocityFloat = manualMovementVelocityFloat;
    command.useDesiredLookAt = true;
    command.desiredLookAt = player->GetPosition() + (_mentalImage->GetBallPrediction(40).Get2D() - player->GetPosition()).GetNormalized(0) * 10.0f;
    commandQueue.push_back(command);

  }
}

void ElizaController::Process() {
  PlayerController::Process();
}

Vector3 ElizaController::GetDirection() {
  return lastDesiredDirection;
}

float ElizaController::GetFloatVelocity() {
  return lastDesiredVelocity;
}

void ElizaController::LoadStrategies() {
  defenseStrategy = new DefaultDefenseStrategy(this);
  midfieldStrategy = new DefaultMidfieldStrategy(this);
  offenseStrategy = new DefaultOffenseStrategy(this);
  goalieStrategy = new GoalieDefaultStrategy(this);
}

bool PreRatingSortFunc(const PreRating &a, const PreRating &b) {
  return a.totalRating > b.totalRating;
}

bool SortPlayersDeepestFirst(Player *a, Player *b) {
  return a->GetPosition().coords[0] * a->GetTeam()->GetSide() < b->GetPosition().coords[0] * b->GetTeam()->GetSide();
}

float ElizaController::GetLazyVelocity(float desiredVelocityFloat) {

  // input is unclamped! use large values (less lazy, apparently we are more off-position)
  float adaptedDesiredVelocityFloat = desiredVelocityFloat;
  // don't use full overflow though
  if (adaptedDesiredVelocityFloat > sprintVelocity) adaptedDesiredVelocityFloat = sprintVelocity + (adaptedDesiredVelocityFloat - sprintVelocity) * 0.1f;

  float startLazinessDistance = 20.0f * (CastPlayer()->GetFatigueFactorInv() * 0.8f + 0.2f);
  float endLazinessDistance = 65.0f * (CastPlayer()->GetFatigueFactorInv() * 0.5f + 0.5f);

  Vector3 oppPos = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer()->GetPosition();
  float actionDistance = (player->GetPosition() - oppPos).GetLength();
  float teamPossession = clamp(GetFadingTeamPossessionAmount() - 0.5f, 0.0f, 1.0f);
  float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);

  // if mindSet is high (offensive), be lazy when teamPossession is low. and the other way round for defenders.
  // (midfielders are always half-lazy by role this way; pow() them a bit if this is undesired; this will of course wear them out more than backs and strikers)
  float lazinessByRole = mindSet + teamPossession * (1.0f - mindSet * 2.0f);
  float lazinessByPosition = NormalizedClamp(actionDistance, startLazinessDistance, endLazinessDistance);

  float lazyFactor = lazinessByPosition * (0.5f + lazinessByRole * 0.5f);
  float resultingVelocityFloat = adaptedDesiredVelocityFloat * (1.0f - lazyFactor);

  bool clampToDribble = false;
  if (desiredVelocityFloat >= dribbleVelocity) clampToDribble = true; // no standing still when initially undesired; it would get us further and further away from our desired position *edit: no longer true with the unclamped input
  if (clampToDribble && resultingVelocityFloat < dribbleVelocity) resultingVelocityFloat = dribbleVelocity;

  // short term fatigue/work rate shortage ;)
  // does not heed dribble clamp above, as to simulate players having to stop to catch their breath
  float breathLeftFactor = 1.0f - NormalizedClamp(CastPlayer()->GetAverageVelocity(10), idleVelocity, sprintVelocity);
  float workRate = CastPlayer()->GetStat("mental_workrate");
  breathLeftFactor = std::pow(breathLeftFactor, 0.8f - workRate * 0.2f);
  breathLeftFactor = clamp(breathLeftFactor * 1.2f, 0.0f, 1.0f); // make sure beginning of sprint is full speed
  breathLeftFactor = breathLeftFactor * lazyFactor + 1.0f * (1.0f - lazyFactor); // sometimes, we really need to force it
  resultingVelocityFloat = std::min(resultingVelocityFloat, sprintVelocity * breathLeftFactor);

  return resultingVelocityFloat;
}

Vector3 ElizaController::GetSupportPosition(const MentalImage *mentalImage, const Vector3 &basePosition) {

  float offenseWeight = 0.8f + (AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role)) * clamp((GetFadingTeamPossessionAmount() - 0.5f) * 2.0f, 0.0f, 1.0f);
  float distanceWeight = 1.0f;
  float passWeight = 0.8f;
  float movementWeight = 1.5f;
  float formationWeight = 1.6f + clamp((CastPlayer()->GetPosition() - mentalImage->GetBallPrediction(100).Get2D()).GetLength() / 30.0f, 0.0, 1.0f);
  float offsideWeight = 10.0f;

  Vector3 playerPos = player->GetPosition() + player->GetMovement() * 0.1f; // + physics slowness


  // get closest opponents

  std::vector<Player*> opponents;
  std::vector<PlayerImage> opponentImages;
  AI_GetClosestPlayers(match->GetTeam(abs(team->GetID() - 1)), playerPos, false, opponents, 4);
  for (unsigned int i = 0; i < opponents.size(); i++) {
    PlayerImage oppImage = mentalImage->GetPlayerImage(opponents.at(i)->GetID());
    oppImage.position += oppImage.movement * 0.15f;
    opponentImages.push_back(oppImage);
  }


  // add candidate positions

  std::vector<Vector3> candidatePositions;
  candidatePositions.push_back(playerPos);
  float step = (2 * pi) / 12.0f;
  Vector3 pos;
  for (float rad = 0; rad < 2 * pi; rad += step) {
    pos.coords[0] = cos(rad);
    pos.coords[1] = sin(rad);
    candidatePositions.push_back(playerPos + pos * dribbleVelocity);
    candidatePositions.push_back(playerPos + pos * walkVelocity);
    candidatePositions.push_back(playerPos + pos * sprintVelocity);
  }


  // rate candidates

  Vector3 goalPos = Vector3(-team->GetSide() * pitchHalfW, playerPos.coords[1] * 0.8f, 0); // y element is because goal is wider than 0
  float goalDistance = fabs((goalPos - playerPos).GetLength() - 5.0f); // and not all too close
  float ballDistance = (playerPos - mentalImage->GetBallPrediction(100).Get2D()).GetLength();
  float formationDistance = (basePosition - playerPos).GetLength();
  float offside = AI_GetOffsideLine(match, mentalImage, abs(team->GetID() - 1));

  float passRating; // are opponents blocking pass space to this position?

  float bestRating = 0;
  int bestCandidate = 0;

  std::vector<PreRating> preRatings;

  for (unsigned int i = 0; i < candidatePositions.size(); i++) {

    if (fabs(candidatePositions.at(i).coords[0]) < pitchHalfW && fabs(candidatePositions.at(i).coords[1]) < pitchHalfH) {
      PreRating preRating;
      preRating.candidateID = i;

      // offense rating
      preRating.offenseRating = clamp((goalDistance - fabs((goalPos - candidatePositions.at(i)).GetLength() - 5.0)) / sprintVelocity * 0.5 + 0.5, 0.0, 1.0);

      // distance rating
      float candidateBallDistance = (candidatePositions.at(i) - mentalImage->GetBallPrediction(100).Get2D()).GetLength();
      float desiredBallDistance = 10.0f;
      // too close is bad as well..
      if (candidateBallDistance < desiredBallDistance) candidateBallDistance = desiredBallDistance + fabs(candidateBallDistance - desiredBallDistance);
      float candidateBallVsBallDistance = (ballDistance - candidateBallDistance);

      preRating.distanceRating = clamp((candidateBallVsBallDistance / sprintVelocity) * 0.5f + 0.5f, 0.0f, 1.0f);

      // movement rating
      preRating.movementRating = 1.0 - clamp((player->GetMovement() - (candidatePositions.at(i) - playerPos)).GetLength() / (2 * sprintVelocity), 0.0, 1.0);

      // formation rating
      preRating.formationRating = clamp(((formationDistance - (basePosition - candidatePositions.at(i)).GetLength()) / sprintVelocity) * 0.5 + 0.5, 0.0, 1.0);

      // offside rating
      bool isOffside = false;
      if (candidatePositions.at(i).coords[0] * -team->GetSide() >= offside * -team->GetSide()) isOffside = true;
      preRating.offsideRating = isOffside ? 0.0 : 1.0;

      preRating.totalRating = preRating.offenseRating * offenseWeight + preRating.distanceRating * distanceWeight + preRating.movementRating * movementWeight + preRating.formationRating * formationWeight + preRating.offsideRating * offsideWeight;

      // small bonus for 'current position'
      if (i == 0) preRating.totalRating += 0.1f;

      preRatings.push_back(preRating);
    }
  }

  std::sort(preRatings.begin(), preRatings.end(), PreRatingSortFunc);

  for (unsigned int i = 0; i < preRatings.size() / 3; i++) {

    const PreRating &preRating = preRatings.at(i);
    int candidateID = preRating.candidateID;

    // pass rating
    passRating = 1.0;
    Vector3 checkPosition = candidatePositions.at(candidateID) + (mentalImage->GetBallPrediction(240).Get2D() - candidatePositions.at(candidateID)).GetNormalized(0) * 2.0f;
    for (unsigned int o = 0; o < opponentImages.size(); o++) {
      float oppDist = pow(clamp((opponentImages.at(o).position - checkPosition).GetLength() / 8.0f, 0.0f, 1.0f), 1.2f); // 0 .. 1 == worst, best
      if (oppDist < passRating) passRating = oppDist;
    }

    // total rating
    float totalRating = preRating.totalRating + passRating * passWeight;
    if (totalRating > bestRating) {
      bestRating = totalRating;
      bestCandidate = candidateID;
    }

  }

  Vector3 result = candidatePositions.at(bestCandidate);

  return result;
}

Vector3 ElizaController::GetSupportPosition_ForceField(const MentalImage *mentalImage, const Vector3 &basePosition, bool makeRun) {

  Player *designatedPlayer = team->GetDesignatedTeamPossessionPlayer();

  Vector3 currentPos = player->GetPosition() + CastPlayer()->GetMovement() * 0.1f; //basePosition;
  Vector3 mainManPos = designatedPlayer->GetPosition() + designatedPlayer->GetMovement() * 0.1f;

  std::vector<ForceSpot> forceField;


  // support position

  float dynamicMindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
  float ballDistance = (match->GetBall()->Predict(250).Get2D() - player->GetPosition()).GetLength();
  float ballDistanceX = fabs(match->GetBall()->Predict(250).coords[0] - player->GetPosition().coords[0]);

  bool forceNoOffside = true;

  float basePositionWeight = 0.7f;
  float overallWeight = 1.0f;
  float opponentRepelWeight = 0.3f * overallWeight;
  float teammateRepelWeight = 0.4f * overallWeight;
  float ballRepelWeight = 1.0f * overallWeight;
  float runWeight = 1.0f * overallWeight;
  float flockToPossessionPlayerWeight = 0.45f * overallWeight;

  float webScale = 0.75f;

  switch (CastPlayer()->GetDynamicFormationEntry().role) {
    case e_PlayerRole_CB:
    case e_PlayerRole_LB:
    case e_PlayerRole_RB:
      opponentRepelWeight *= 2.2f;
      break;
    case e_PlayerRole_DM:
      opponentRepelWeight *= 2.0f;
      break;
    case e_PlayerRole_CM:
    case e_PlayerRole_LM:
    case e_PlayerRole_RM:
      opponentRepelWeight *= 1.6f;
      break;
    case e_PlayerRole_AM:
      opponentRepelWeight *= 1.2f;
      break;
    case e_PlayerRole_CF:
      opponentRepelWeight *= 1.0f;
      break;
    default:
      break;
  }

  float offsideX = AI_GetOffsideLine(match, _mentalImage, abs(team->GetID() - 1), 240);
  float adaptedMakeRun = makeRun;

  // actual base position
  {
    ForceSpot spot;
    spot.origin = basePosition;

    // let left and right flank flow forwards and backwards every period_sec seconds, for some dynamics
    // except for one close player, who should always run forward for support
    Player *forwardSupportPlayer = team->GetController()->GetForwardSupportPlayer();
    if (player == forwardSupportPlayer) {
      spot.origin.coords[0] += -team->GetSide() * (0.3f + 0.7f * dynamicMindSet) * 12.0f;
    } else {
      /* sine version
      float amount = 8.0f;//14.0f;//8.0f;
      float period_sec = 7.0f;
      float phase_offset = NormalizedClamp(currentPos.coords[1], -pitchHalfH, pitchHalfH);
      amount *= sin(match->GetActualTime_ms() * 0.001f * ((1.0f / period_sec) * 2.0f * pi) + (phase_offset * 2.0f * pi)) * 0.5f + 0.5f;
      float delta = -team->GetSide() * curve(dynamicMindSet, 0.5f) * (1.0f - pow(NormalizedClamp(ballDistanceX, 1.0f, 30.0f), 1.5f)) * amount; // offensive crew
      spot.origin.coords[0] += delta;
      */
      // lane version
      float amount = 22.0f;
      float laneY = -signSide(mainManPos.coords[1]) * 8.0f;
      amount *= curve(1.0f - NormalizedClamp(fabs(laneY - currentPos.coords[1]), 0.0f, 30.0f), 1.0f);
      float delta = -team->GetSide() * std::pow(dynamicMindSet, 1.5f) * amount;
      spot.origin.coords[0] += delta;
    }

    spot.magnetType = e_MagnetType_Attract;
    spot.decayType = e_DecayType_Constant;
    spot.power = 1.0f * basePositionWeight;
    // the farther away from this position, the more we are attracted to it
    spot.power *= 0.3f + 0.7f * NormalizedClamp((spot.origin - currentPos).GetLength(), 0.0f, 20.0f);
    forceField.push_back(spot);
  }

  if (adaptedMakeRun) {
    ForceSpot spot;
    spot.origin = Vector3(-team->GetSide() * pitchHalfW, currentPos.coords[1] * 0.5f, 0.0f);//player->GetDirectionVec() * 100.0f;// / distanceToVelocityMultiplier;
    spot.magnetType = e_MagnetType_Attract;
    spot.decayType = e_DecayType_Constant;
    spot.power = 2.0f * runWeight;
    forceField.push_back(spot);
  }

  // stay away from opponents
  std::vector<Player*> opponents;
  AI_GetClosestPlayers(match->GetTeam(abs(team->GetID() - 1)), mainManPos * 0.3f + currentPos + 0.7f, false, opponents, 3);
  for (unsigned int i = 0; i < opponents.size(); i++) {
    const PlayerImage &oppImg = mentalImage->GetPlayerImage(opponents.at(i)->GetID());
    ForceSpot spot;
    Vector3 oppPos = oppImg.position + oppImg.movement * 0.1f;
    spot.origin = oppPos + (oppPos - mainManPos).GetNormalized(0) * 2.0f; // anti-magnet behind opponent, because the pass-way must be cleared
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 1.0f * opponentRepelWeight;
    spot.scale = 5.0f;
    if (adaptedMakeRun) {
      spot.scale = 2.0f;
      spot.power *= 0.5f;
    }
    spot.exp = 0.7f;
    forceField.push_back(spot);
  }

  // stay away from teammates
  if (team->GetFadingTeamPossessionAmount() >= 1.02f) {
    std::vector<Player*> players;
    AI_GetClosestPlayers(team, currentPos, false, players, 6);
    for (unsigned int i = 0; i < players.size(); i++) {
      if (players.at(i) != CastPlayer()) {
        const PlayerImage &mateImg = mentalImage->GetPlayerImage(players.at(i)->GetID());
        ForceSpot spot;
        spot.origin = mateImg.position + mateImg.movement * 0.1f;
        spot.magnetType = e_MagnetType_Repel;
        spot.decayType = e_DecayType_Variable;
        spot.power = 1.0f * teammateRepelWeight;
        spot.scale = 14.0f * webScale;
        spot.exp = 1.0f;
        forceField.push_back(spot);
      }
    }
  }

  // stay away from ball (to not get in the way of passes and possessionplayer)
  if (CastPlayer() != designatedPlayer && team->GetFadingTeamPossessionAmount() >= 1.06f) {
    ForceSpot spot;
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 1.0f * ballRepelWeight;
    spot.scale = 2.0f;
    spot.exp = 0.5f;
    spot.origin = mentalImage->GetBallPrediction(200).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(350).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(500).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(650).Get2D();
    forceField.push_back(spot);
  }

  if (CastPlayer() != designatedPlayer) {

    // attract to teammate in possession
    {
      ForceSpot spot;
      spot.origin = mainManPos;
      spot.magnetType = e_MagnetType_Attract;
      spot.decayType = e_DecayType_Variable;
      spot.power = 1.0f * flockToPossessionPlayerWeight;
      spot.scale = 28.0f * webScale;
      spot.exp = 1.0f;
      forceField.push_back(spot);
    }

    // ..yet not too close
    {
      ForceSpot spot;
      spot.origin = mainManPos;
      spot.magnetType = e_MagnetType_Repel;
      spot.decayType = e_DecayType_Variable;
      spot.power = 1.0f * flockToPossessionPlayerWeight;
      spot.scale = 16.0f * webScale;
      spot.exp = 1.0f;
      forceField.push_back(spot);
    }
  }

  Vector3 forceFieldPosition = currentPos + AI_GetForceFieldMovement(forceField, currentPos, 7);//8);

  forceFieldPosition = AIAdaptiveRegistry::Instance().AdaptSupportPositionCorridors(
      match, mentalImage, CastPlayer(), designatedPlayer, forceFieldPosition);

  float margin = 0.08f;
  if (forceNoOffside) if (forceFieldPosition.coords[0] * -team->GetSide() > (offsideX * -team->GetSide()) - margin) forceFieldPosition.coords[0] = offsideX - (margin * -team->GetSide());

  forceFieldPosition.coords[0] = clamp(forceFieldPosition.coords[0], -pitchHalfW, pitchHalfW);
  forceFieldPosition.coords[1] = clamp(forceFieldPosition.coords[1], -pitchHalfH, pitchHalfH);

  return forceFieldPosition;
}

void ElizaController::Reset() {
  lastDesiredDirection = Vector3(0);
  lastDesiredVelocity = 0;
  pendingTakeOnKnockOn = false;
}

void ElizaController::GetOnTheBallCommands(std::vector<PlayerCommand> &commandQueue, Vector3 &rawInputDirection, float &rawInputVelocityFloat) {

  float movementDiff = NormalizedClamp((match->GetBall()->GetMovement() - CastPlayer()->GetMovement()).GetLength(), 0.0f, 10.0f);
  float oneTouchIsHard = (movementDiff - CastPlayer()->GetStat("technical_shortpass") * movementDiff * 0.8f) * 0.35f;

  std::vector<PlayerImage> opponentPlayerImages;
  _mentalImage->GetTeamPlayerImages(abs(team->GetID() - 1), -1, opponentPlayerImages);

  signed int side = team->GetSide(); // +1 defends +X (attacks -X), -1 defends -X (attacks +X)
  float attSign = (float)(-side);
  Vector3 myPos = CastPlayer()->GetPosition();
  Vector3 oppGoalCenter(pitchHalfW * attSign, 0.0f, 0.0f);

  // Expected Threat (xT) surface: rewards pitch progression, central/half-space access, and penalty-box proximity
  auto calcExpectedThreat = [&](const Vector3 &pos) -> float {
    float normX = clamp((pos.coords[0] * attSign + pitchHalfW) / (pitchHalfW * 2.0f), 0.0f, 1.0f);
    float normY = clamp(fabs(pos.coords[1]) / pitchHalfH, 0.0f, 1.0f);
    float centrality = 1.0f - std::pow(normY, 1.35f) * 0.55f;
    float baseProgression = std::pow(normX, 1.75f) * centrality;
    float boxDist = (pos - Vector3((pitchHalfW - 10.0f) * attSign, 0.0f, 0.0f)).GetLength();
    float boxThreat = std::pow(1.0f - NormalizedClamp(boxDist, 0.0f, 26.0f), 1.6f) * 0.55f;
    return clamp(baseProgression * 0.72f + boxThreat, 0.0f, 1.2f);
  };

  float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
  float longPossessionFactor = std::pow(NormalizedClamp(CastPlayer()->GetPossessionDuration_ms(), 0, 4200), 1.6f);
  const TacticalPlayerSituation &sit = CastPlayer()->GetTacticalSituation();

  // Compute pressure on ball carrier & separate outfield blockers from the Goalkeeper
  float closestOppDist = 30.0f;
  float closestOutfieldOppDist = 30.0f;
  int outfieldAheadInLaneCount = 0;
  Vector3 keeperPos = oppGoalCenter;
  Vector3 keeperRawPos = oppGoalCenter;
  Vector3 keeperVel(0, 0, 0);
  bool foundOppKeeper = false;
  std::vector<PlayerImage> outfieldOpponentImages;
  outfieldOpponentImages.reserve(opponentPlayerImages.size());

  for (unsigned int o = 0; o < opponentPlayerImages.size(); o++) {
    Vector3 predOpp = opponentPlayerImages.at(o).position + opponentPlayerImages.at(o).movement * 0.18f;
    float d = (predOpp - (myPos + CastPlayer()->GetMovement() * 0.12f)).GetLength();
    if (d < closestOppDist) closestOppDist = d;
    if (opponentPlayerImages.at(o).dynamicFormationEntry.role == e_PlayerRole_GK) {
      keeperRawPos = opponentPlayerImages.at(o).position;
      keeperVel = opponentPlayerImages.at(o).movement;
      keeperPos = keeperRawPos + keeperVel * 0.24f;
      foundOppKeeper = true;
    } else {
      outfieldOpponentImages.push_back(opponentPlayerImages.at(o));
      if (d < closestOutfieldOppDist) closestOutfieldOppDist = d;
      float aheadX = (predOpp.coords[0] - myPos.coords[0]) * attSign;
      if (aheadX > 0.2f && fabs(predOpp.coords[1] - myPos.coords[1]) < 4.5f) {
        outfieldAheadInLaneCount++;
      }
    }
  }
  float underPressure = 1.0f - NormalizedClamp(closestOutfieldOppDist, 1.4f, 8.5f);

  // 1. Evaluate Dribble Utility (carrying into open space) + Player-Centric Adaptive Profile
  PlayerDispositionProfile disposition = AIAdaptiveRegistry::Instance().BuildDispositionProfile(match, CastPlayer());
  DefenderMomentumInfo defMomentum = AIAdaptiveRegistry::Instance().AnalyzeDefenderSituations(match, CastPlayer(), opponentPlayerImages);

  float myXT = calcExpectedThreat(myPos);
  Vector3 projectedCarryPos = myPos + Vector3(attSign * 7.5f, -myPos.coords[1] * 0.15f, 0.0f);
  float carryXT = calcExpectedThreat(projectedCarryPos);
  float distToGoalNow = (oppGoalCenter - myPos).GetLength();
  float distToKeeperNow = (keeperPos - myPos).GetLength();
  // Prevent "walking the ball into the net": carrying straight into a closing GK loses value rapidly inside 16m
  if (distToGoalNow < 16.5f) {
    float gkCrowdPenalty = (1.0f - NormalizedClamp(distToKeeperNow, 2.8f, 9.5f)) * 0.42f +
                           (1.0f - NormalizedClamp(distToGoalNow, 6.5f, 16.5f)) * 0.28f;
    carryXT = std::max(0.15f, carryXT - gkCrowdPenalty);
  }
  float dribbleSkill = 0.5f * CastPlayer()->GetStat("technical_dribble") + 0.5f * CastPlayer()->GetStat("physical_velocity");
  float dribbleOffenseTactic = team->GetTeamData()->GetTactics().userProperties.GetReal("dribble_offensiveness", 0.5f);

  float dribbleUtility = myXT * 0.45f +
                         carryXT * (0.28f + 0.24f * sit.forwardSpaceRating) +
                         sit.forwardSpaceRating * (0.22f + 0.14f * dribbleSkill + 0.08f * dribbleOffenseTactic) +
                         sit.spaceRating * 0.12f -
                         underPressure * (0.26f - 0.10f * dribbleSkill) -
                         longPossessionFactor * 0.16f;
  if (distToGoalNow < 15.5f && outfieldAheadInLaneCount == 0) {
    // 1v1 with Keeper: carrying further shrinks shooting angle around GK's feet!
    dribbleUtility -= (1.0f - NormalizedClamp(distToKeeperNow, 3.0f, 10.5f)) * 0.30f;
  }
  if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK) {
    dribbleUtility -= 0.45f; // keeper prefers distributing rather than dribbling out
  }

  // 2. Evaluate Pass Candidates (Short Pass, Through Ball, Cross, Switch of Play)
  float offsideX = AI_GetOffsideLine(match, _mentalImage, abs(team->GetID() - 1), 240);
  Player *attackingRunner = (team->GetController()->GetEndApplyAttackingRun_ms() > match->GetActualTime_ms())
                                ? team->GetController()->GetAttackingRunPlayer()
                                : 0;
  Player *supportRunner = team->GetController()->GetForwardSupportPlayer();

  std::vector<Player*> mates;
  team->GetActivePlayers(mates);

  Player *bestPassTarget = 0;
  e_FunctionType bestPassType = e_FunctionType_ShortPass;
  float bestPassUtility = -1.0f;
  float bestPassOdds = 0.0f;

  float minSafePassOdds = clamp(0.24f - mindSet * 0.10f - underPressure * 0.08f, 0.08f, 0.30f);

  Vector3 directionVec = CastPlayer()->GetDirectionVec();

  for (unsigned int i = 0; i < mates.size(); i++) {
    Player *mate = mates.at(i);
    if (mate == CastPlayer()) continue;

    PlayerImage mateImg = _mentalImage->GetPlayerImage(mate->GetID());
    Vector3 matePos = mateImg.position;
    Vector3 mateVel = mateImg.movement;
    float passDist = (matePos - myPos).GetLength();
    if (passDist < 3.0f || passDist > 58.0f) continue;

    // Offside check for forward passes (using MentalImage perceived position & velocity)
    bool mateAheadOfBall = (matePos.coords[0] * attSign > (myPos.coords[0] * attSign + 0.3f));
    float predictedMateX = (matePos.coords[0] + mateVel.coords[0] * 0.26f);
    if (mateAheadOfBall && predictedMateX * attSign > offsideX * attSign - 0.25f) {
      continue; // offside!
    }

    // Original baseline mechanical body-direction penalty (front 180 deg = 1.0, rear 180 deg = 0.3 .. 1.0)
    float bodyDirPenalty = directionVec.GetDotProduct((matePos - myPos).GetNormalized(Vector3(attSign, 0, 0)));
    bodyDirPenalty = clamp(bodyDirPenalty, -1.0f, 0.0f) + 1.0f;
    bodyDirPenalty = bodyDirPenalty * 0.7f + 0.3f;

    const TacticalPlayerSituation &mateSit = mate->GetTacticalSituation();
    float mateXT = calcExpectedThreat(matePos + mateVel * 0.35f);
    if (mate->GetFormationEntry().role == e_PlayerRole_GK) {
      mateXT *= 0.45f; // only recycle to GK when heavily pressed with no forward options
    }

    float oddsShort = (passDist < 38.0f) ? (_GetPassingOdds(mate, e_FunctionType_ShortPass, opponentPlayerImages) * bodyDirPenalty) : 0.0f;
    float oddsLong  = (passDist > 7.0f && passDist < 46.0f) ? (_GetPassingOdds(mate, e_FunctionType_LongPass, opponentPlayerImages) * bodyDirPenalty) : 0.0f;
    float oddsHigh  = (passDist > 13.0f) ? (_GetPassingOdds(mate, e_FunctionType_HighPass, opponentPlayerImages) * bodyDirPenalty) : 0.0f;

    // Context bonuses for specialized pass types
    bool isRunner = (mate == attackingRunner || mate == supportRunner ||
                     (mateVel.coords[0] * attSign > 3.2f && AI_GetMindSet(mate->GetDynamicFormationEntry().role) >= 0.5f));
    bool isThroughBallWindow = isRunner && mateSit.forwardSpaceRating > 0.45f &&
                               (predictedMateX + attSign * passDist * 0.16f) * attSign < (offsideX * attSign + 1.5f);

    bool isWideCrossWindow = (myPos.coords[0] * attSign > pitchHalfW * 0.56f && fabs(myPos.coords[1]) > 13.5f &&
                              matePos.coords[0] * attSign > pitchHalfW * 0.68f && fabs(matePos.coords[1]) < 12.5f);

    bool isSwitchPlayWindow = (fabs(matePos.coords[1] - myPos.coords[1]) > 22.0f &&
                               mateSit.spaceRating > 0.62f && sit.spaceRating < 0.52f);

    // Score each viable pass type for this teammate
    float candOdds = oddsShort;
    e_FunctionType candType = e_FunctionType_ShortPass;
    float typeTacticalBonus = 0.0f;

    float shortScore = oddsShort * 0.55f + mateSit.spaceRating * 0.15f;
    float longScore  = oddsLong * 0.55f + mateSit.forwardSpaceRating * 0.22f + (isThroughBallWindow ? 0.18f : 0.0f);
    float highScore  = oddsHigh * 0.52f + mateSit.spaceRating * 0.16f + (isWideCrossWindow ? 0.26f : 0.0f) + (isSwitchPlayWindow ? 0.22f : 0.0f);

    if (longScore > shortScore && longScore >= highScore && oddsLong > minSafePassOdds) {
      candOdds = oddsLong;
      candType = e_FunctionType_LongPass;
      typeTacticalBonus = isThroughBallWindow ? 0.14f : 0.04f;
    } else if (highScore > shortScore && oddsHigh > minSafePassOdds * 0.85f) {
      candOdds = oddsHigh;
      candType = e_FunctionType_HighPass;
      typeTacticalBonus = (isWideCrossWindow ? 0.18f : 0.0f) + (isSwitchPlayWindow ? 0.14f : 0.0f);
    } else {
      candOdds = oddsShort;
      candType = e_FunctionType_ShortPass;
    }

    if (candOdds < minSafePassOdds) continue;

    // Ideal passing distance curve (10m..26m is crispest)
    float distQuality = 1.0f - fabs(passDist - 17.0f) / 48.0f;
    distQuality = clamp(distQuality, 0.55f, 1.0f);

    float mateUtility = mateXT * 0.78f +
                        mateSit.spaceRating * 0.18f +
                        mateSit.forwardSpaceRating * 0.16f +
                        candOdds * (0.34f + 0.10f * (1.0f - mindSet)) * distQuality +
                        typeTacticalBonus -
                        oneTouchIsHard;

    // Discourage pointless sideways/backward passes when carrier already has open space ahead
    if (mateXT < myXT - 0.03f && underPressure < 0.35f) {
      mateUtility -= 0.14f;
    }

    PassCandidateContext passCtx;
    passCtx.carrier = CastPlayer();
    passCtx.target = mate;
    passCtx.passType = candType;
    passCtx.passDist = passDist;
    passCtx.passOdds = candOdds;
    passCtx.underPressure = underPressure;
    passCtx.isThroughBall = isThroughBallWindow;
    passCtx.isSwitchPlay = isSwitchPlayWindow;
    passCtx.isWideCross = isWideCrossWindow;
    mateUtility = AIAdaptiveRegistry::Instance().AdaptPassCandidateUtility(passCtx, disposition, mateUtility);

    if (mateUtility > bestPassUtility) {
      bestPassUtility = mateUtility;
      bestPassTarget = mate;
      bestPassType = candType;
      bestPassOdds = candOdds;
    }
  }

  // 3. Evaluate Shot Utility Directly via MentalImage (360 Omnidirectional Perception, No Vision Cone)
  float distToGoal = (oppGoalCenter - myPos).GetLength();
  bool shouldShoot = false;
  bool is1v1KeeperSituation = false;
  float bestShotUtility = -1.0f;
  float bestShotY = 0.0f;

  if (distToGoal < 29.0f && myPos.coords[0] * attSign > pitchHalfW * 0.32f &&
      CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    Vector3 toGoalCenter = (oppGoalCenter - myPos).GetNormalized(Vector3(attSign, 0, 0));
    float angleCos = clamp(toGoalCenter.coords[0] * attSign, 0.05f, 1.0f);
    float shotAcc = CastPlayer()->GetStat("technical_shot");
    float shotPower = CastPlayer()->GetStat("physical_shotpower");
    float shotSkill = 0.55f * shotAcc + 0.45f * shotPower;

    const float targetCols[5] = {-3.30f, -1.80f, 0.0f, 1.80f, 3.30f};
    float bestWindowQuality = 0.0f;
    float bestCorridorClearance = 0.0f;

    for (int c = 0; c < 5; c++) {
      float colY = targetCols[c];
      Vector3 goalPt(pitchHalfW * attSign, colY, 0.0f);
      Vector3 ray = (goalPt - myPos);
      float rayLen = std::max(1.0f, ray.Get2D().GetLength());
      Vector3 rayDir = ray.Get2D().GetNormalized(Vector3(attSign, 0, 0));

      // Outfield blocker clearance from MentalImage
      float corridorClear = 1.0f;
      for (size_t o = 0; o < outfieldOpponentImages.size(); o++) {
        Vector3 oppPred = outfieldOpponentImages[o].position.Get2D() + outfieldOpponentImages[o].movement.Get2D() * 0.16f;
        Vector3 rel = oppPred - myPos.Get2D();
        float along = rel.GetDotProduct(rayDir);
        if (along <= 0.35f || along >= rayLen - 0.4f) continue;
        Vector3 closestPt = myPos.Get2D() + rayDir * along;
        float perp = (oppPred - closestPt).GetLength();
        float blockRadius = 0.85f + NormalizedClamp(along, 1.5f, 18.0f) * 0.55f;
        if (perp < blockRadius) {
          float clearHere = clamp(perp / blockRadius, 0.0f, 1.0f);
          corridorClear = std::min(corridorClear, clearHere * clearHere);
        }
      }

      // Goalkeeper angle coverage from MentalImage
      float gkOpenness = 1.0f;
      if (foundOppKeeper) {
        Vector3 relGK = keeperPos.Get2D() - myPos.Get2D();
        float gkAlong = clamp(relGK.GetDotProduct(rayDir), 0.5f, rayLen);
        Vector3 rayAtGK = myPos.Get2D() + rayDir * gkAlong;
        float gkPerp = (keeperPos.Get2D() - rayAtGK).GetLength();
        float ballFlight_s = gkAlong / std::max(18.0f, 22.0f + shotPower * 9.0f);
        float gkReach = 0.95f + ballFlight_s * 3.1f;
        gkOpenness = clamp((gkPerp - 0.35f) / std::max(0.65f, gkReach), 0.10f, 1.0f);
      }

      float cornerBonus = (fabs(colY) > 2.2f) ? 0.06f : 0.0f;
      float colQuality = corridorClear * (0.35f + 0.65f * gkOpenness) + cornerBonus;
      if (colQuality > bestWindowQuality) {
        bestWindowQuality = colQuality;
        bestCorridorClearance = corridorClear;
        bestShotY = colY;
      }
    }

    is1v1KeeperSituation = (distToGoal < 18.0f && angleCos > 0.42f &&
                            outfieldAheadInLaneCount == 0 &&
                            bestCorridorClearance > 0.55f);

    float rangeFactor = 1.0f - NormalizedClamp(distToGoal, 7.0f, 27.5f);
    float clinical1v1Bonus = 0.0f;
    if (is1v1KeeperSituation) {
      clinical1v1Bonus = 0.40f + 0.14f * shotAcc +
                         (1.0f - NormalizedClamp(distToKeeperNow, 2.8f, 11.0f)) * 0.16f;
    } else if (distToGoal < 15.5f && angleCos > 0.48f && bestCorridorClearance > 0.50f) {
      clinical1v1Bonus = 0.28f + 0.10f * shotAcc;
    }

    bestShotUtility = bestWindowQuality * (0.58f + 0.48f * rangeFactor) * (0.45f + 0.55f * angleCos) +
                      shotSkill * 0.18f +
                      clinical1v1Bonus;

    float minShotThreshold = 0.52f - shotSkill * 0.08f;
    bool passIsBoxBreakaway = (bestPassTarget != 0) &&
                              (_mentalImage->GetPlayerImage(bestPassTarget->GetID()).position.coords[0] * attSign > pitchHalfW - 15.5f);
    float effectivePassComp = (is1v1KeeperSituation && !passIsBoxBreakaway)
                                  ? (bestPassUtility * 0.80f)
                                  : bestPassUtility;
    float passMargin = is1v1KeeperSituation ? 0.12f : 0.05f;
    float dribbleMargin = is1v1KeeperSituation ? -0.10f : 0.01f;

    if (bestShotUtility > minShotThreshold &&
        bestShotUtility > dribbleUtility + dribbleMargin &&
        bestShotUtility > effectivePassComp - passMargin) {
      shouldShoot = true;
    }
  }

  // 4. Arbitrate On-Ball Intent (Shot vs Pass vs 1v1 Take-On vs Hold/Shield vs Open Carry)
  AI_GetBestDribbleMovement(match, player->GetID(), _mentalImage, rawInputDirection, rawInputVelocityFloat, team->GetTeamData()->GetTactics());

  OnBallArbitrationInput arbIn;
  arbIn.match = match;
  arbIn.carrier = CastPlayer();
  arbIn.mentalImage = _mentalImage;
  arbIn.opponentImages = &opponentPlayerImages;
  arbIn.baselineDribbleUtility = dribbleUtility;
  arbIn.bestPassUtility = bestPassUtility;
  arbIn.bestPassOdds = bestPassOdds;
  arbIn.bestPassTarget = bestPassTarget;
  arbIn.bestPassType = bestPassType;
  arbIn.baselineShouldShoot = shouldShoot;
  arbIn.is1v1Keeper = is1v1KeeperSituation;
  arbIn.bestShotUtility = bestShotUtility;
  arbIn.bestShotTargetY = bestShotY;
  arbIn.underPressure = underPressure;
  arbIn.baselineDribbleDir = rawInputDirection;
  arbIn.baselineDribbleVel = rawInputVelocityFloat;

  OnBallArbitrationResult arbOut = AIAdaptiveRegistry::Instance().ArbitrateOnBallIntent(arbIn, disposition, defMomentum);
  rawInputDirection = arbOut.adaptedDribbleDir;
  rawInputVelocityFloat = arbOut.adaptedDribbleVel;
  pendingTakeOnKnockOn = arbOut.requestKnockOn;

  // 5. Defensive Panic / Clearance Under Extreme Box Danger (moderated by player composure)
  if (!arbOut.executeShot && !arbOut.suppressPanicClearance && mindSet < 0.25f) {
    float panicProneness = 1.0f - mindSet * 2.0f;
    float ownGoalCloseness = 1.0f - NormalizedClamp((myPos - Vector3(pitchHalfW * side, 0, 0)).GetLength(), 2.0f, 16.0f);
    if (CastPlayer()->GetDynamicFormationEntry().role != e_PlayerRole_GK) {
      if ((bestPassTarget == 0 || bestPassOdds < panicProneness * ownGoalCloseness) &&
          possessionAmount < 0.88f + panicProneness * ownGoalCloseness * 0.75f &&
          underPressure > 0.55f) {
        _AddPanicPass(commandQueue);
      }
    } else if (possessionAmount < 2.8f && underPressure > 0.45f) {
      _AddPanicPass(commandQueue);
    }
  }

  // 6. Commit Single Coherent Action (Shot OR Best Pass OR Adapted Dribble/Take-On/Shield)
  if (arbOut.executeShot) {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Shot;
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    // Match current stride velocity on breakaways so shot animation triggers immediately without 2-touch deceleration!
    float curVel = CastPlayer()->GetFloatVelocity();
    command.desiredVelocityFloat = (distToGoal < 15.5f && curVel < walkVelocity + 1.0f)
                                       ? dribbleVelocity
                                       : ClampVelocity(std::max(curVel, dribbleVelocity));
    float jitterScale = is1v1KeeperSituation ? 0.46f : 0.68f;
    float aimJitter = (1.0f - CastPlayer()->GetStat("technical_shot")) * jitterScale;
    Vector3 aimPoint((pitchHalfW + 1.0f) * attSign, bestShotY + random(-aimJitter, aimJitter), 0.0f);
    Vector3 origin = myPos + CastPlayer()->GetMovement() * 0.18f;
    command.touchInfo.desiredDirection = (aimPoint - origin).GetNormalized(Vector3(attSign, 0, 0));
    command.touchInfo.desiredDirection = (command.touchInfo.desiredDirection * 0.88f + -CastPlayer()->GetDirectionVec() * (CastPlayer()->GetFloatVelocity() / sprintVelocity) * 0.12f).GetNormalized(Vector3(attSign, 0, 0));
    command.touchInfo.autoDirectionBias = 1.0f;
    if (distToGoal < 14.5f) {
      // Composed low/placed finish in 1v1 so shots don't sky over the bar
      command.touchInfo.desiredPower = clamp(random(0.50f, 0.70f), 0.46f, 0.74f);
    } else {
      float normDist = NormalizedClamp(distToGoal, 14.5f, 36.0f);
      float pwrStat = CastPlayer()->GetStat("physical_shotpower");
      command.touchInfo.desiredPower = clamp(random(0.72f + normDist * 0.18f, 0.84f + normDist * 0.13f + pwrStat * 0.04f), 0.68f, 0.98f);
    }
    commandQueue.push_back(command);
  } else if (arbOut.executePass && bestPassTarget != 0) {
    _AddPass(commandQueue, bestPassTarget, bestPassType);
  }
}

void ElizaController::_AddPass(std::vector<PlayerCommand> &commandQueue, Player *target, e_FunctionType passType) {
  PlayerCommand command;
  command.desiredFunctionType = passType;
  command.useDesiredMovement = false;
  command.useDesiredLookAt = false;
  command.touchInfo.targetPlayer = 0;
  command.touchInfo.forcedTargetPlayer = target;
  command.touchInfo.inputDirection = Vector3(0);
  command.touchInfo.inputPower = 0;
  command.touchInfo.autoDirectionBias = 1.0f;
  command.touchInfo.autoPowerBias = 1.0f;
  AI_GetPass(CastPlayer(), passType, command.touchInfo.inputDirection, command.touchInfo.inputPower, command.touchInfo.autoDirectionBias, command.touchInfo.autoPowerBias, command.touchInfo.desiredDirection, command.touchInfo.desiredPower, command.touchInfo.targetPlayer, command.touchInfo.forcedTargetPlayer);
  commandQueue.push_back(command);
}

void ElizaController::_AddPanicPass(std::vector<PlayerCommand> &commandQueue) {

  int yside = signSide(player->GetDirectionVec().coords[1]); // > 0 ? 1 : -1;
  Vector3 sensibleAwayDir = ((player->GetDirectionVec() * Vector3(0.8f, 1.0f, 0.0f)).GetNormalized() + Vector3(-team->GetSide() * 0.7f, yside * 0.5f, 0)).GetNormalized(0) + Vector3(0, 0, 0.3f);
  sensibleAwayDir.Normalize(player->GetDirectionVec());

  PlayerCommand command;
  command.useDesiredMovement = false;
  command.useDesiredLookAt = false;

  command.touchInfo.inputDirection = sensibleAwayDir;
  command.touchInfo.desiredDirection = sensibleAwayDir;
  command.touchInfo.autoDirectionBias = 0.0f;
  command.touchInfo.autoPowerBias = 0.0f;

  command.desiredFunctionType = e_FunctionType_HighPass;
  command.touchInfo.inputPower = 0.7f;
  command.touchInfo.desiredPower = 0.7f;
  commandQueue.push_back(command);

  command.desiredFunctionType = e_FunctionType_Shot;
  command.touchInfo.inputPower = 0.6f;
  command.touchInfo.desiredPower = 0.6f;
  commandQueue.push_back(command);

  command.desiredFunctionType = e_FunctionType_LongPass;
  command.touchInfo.inputPower = 0.8f;
  command.touchInfo.desiredPower = 0.8f;
  commandQueue.push_back(command);
}

float ElizaController::_GetPassingOdds(Player *targetPlayer, e_FunctionType passType, const std::vector<PlayerImage> &opponentPlayerImages, float ballVelocityMultiplier) {

  float initialTargetDistance = (targetPlayer->GetPosition() - player->GetPosition()).GetLength();
  if (passType == e_FunctionType_HighPass && initialTargetDistance < 10.0f) return 0.0f;
  float estimatedTime_sec = 0.7f + initialTargetDistance * 0.03f;

  Vector3 target = targetPlayer->GetPosition() + targetPlayer->GetMovement() * clamp(estimatedTime_sec, 0.0f, 0.5f); // time needed to brake
  if (passType == e_FunctionType_LongPass) target += Vector3(-team->GetSide() * initialTargetDistance * 0.2f, 0, 0);

  return _GetPassingOdds(target, passType, opponentPlayerImages, ballVelocityMultiplier);
}

float ElizaController::_GetPassingOdds(const Vector3 &target, e_FunctionType passType, const std::vector<PlayerImage> &opponentPlayerImages, float ballVelocityMultiplier) {

  float secondScale = 1.0f; // how many seconds of range to measure danger in

  Vector3 origin = player->GetPosition() + player->GetMovement() * 0.12f;

  // draw imaginary line between this and target player
  Line line(origin, target);

  float danger = 0.0f;
  for (unsigned int opp = 0; opp < opponentPlayerImages.size(); opp++) {
    Vector3 oppPos = opponentPlayerImages.at(opp).position + opponentPlayerImages.at(opp).movement * 0.2f; // + time needed to brake
    float u; // % of line opp is closest to (0 .. 1)
    float oppDistance;
    oppDistance = line.GetDistanceToPoint(oppPos, u);

    if (u >= 0.0f && u <= 1.0f + 0.2f) { // opp is dangerous in the first place
      if ((passType == e_FunctionType_HighPass && (u < 0.2f || u > 0.65f)) ||
           passType != e_FunctionType_HighPass) {
        float clampedU = clamp(u, 0.0f, 1.0f);
        Vector3 intersect = origin * (1.0f - clampedU) + target * clampedU; // where opp is most likely to intercept ball

        float oppToIntersect_sec = (oppDistance + 1.0f) / sprintVelocity; // add some distance to correct for acceleration

        Vector3 originToBallPos = (intersect - origin);
        float penaltyTime = (passType == e_FunctionType_HighPass && u > 0.5f) ? 2.5f : 0.0f; // trapping high balls takes time
        float ballToIntersect_sec = 0.7f + originToBallPos.GetLength() * u * 0.03f + penaltyTime;
        ballToIntersect_sec *= 1.0f / ballVelocityMultiplier;

        danger += clamp(ballToIntersect_sec - oppToIntersect_sec + (secondScale * 0.5f), 0.0f, secondScale); // add some seconds because 0 seconds doesn't mean 0 danger
      }
    }
  }

  if (passType == e_FunctionType_HighPass) {
    float passLen = (target - origin).GetLength();
    // Less penalty on genuine long switches of play or crosses (> 24m)
    danger += (passLen > 24.0f) ? 0.18f : 0.36f;
  }

  danger = NormalizedClamp(danger, 0.0f, secondScale); // 1 super dangerous dude is basically the same as 100% danger
  float odds = 1.0f - danger;

  return odds;
}

void ElizaController::_AddCelebration(std::vector<PlayerCommand> &commandQueue) {

  unsigned long elapsed_ms = match->GetActualTime_ms() - match->GetReferee()->GetBuffer().stopTime;

  signed int xSide = (match->GetBall()->Predict(0).Get2D().coords[0] > 0) ? 1 : -1;
  signed int ySide = team->GetSide();
  if (team->GetLastTouchPlayer()) ySide = (team->GetLastTouchPlayer()->GetPosition().coords[1] >= 0) ? 1 : -1;
  Vector3 celebrationPosition = Vector3((pitchHalfW - 4.5f) * xSide, (pitchHalfH - 4.0f) * ySide, 0);

  Vector3 toTarget = celebrationPosition - player->GetPosition();
  float distToTarget = toTarget.GetLength();
  Vector3 desiredDirection = toTarget.GetNormalized(player->GetDirectionVec());
  float desiredVelocityFloat = ClampVelocity(distToTarget / 3.5f);
  Vector3 desiredLookAt = player->GetPosition() + player->GetDirectionVec() * 1000;

  int celebrationType = 1;
  if (match->GetLastGoalTeamID() != team->GetID()) {
    celebrationType = 2;
    desiredVelocityFloat = idleVelocity;
  } else {
    celebrationType = 1;
  }

  int madeGoal = 1;
  if (celebrationType == 1) {
    if (team->GetLastTouchPlayer() == player) {
      madeGoal = 2;
      if (distToTarget > 5.5f && elapsed_ms < 5800) {
        desiredVelocityFloat = sprintVelocity;
      } else if (distToTarget > 2.5f && elapsed_ms < 8200) {
        desiredVelocityFloat = dribbleVelocity;
      } else {
        desiredVelocityFloat = walkVelocity;
      }
    } else {
      madeGoal = 1;
      if (team->GetLastTouchPlayer() != 0 && CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
        float distToScorer = (team->GetLastTouchPlayer()->GetPosition() - player->GetPosition()).GetLength();
        if (distToScorer < 36.0f) {
          celebrationPosition = team->GetLastTouchPlayer()->GetPosition();
          desiredDirection = ((team->GetLastTouchPlayer()->GetPosition() * 0.65f + celebrationPosition * 0.35f) - player->GetPosition()).GetNormalized(player->GetDirectionVec());
          desiredVelocityFloat = (distToScorer > 3.2f) ? ClampVelocity(distToScorer / 2.2f) : walkVelocity;
        } else {
          desiredVelocityFloat = idleVelocity;
        }
        desiredLookAt = team->GetLastTouchPlayer()->GetPosition();
      } else {
        desiredVelocityFloat = idleVelocity;
      }
    }
  }


  // Extended post-goal celebration specials (1.4s .. 9.2s)

  if (elapsed_ms > 1400 && elapsed_ms < 9200) {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Special;
    command.useSpecialVar1 = true;
    command.specialVar1 = celebrationType;
    command.useSpecialVar2 = true;
    command.specialVar2 = madeGoal;
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    commandQueue.push_back(command);
  }


  // movement

  {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.desiredDirection = desiredDirection;
    command.desiredVelocityFloat = desiredVelocityFloat;
    command.useDesiredLookAt = true;
    command.desiredLookAt = desiredLookAt;
    commandQueue.push_back(command);
  }

}
