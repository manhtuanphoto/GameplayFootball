// Player-Centric Adaptive AI Pipeline for GameplayFootball
// Implements 7 pluggable, weight-adaptive, hot-removable AI behavior modules
// organized in 4 concentric layers around each player.

#include "ai_adaptive_pipeline.hpp"

#include "AIfunctions.hpp"
#include "../match.hpp"
#include "../team.hpp"
#include "../teamAIcontroller.hpp"
#include "../ball.hpp"
#include "../player/player.hpp"
#include "../player/controller/playercontroller.hpp"
#include "../../main.hpp"
#include "../../menu/menutask.hpp"
#include "base/geometry/line.hpp"
#include "base/geometry/triangle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

using namespace blunted;

AIAdaptiveRegistry &AIAdaptiveRegistry::Instance() {
  static AIAdaptiveRegistry instance;
  return instance;
}

AIAdaptiveRegistry::AIAdaptiveRegistry() {
  modules[e_AIModule_VisionPerception] = {
      e_AIModule_VisionPerception, "vision_perception", "1:MentalImg",
      "Baseline MentalImage Perception (Vision Cone Removed)", false, 0.0f};
  modules[e_AIModule_PlayerDisposition] = {
      e_AIModule_PlayerDisposition, "player_disposition", "2:Persona",
      "22-Stat Player Disposition & Risk Gate", true, 1.0f};
  modules[e_AIModule_TakeOn1v1] = {
      e_AIModule_TakeOn1v1, "takeon_1v1", "3:TakeOn1v1",
      "Active 1v1 Beat-Defender Burst", true, 1.0f};
  modules[e_AIModule_HoldAndShield] = {
      e_AIModule_HoldAndShield, "hold_shield", "4:Shield",
      "La Pausa & Physical Ball Shielding", true, 1.0f};
  modules[e_AIModule_TurnBeforeRun] = {
      e_AIModule_TurnBeforeRun, "turn_before_run", "5:TurnRun",
      "Biomechanical Turn-Before-Run & Kick Angle", true, 1.0f};
  modules[e_AIModule_LooseBallBurstLunge] = {
      e_AIModule_LooseBallBurstLunge, "looseball_lunge", "6:LungeGaze",
      "Loose-Ball Burst, Lunge & Ball Gaze", true, 1.0f};
  modules[e_AIModule_FormationCorridors] = {
      e_AIModule_FormationCorridors, "formation_corridors", "7:Corridors",
      "5-Corridor Positional Play (4-4-2 / 3-5-2)", true, 1.0f};

  playerStateOwnerID.fill(-1);
}

void AIAdaptiveRegistry::EnsureInitialized() {
  if (initialized) return;
  initialized = true;

  masterEnabled = true;
  for (int i = 0; i < e_AIModule_COUNT; i++) {
    modules[i].enabled = (i != e_AIModule_VisionPerception);
    modules[i].weight = (i != e_AIModule_VisionPerception) ? 1.0f : 0.0f;
  }

  Properties *cfg = GetConfiguration();
  if (!cfg) return;

  hudMode = (cfg->GetInt("ai_adaptive_hud", 1) > 0) ? 1 : 0;

  for (int i = 0; i < e_AIModule_COUNT; i++) {
    if (i == e_AIModule_VisionPerception) {
      modules[i].enabled = false;
      modules[i].weight = 0.0f;
      continue;
    }
    std::string keyWeight = std::string("ai_module_") + modules[i].configSlug + "_weight";
    modules[i].enabled = true;
    modules[i].weight = clamp(cfg->GetReal(keyWeight.c_str(), 1.0f), 0.1f, 2.0f);
  }
}

void AIAdaptiveRegistry::SaveToConfig() {
  Properties *cfg = GetConfiguration();
  if (!cfg) return;

  cfg->SetBool("ai_adaptive_master", true);
  cfg->SetInt("ai_adaptive_hud", hudMode);
  for (int i = 0; i < e_AIModule_COUNT; i++) {
    std::string keyEnabled = std::string("ai_module_") + modules[i].configSlug + "_enabled";
    std::string keyWeight = std::string("ai_module_") + modules[i].configSlug + "_weight";
    cfg->SetBool(keyEnabled.c_str(), true);
    cfg->Set(keyWeight.c_str(), modules[i].weight);
  }
  cfg->SaveFile(GetConfigFilename());
}

void AIAdaptiveRegistry::ResetRuntimeStates() {
  for (auto &st : playerStates) {
    st = PlayerRuntimeState();
  }
  playerStateOwnerID.fill(-1);
  teamFormations[0] = TeamFormationCache();
  teamFormations[1] = TeamFormationCache();
}

void AIAdaptiveRegistry::SetMasterEnabled(bool enabled) {
  EnsureInitialized();
  masterEnabled = enabled;
  if (!masterEnabled) ResetRuntimeStates();
  SaveToConfig();
}

bool AIAdaptiveRegistry::ToggleMasterEnabled() {
  EnsureInitialized();
  SetMasterEnabled(!masterEnabled);
  return masterEnabled;
}

bool AIAdaptiveRegistry::IsModuleEnabled(e_AIModuleId id) const {
  const_cast<AIAdaptiveRegistry*>(this)->EnsureInitialized();
  if (!masterEnabled) return false;
  return modules[id].enabled && modules[id].weight > 0.001f;
}

float AIAdaptiveRegistry::GetModuleWeight(e_AIModuleId id) const {
  const_cast<AIAdaptiveRegistry*>(this)->EnsureInitialized();
  if (!masterEnabled || !modules[id].enabled) return 0.0f;
  return modules[id].weight;
}

bool AIAdaptiveRegistry::ToggleModule(e_AIModuleId id) {
  EnsureInitialized();
  modules[id].enabled = !modules[id].enabled;
  SaveToConfig();
  return modules[id].enabled;
}

void AIAdaptiveRegistry::SetModuleEnabled(e_AIModuleId id, bool enabled) {
  EnsureInitialized();
  modules[id].enabled = enabled;
  SaveToConfig();
}

void AIAdaptiveRegistry::SetModuleWeight(e_AIModuleId id, float weight) {
  EnsureInitialized();
  modules[id].weight = clamp(weight, 0.0f, 2.0f);
  SaveToConfig();
}

const AIModuleConfig &AIAdaptiveRegistry::GetModuleConfig(e_AIModuleId id) const {
  const_cast<AIAdaptiveRegistry*>(this)->EnsureInitialized();
  return modules[id];
}

int AIAdaptiveRegistry::CycleHudMode() {
  EnsureInitialized();
  hudMode = (hudMode == 0) ? 1 : 0;
  SaveToConfig();
  return hudMode;
}

std::string AIAdaptiveRegistry::BuildStatusSummary() const {
  const_cast<AIAdaptiveRegistry*>(this)->EnsureInitialized();
  if (!masterEnabled) {
    return "Adaptive AI: OFF (Legacy Baseline Active)";
  }
  std::ostringstream oss;
  oss << "AI Modules: ";
  for (int i = 0; i < e_AIModule_COUNT; i++) {
    if (i > 0) oss << "  ";
    oss << "[" << modules[i].shortName << ":" << (modules[i].enabled ? "ON" : "OFF") << "]";
  }
  return oss.str();
}

AIAdaptiveRegistry::PlayerRuntimeState &AIAdaptiveRegistry::GetPlayerState(Player *player, unsigned long now_ms) const {
  int pid = player->GetID();
  int slot = ((pid % 64) + 64) % 64;
  PlayerRuntimeState &st = playerStates[slot];
  if (playerStateOwnerID[slot] != pid || now_ms + 50 < st.lastUpdate_ms || (now_ms - st.lastUpdate_ms) > 5000) {
    st = PlayerRuntimeState();
    playerStateOwnerID[slot] = pid;
  }
  st.lastUpdate_ms = now_ms;
  return st;
}

const char *AIAdaptiveRegistry::GetArchetypeShortName(e_AIPlayerArchetype archetype) {
  switch (archetype) {
    case e_AIArchetype_Playmaker:      return "Playmaker";
    case e_AIArchetype_WingerDribbler: return "1v1Dribbler";
    case e_AIArchetype_PressResistant: return "Anchor";
    case e_AIArchetype_Poacher:        return "Poacher";
    case e_AIArchetype_Stopper:        return "Stopper";
    default:                           return "Balanced";
  }
}

const char *AIAdaptiveRegistry::GetIntentShortName(e_AIActiveIntent intent) {
  switch (intent) {
    case e_AIIntent_Pass:            return "PASS";
    case e_AIIntent_CarryOpen:       return "CARRY";
    case e_AIIntent_TakeOn1v1:       return "1v1 TAKE-ON";
    case e_AIIntent_HoldShield:      return "SHIELD";
    case e_AIIntent_Shot:            return "SHOT";
    case e_AIIntent_Clearance:       return "CLEAR";
    case e_AIIntent_LooseBallLunge:  return "LUNGE";
    case e_AIIntent_CorridorSupport: return "SUPPORT";
    default:                         return "";
  }
}

// ============================================================================
// LAYER 1: OPPONENT MOMENTUM ANALYSIS (READS BASELINE MENTALIMAGE)
// ============================================================================

DefenderMomentumInfo AIAdaptiveRegistry::AnalyzeDefenderSituations(
    Match *match, Player *carrier, const std::vector<PlayerImage> &opponentImages) const {
  DefenderMomentumInfo info;
  Vector3 myPos = carrier->GetPosition();
  Vector3 myVel = carrier->GetMovement();
  float attSign = (float)(-carrier->GetTeam()->GetSide());
  Vector3 attDir(attSign, 0.0f, 0.0f);

  float bestPrimaryScore = 1000.0f;
  int primaryIdx = -1;

  for (unsigned int i = 0; i < opponentImages.size(); i++) {
    const PlayerImage &opp = opponentImages[i];
    if (opp.dynamicFormationEntry.role == e_PlayerRole_GK) continue;

    Vector3 predOppPos = opp.position + opp.movement * 0.14f;
    Vector3 toOpp = predOppPos - myPos;
    float d = toOpp.GetLength();

    if (d < info.closestAnyDist) {
      info.closestAnyDist = d;
      info.closestAnyPos = predOppPos;
    }

    if (d < 0.3f || d > 8.2f) continue;
    Vector3 toOppDir = toOpp.GetNormalized(attDir);
    float forwardDot = toOppDir.GetDotProduct(attDir);

    // Primary 1v1 defender must be roughly ahead or closing diagonally from the side
    if (forwardDot > -0.18f) {
      float duelRelevance = d * (1.25f - 0.45f * forwardDot);
      if (duelRelevance < bestPrimaryScore) {
        bestPrimaryScore = duelRelevance;
        primaryIdx = (int)i;
      }
    }
  }

  if (primaryIdx >= 0) {
    const PlayerImage &prim = opponentImages[primaryIdx];
    info.valid = true;
    info.playerID = prim.playerID;
    info.position = prim.position + prim.movement * 0.14f;
    info.velocity = prim.movement;

    Vector3 toDef = info.position - myPos;
    info.distance = std::max(0.25f, toDef.GetLength());
    Vector3 toDefDir = toDef.GetNormalized(attDir);
    info.forwardAlignment = toDefDir.GetDotProduct(attDir);

    Vector3 relVel = prim.movement - myVel;
    info.closingSpeed = -relVel.GetDotProduct(toDefDir);

    Vector3 perpLeft(-toDefDir.coords[1], toDefDir.coords[0], 0.0f);
    info.lateralVel = prim.movement.GetDotProduct(perpLeft);

    // If defender rushes in too fast or commits laterally, attacker can wrong-foot them
    float rushOvercommit = NormalizedClamp(info.closingSpeed, 2.2f, 7.2f);
    float lateralCommit = NormalizedClamp(fabs(info.lateralVel), 1.4f, 5.4f);
    info.wrongFootScore = clamp(0.52f * rushOvercommit + 0.48f * lateralCommit, 0.0f, 1.0f);

    // Count 2nd-line cover defenders lurking behind the primary defender
    for (unsigned int j = 0; j < opponentImages.size(); j++) {
      if ((int)j == primaryIdx) continue;
      if (opponentImages[j].dynamicFormationEntry.role == e_PlayerRole_GK) continue;
      Vector3 covPos = opponentImages[j].position + opponentImages[j].movement * 0.18f;
      Vector3 fromPrim = covPos - info.position;
      float covDist = fromPrim.GetLength();
      if (covDist < 5.4f && fromPrim.GetDotProduct(attDir) > -0.6f) {
        info.coverDefendersBehind++;
      }
    }
  }

  return info;
}

// ============================================================================
// LAYER 2: 22-STAT PLAYER DISPOSITION & PITCH-ZONE RISK SAFETY
// ============================================================================

PlayerDispositionProfile AIAdaptiveRegistry::BuildDispositionProfile(Match *match, Player *player) const {
  PlayerDispositionProfile prof;
  e_PlayerRole role = player->GetDynamicFormationEntry().role;
  signed int side = player->GetTeam()->GetSide();
  float attSign = (float)(-side);
  Vector3 myPos = player->GetPosition();

  // Check if player is GK or one of the deepest 2 defenders (last-man safety check!)
  std::vector<Player*> mates;
  player->GetTeam()->GetActivePlayers(mates);
  int deeperOutfieldMates = 0;
  for (unsigned int i = 0; i < mates.size(); i++) {
    if (mates[i] == player || mates[i]->GetFormationEntry().role == e_PlayerRole_GK) continue;
    if (mates[i]->GetPosition().coords[0] * side > myPos.coords[0] * side + 1.2f) {
      deeperOutfieldMates++;
    }
  }

  float normX = clamp((myPos.coords[0] * attSign + pitchHalfW) / (pitchHalfW * 2.0f), 0.0f, 1.0f);
  float absY = fabs(myPos.coords[1]);
  prof.isLastLineDefender = (role == e_PlayerRole_GK) || (deeperOutfieldMates <= 1 && normX < 0.55f);

  if (!IsModuleEnabled(e_AIModule_PlayerDisposition)) {
    prof.takeOnRiskAllowance = prof.isLastLineDefender ? 0.0f : NormalizedClamp(normX, 0.30f, 0.72f);
    return prof;
  }

  float vision   = player->GetStat("mental_vision");
  float calmness = player->GetStat("mental_calmness");
  float workrate = player->GetStat("mental_workrate");
  float resili   = player->GetStat("mental_resilience");
  float offPos   = player->GetStat("mental_offensivepositioning");
  float defPos   = player->GetStat("mental_defensivepositioning");

  float sPass    = player->GetStat("technical_shortpass");
  float hPass    = player->GetStat("technical_highpass");
  float dribble  = player->GetStat("technical_dribble");
  float bCtrl    = player->GetStat("technical_ballcontrol");
  float shot     = player->GetStat("technical_shot");
  float tackle   = player->GetStat("technical_standingtackle");

  float accel    = player->GetStat("physical_acceleration");
  float agility  = player->GetStat("physical_agility");
  float balance  = player->GetStat("physical_balance");

  float w = GetModuleWeight(e_AIModule_PlayerDisposition);

  prof.playmakerBias       = clamp(0.5f + ((0.40f * vision + 0.36f * sPass + 0.24f * hPass) - 0.62f) * 1.45f * w, 0.15f, 0.98f);
  prof.takeOn1v1Bias       = clamp(0.5f + ((0.38f * dribble + 0.25f * agility + 0.23f * accel + 0.14f * bCtrl) - 0.62f) * 1.55f * w, 0.12f, 0.98f);
  prof.holdShieldBias      = clamp(0.5f + ((0.38f * balance + 0.34f * calmness + 0.28f * bCtrl) - 0.62f) * 1.45f * w, 0.15f, 0.96f);
  prof.composureUnderPress = clamp(0.5f + ((0.52f * calmness + 0.28f * balance + 0.20f * resili) - 0.60f) * 1.40f * w, 0.15f, 0.98f);
  prof.offBallWorkRate     = clamp(0.45f * workrate + 0.30f * offPos + 0.25f * accel, 0.20f, 0.98f);
  prof.defensiveDiscipline = clamp(0.50f * defPos + 0.30f * tackle + 0.20f * workrate, 0.20f, 0.98f);

  // Classify natural player archetype from dominant stat cluster + role context
  float finisherScore = 0.55f * shot + 0.45f * offPos + (role == e_PlayerRole_CF ? 0.12f : 0.0f);
  float wingerScore   = prof.takeOn1v1Bias + ((role == e_PlayerRole_LM || role == e_PlayerRole_RM || role == e_PlayerRole_AM) ? 0.08f : 0.0f);
  float playmakerScr  = prof.playmakerBias + ((role == e_PlayerRole_CM || role == e_PlayerRole_AM || role == e_PlayerRole_DM) ? 0.07f : 0.0f);
  float anchorScore   = prof.holdShieldBias + ((role == e_PlayerRole_DM || role == e_PlayerRole_CM) ? 0.06f : 0.0f);
  float stopperScore  = prof.defensiveDiscipline + ((role == e_PlayerRole_CB) ? 0.12f : 0.0f);

  float top = 0.63f;
  if (stopperScore > top && (role == e_PlayerRole_CB || role == e_PlayerRole_LB || role == e_PlayerRole_RB)) {
    prof.archetype = e_AIArchetype_Stopper;
    top = stopperScore;
  }
  if (wingerScore > top)    { prof.archetype = e_AIArchetype_WingerDribbler; top = wingerScore; }
  if (playmakerScr > top)   { prof.archetype = e_AIArchetype_Playmaker;      top = playmakerScr; }
  if (anchorScore > top && prof.archetype == e_AIArchetype_Balanced) {
    prof.archetype = e_AIArchetype_PressResistant;
    top = anchorScore;
  }
  if (finisherScore > top && role == e_PlayerRole_CF) {
    prof.archetype = e_AIArchetype_Poacher;
  }

  // Pitch-zone risk allowance: strictly 0 for GK / last-line defender or deep own-box build-up
  if (prof.isLastLineDefender || normX < 0.24f) {
    prof.takeOnRiskAllowance = 0.0f;
  } else {
    float wingFreedom = NormalizedClamp(absY, 9.0f, 26.0f) * 0.22f;
    float progressionFreedom = NormalizedClamp(normX, 0.28f, 0.74f) * 0.82f;
    float roleMod = (role == e_PlayerRole_CB) ? 0.25f : (role == e_PlayerRole_DM ? 0.65f : 1.0f);
    prof.takeOnRiskAllowance = clamp((progressionFreedom + wingFreedom) * roleMod, 0.0f, 1.0f);
  }

  PlayerRuntimeState &st = GetPlayerState(player, match->GetActualTime_ms());
  st.cachedArchetype = prof.archetype;
  return prof;
}

// ============================================================================
// LAYER 3: PLUGGABLE ON-BALL INTENT EVALUATORS & ARBITER
// ============================================================================

float AIAdaptiveRegistry::AdaptPassCandidateUtility(
    const PassCandidateContext &ctx, const PlayerDispositionProfile &profile, float baseUtility) const {
  if (!masterEnabled) return baseUtility;

  float adapted = baseUtility;
  Vector3 myPos = ctx.carrier->GetPosition();
  Vector3 targetPos = ctx.target->GetPosition();
  Vector3 toTarget = (targetPos - myPos).Get2D().GetNormalized(ctx.carrier->GetDirectionVec());

  // 1. Biomechanical Body-Turn Kick Feasibility
  if (IsModuleEnabled(e_AIModule_TurnBeforeRun)) {
    Vector3 bodyDir = ctx.carrier->GetBodyDirectionVec().Get2D();
    float turnAngle = fabs(bodyDir.GetAngle2D(toTarget)); // [0 .. pi]
    if (turnAngle > 0.48f * pi) {
      float excessTurn = NormalizedClamp(turnAngle, 0.48f * pi, pi);
      float agilityMitigation = 0.5f * ctx.carrier->GetStat("physical_agility") +
                                0.5f * ctx.carrier->GetStat("technical_ballcontrol");
      float longKickDifficulty = (ctx.passType != e_FunctionType_ShortPass || ctx.passDist > 20.0f) ? 1.35f : 0.85f;
      float turnPenalty = excessTurn * (0.14f - 0.06f * agilityMitigation) * longKickDifficulty *
                          (0.65f + 0.55f * ctx.underPressure);
      adapted -= turnPenalty * GetModuleWeight(e_AIModule_TurnBeforeRun);
    }
  }

  // 2. Playmaker Disposition Bonus
  if (IsModuleEnabled(e_AIModule_PlayerDisposition)) {
    float passBiasDelta = profile.playmakerBias - 0.52f;
    adapted += passBiasDelta * 0.10f;
    if (profile.archetype == e_AIArchetype_Playmaker && (ctx.isThroughBall || ctx.isSwitchPlay)) {
      adapted += 0.05f * GetModuleWeight(e_AIModule_PlayerDisposition);
    }
  }

  return adapted;
}

OnBallArbitrationResult AIAdaptiveRegistry::ArbitrateOnBallIntent(
    const OnBallArbitrationInput &in,
    const PlayerDispositionProfile &profile,
    const DefenderMomentumInfo &defInfo) {

  OnBallArbitrationResult out;
  out.adaptedDribbleDir = in.baselineDribbleDir;
  out.adaptedDribbleVel = in.baselineDribbleVel;
  out.executeShot = in.baselineShouldShoot;
  out.executePass = (in.bestPassTarget != nullptr && in.bestPassUtility > in.baselineDribbleUtility);
  out.winningIntent = out.executeShot ? e_AIIntent_Shot
                                      : (out.executePass ? e_AIIntent_Pass : e_AIIntent_CarryOpen);

  if (!masterEnabled) return out;

  unsigned long now_ms = in.match->GetActualTime_ms();
  PlayerRuntimeState &st = GetPlayerState(in.carrier, now_ms);
  st.lastBestPassTargetID = (in.bestPassTarget ? in.bestPassTarget->GetID() : -1);
  st.lastPrimaryDefID = (defInfo.valid ? defInfo.playerID : -1);
  st.lastShotTargetY = in.bestShotTargetY;

  signed int side = in.carrier->GetTeam()->GetSide();
  float attSign = (float)(-side);
  Vector3 attDir(attSign, 0.0f, 0.0f);
  Vector3 myPos = in.carrier->GetPosition();
  float normX = clamp((myPos.coords[0] * attSign + pitchHalfW) / (pitchHalfW * 2.0f), 0.0f, 1.0f);

  // Composure under pressure prevents premature clearances for calm defenders/midfielders
  if (IsModuleEnabled(e_AIModule_PlayerDisposition) || IsModuleEnabled(e_AIModule_HoldAndShield)) {
    float ownGoalDist = (myPos - Vector3(pitchHalfW * side, 0.0f, 0.0f)).GetLength();
    if (profile.composureUnderPress > 0.58f && ownGoalDist > 9.5f && in.bestPassOdds > 0.14f) {
      out.suppressPanicClearance = true;
    }
  }

  // Shots remain top priority when a high-value scoring window exists
  if (in.baselineShouldShoot && (in.bestShotUtility > 0.72f || in.is1v1Keeper)) {
    out.winningIntent = e_AIIntent_Shot;
    out.executeShot = true;
    out.executePass = false;
    st.wasShielding = false;
    st.activeIntent = e_AIIntent_Shot;
    st.intentExpire_ms = now_ms + 380;
    return out;
  }

  // --------------------------------------------------------------------------
  // Evaluate Intent A: Active 1v1 Take-On ("Qua nguoi chu dong")
  // --------------------------------------------------------------------------
  float takeOnUtility = -1.0f;
  Vector3 takeOnDir = in.baselineDribbleDir;
  float takeOnVel = sprintVelocity;
  bool takeOnKnockOn = false;

  if (IsModuleEnabled(e_AIModule_TakeOn1v1) &&
      profile.takeOnRiskAllowance > 0.08f &&
      defInfo.valid &&
      defInfo.distance >= 1.55f && defInfo.distance <= 6.8f &&
      defInfo.forwardAlignment > -0.10f &&
      defInfo.coverDefendersBehind <= 1) {

    Player *oppPlayer = in.match->GetPlayer(defInfo.playerID);
    float attDribble = in.carrier->GetStat("technical_dribble");
    float attAccel   = in.carrier->GetStat("physical_acceleration");
    float attAgil    = in.carrier->GetStat("physical_agility");

    float defTackle  = oppPlayer ? oppPlayer->GetStat("technical_standingtackle") : 0.6f;
    float defAgil    = oppPlayer ? oppPlayer->GetStat("physical_agility") : 0.6f;
    float defPosStat = oppPlayer ? oppPlayer->GetStat("mental_defensivepositioning") : 0.6f;

    float skillEdge = (0.40f * attDribble + 0.32f * attAccel + 0.28f * attAgil) -
                      (0.44f * defTackle + 0.30f * defAgil + 0.26f * defPosStat);

    // Isolation quality: pure 1v1 (0 cover defenders) is prime take-on territory
    float isolationFactor = (defInfo.coverDefendersBehind == 0) ? 1.0f : 0.52f;
    // Sweet-spot engagement distance is 2.1m .. 5.2m
    float distWindow = 1.0f - fabs(defInfo.distance - 3.55f) / 3.6f;
    distWindow = clamp(distWindow, 0.35f, 1.0f);

    float wTakeOn = GetModuleWeight(e_AIModule_TakeOn1v1);
    takeOnUtility = in.baselineDribbleUtility +
                    in.underPressure * 0.20f + // Neutralizes the baseline pressure fear!
                    (0.20f + 0.42f * skillEdge + 0.22f * defInfo.wrongFootScore +
                     0.18f * (profile.takeOn1v1Bias - 0.5f)) *
                        isolationFactor * distWindow * profile.takeOnRiskAllowance * wTakeOn;

    // Bonus in dangerous 1v1 wing / half-space zones in attacking half
    if (normX > 0.56f && fabs(myPos.coords[1]) > 8.5f && defInfo.coverDefendersBehind == 0) {
      takeOnUtility += 0.09f * wTakeOn;
    }

    // Choose diagonal beat-defender exit lane with 340ms hysteresis to prevent left-right jitter
    Vector3 toDef = (defInfo.position - myPos).Get2D();
    Vector3 toDefDir = toDef.GetNormalized(attDir);
    Vector3 perpLeft(-toDefDir.coords[1], toDefDir.coords[0], 0.0f); // +1 side

    int chosenSide = st.lockedBeatSide;
    if (chosenSide == 0 || st.lockedDefenderID != defInfo.playerID || now_ms >= st.beatLockUntil_ms) {
      auto scoreSide = [&](int s) -> float {
        Vector3 sideVec = perpLeft * (float)s;
        Vector3 exitSpot = defInfo.position + attDir * 3.4f + sideVec * 2.3f;
        float score = 0.0f;
        // 1. Exploit opposite of defender's lateral momentum (wrong-footing!)
        score += (-defInfo.lateralVel * (float)s) * 0.14f;
        // 2. Stay inside touchlines with comfortable margin
        if (fabs(exitSpot.coords[1]) > pitchHalfH - 2.6f) score -= 0.75f;
        else if (fabs(exitSpot.coords[1]) > pitchHalfH - 5.5f) score -= 0.22f;
        // 3. In final third, reward cutting inside toward goal/half-space unless blocked
        if (normX > 0.66f && fabs(exitSpot.coords[1]) < fabs(myPos.coords[1])) score += 0.16f;
        // 4. Prefer side closer to current body orientation for explosive first step
        score += in.carrier->GetBodyDirectionVec().Get2D().GetDotProduct(sideVec) * 0.10f;
        // 5. Check free space at exit spot
        float spaceAtExit = AI_CalculateFreeSpace(in.match, in.mentalImage, in.carrier->GetTeamID(), exitSpot, 4.8f, 0.28f, true);
        score += spaceAtExit * 0.45f;
        return score;
      };

      chosenSide = (scoreSide(1) >= scoreSide(-1)) ? 1 : -1;
      st.lockedBeatSide = chosenSide;
      st.lockedDefenderID = defInfo.playerID;
      st.beatLockUntil_ms = now_ms + 340;
    }

    Vector3 beatPerp = perpLeft * (float)chosenSide;
    // Two-tempo 1v1 rhythm:
    // Phase 1 (4.6m..6.8m): controlled approach touch drawing defender in.
    // Phase 2 (< 4.6m): sharp diagonal burst past defender's hip into space behind!
    if (defInfo.distance > 4.65f) {
      Vector3 setupVec = (toDefDir * 0.78f + beatPerp * 0.34f).GetNormalized(attDir);
      takeOnDir = setupVec;
      takeOnVel = walkVelocity;
    } else {
      float lateralClearance = clamp(2.15f - (1.0f - NormalizedClamp(defInfo.distance, 1.6f, 4.6f)) * 0.25f, 1.75f, 2.35f);
      Vector3 burstTarget = defInfo.position + attDir * 3.6f + beatPerp * lateralClearance;
      takeOnDir = (burstTarget - myPos).Get2D().GetNormalized(attDir);
      takeOnVel = sprintVelocity;

      float spaceBehind = AI_CalculateFreeSpace(in.match, in.mentalImage, in.carrier->GetTeamID(),
                                                defInfo.position + attDir * 6.0f, 5.5f, 0.35f, true);
      if (defInfo.distance < 3.5f && spaceBehind > 0.62f && attAccel > 0.62f &&
          fabs(burstTarget.coords[0]) < pitchHalfW - 6.0f && fabs(burstTarget.coords[1]) < pitchHalfH - 4.0f) {
        takeOnKnockOn = true;
      }
    }
  }

  // --------------------------------------------------------------------------
  // Evaluate Intent B: Hold & Shield / La Pausa ("Giu nhip che bong")
  // --------------------------------------------------------------------------
  float holdShieldUtility = -1.0f;
  Vector3 shieldDir = in.baselineDribbleDir;
  float shieldVel = dribbleVelocity;

  unsigned long currentShieldDuration_ms = st.wasShielding ? (now_ms - st.shieldStart_ms) : 0;

  if (IsModuleEnabled(e_AIModule_HoldAndShield) &&
      in.carrier->GetFormationEntry().role != e_PlayerRole_GK &&
      !in.is1v1Keeper &&
      !(in.baselineShouldShoot && normX > 0.72f) &&
      defInfo.closestAnyDist < 4.3f &&
      currentShieldDuration_ms < 900) {

    Vector3 awayFromPresser = (myPos - defInfo.closestAnyPos).Get2D().GetNormalized(Vector3(-attSign, 0, 0));
    Vector3 perpA(-awayFromPresser.coords[1], awayFromPresser.coords[0], 0.0f);
    if (fabs(myPos.coords[1] + perpA.coords[1] * 3.0f) > fabs(myPos.coords[1] - perpA.coords[1] * 3.0f)) {
      perpA = -perpA; // Turn away from sideline toward pitch interior
    }

    // Check if presser is on our back/hip (easier to shield physically than running straight into them)
    float presserBehindDot = (-awayFromPresser).GetDotProduct(attDir);
    float shieldAngleBonus = (presserBehindDot < 0.25f) ? 0.10f : 0.02f;

    float wShield = GetModuleWeight(e_AIModule_HoldAndShield);
    float durationDecay = NormalizedClamp((float)currentShieldDuration_ms, 350.0f, 900.0f) * 0.24f;

    holdShieldUtility = in.baselineDribbleUtility * 0.82f +
                        (0.18f + 0.26f * profile.holdShieldBias + shieldAngleBonus - durationDecay) * wShield;

    // Target-man hold-up play (CF with back to goal waiting for midfield runners)
    if (in.carrier->GetDynamicFormationEntry().role == e_PlayerRole_CF && normX > 0.52f && in.bestPassUtility < 0.56f) {
      holdShieldUtility += 0.08f * wShield;
    }

    shieldDir = (awayFromPresser * 0.66f + perpA * 0.34f).GetNormalized(in.carrier->GetDirectionVec());
    // Never shield backward into own goal box
    if (normX < 0.20f && shieldDir.coords[0] * side > 0.2f) {
      shieldDir = (perpA * 0.8f + attDir * 0.2f).GetNormalized(attDir);
    }
    shieldVel = dribbleVelocity;
  }

  // --------------------------------------------------------------------------
  // Arbitrate Between Shot, Pass, 1v1 Take-On, Hold/Shield, and Open Carry
  // --------------------------------------------------------------------------
  float carryUtility = in.baselineDribbleUtility;
  if (IsModuleEnabled(e_AIModule_PlayerDisposition)) {
    carryUtility += (profile.takeOn1v1Bias - 0.52f) * 0.06f;
  }

  // Check if Shot wins (including Poacher / 1v1 clinical instinct)
  float adaptedShotUtility = in.bestShotUtility;
  if (IsModuleEnabled(e_AIModule_PlayerDisposition) && adaptedShotUtility > 0.0f) {
    if (profile.archetype == e_AIArchetype_Poacher) adaptedShotUtility += 0.08f;
    if (in.is1v1Keeper) adaptedShotUtility += 0.14f;
  }
  bool passIntoBox = (in.bestPassTarget != nullptr) &&
                     (in.bestPassTarget->GetPosition().coords[0] * attSign > pitchHalfW - 15.5f);
  float effectivePassForShotCheck = (in.is1v1Keeper && !passIntoBox)
                                        ? (in.bestPassUtility * 0.80f)
                                        : in.bestPassUtility;
  float effectiveCarryForShotCheck = in.is1v1Keeper ? (carryUtility * 0.78f) : carryUtility;
  float effectiveTakeOnForShotCheck = in.is1v1Keeper ? (takeOnUtility * 0.80f) : takeOnUtility;
  float topNonShot = std::max(std::max(effectivePassForShotCheck, effectiveCarryForShotCheck),
                              std::max(effectiveTakeOnForShotCheck, holdShieldUtility));
  if (in.baselineShouldShoot && adaptedShotUtility >= topNonShot - 0.06f) {
    out.winningIntent = e_AIIntent_Shot;
    out.executeShot = true;
    out.executePass = false;
    st.wasShielding = false;
    st.activeIntent = e_AIIntent_Shot;
    st.intentExpire_ms = now_ms + 380;
    st.lastShotTargetY = in.bestShotTargetY;
    return out;
  }
  out.executeShot = false;

  // Compare Pass vs Take-On vs Shield vs Open Carry
  if (takeOnUtility > carryUtility &&
      takeOnUtility > holdShieldUtility &&
      takeOnUtility > in.bestPassUtility + 0.015f) {
    out.winningIntent = e_AIIntent_TakeOn1v1;
    out.executePass = false;
    out.adaptedDribbleDir = takeOnDir;
    out.adaptedDribbleVel = takeOnVel;
    out.requestKnockOn = takeOnKnockOn;
    out.winningConfidence = clamp(takeOnUtility, 0.0f, 1.0f);
    st.wasShielding = false;
  } else if (holdShieldUtility > carryUtility + 0.02f &&
             holdShieldUtility > in.bestPassUtility + 0.01f) {
    out.winningIntent = e_AIIntent_HoldShield;
    out.executePass = false;
    out.adaptedDribbleDir = shieldDir;
    out.adaptedDribbleVel = shieldVel;
    out.winningConfidence = clamp(holdShieldUtility, 0.0f, 1.0f);
    if (!st.wasShielding) {
      st.wasShielding = true;
      st.shieldStart_ms = now_ms;
    }
  } else if (in.bestPassTarget != nullptr && in.bestPassUtility > carryUtility) {
    out.winningIntent = e_AIIntent_Pass;
    out.executePass = true;
    out.winningConfidence = clamp(in.bestPassUtility, 0.0f, 1.0f);
    st.wasShielding = false;
  } else {
    out.winningIntent = e_AIIntent_CarryOpen;
    out.executePass = false;
    out.winningConfidence = clamp(carryUtility, 0.0f, 1.0f);
    st.wasShielding = false;
  }

  st.activeIntent = out.winningIntent;
  st.intentExpire_ms = now_ms + 280;
  st.lastBestPassTargetID = (in.bestPassTarget ? in.bestPassTarget->GetID() : -1);
  st.lastPrimaryDefID = (defInfo.valid ? defInfo.playerID : -1);
  st.lastAdaptedDir = out.adaptedDribbleDir;
  return out;
}

// ============================================================================
// LAYER 4A & 4B: LOOSE-BALL BURST/LUNGE, GAZE & TURN-BEFORE-RUN
// ============================================================================

void AIAdaptiveRegistry::AdaptOffBallPursuitAndLunge(
    Match *match, const MentalImage *mentalImage, Player *player,
    bool teamHasBestPossession, float possessionAmount,
    Vector3 &inputDirection, float &inputVelocityFloat,
    bool &forceMagnet, bool &extraHaste, bool &byAnyMeansInterfere) {

  if (!IsModuleEnabled(e_AIModule_LooseBallBurstLunge)) return;
  if (player->GetFormationEntry().role == e_PlayerRole_GK) return;

  Vector3 myPos = player->GetPosition();
  Vector3 ballNow = mentalImage->GetBallPrediction(40).Get2D();
  Vector3 ballLead = mentalImage->GetBallPrediction(180).Get2D();
  float ballDist = (ballNow - myPos).GetLength();
  if (ballDist > 8.5f) return;

  Team *oppTeam = match->GetTeam(abs(player->GetTeamID() - 1));
  bool ballIsLooseOrContested = (!oppTeam->HasUniquePossession() || possessionAmount > 0.72f ||
                                 match->GetBall()->GetMovement().Get2D().GetLength() > 4.2f);
  if (!ballIsLooseOrContested) return;

  bool isPrimaryChaser = (player->GetTeam()->GetDesignatedTeamPossessionPlayer() == player);
  bool isVeryClose = (ballDist < 3.4f && possessionAmount > 0.58f);
  if (!isPrimaryChaser && !isVeryClose) return;

  float w = GetModuleWeight(e_AIModule_LooseBallBurstLunge);
  float accelStat = player->GetStat("physical_acceleration");
  float reactStat = player->GetStat("physical_reaction");
  float burstFactor = 1.0f + (0.14f + 0.22f * (0.6f * accelStat + 0.4f * reactStat)) * w;

  if (ballDist < 5.2f) {
    Vector3 toBallLead = (ballLead - myPos).Get2D();
    if (toBallLead.GetLength() > 0.2f) {
      inputDirection = (inputDirection * (1.0f - 0.55f * w) + toBallLead.GetNormalized(inputDirection) * (0.55f * w))
                           .GetNormalized(inputDirection);
    }
    inputVelocityFloat = clamp(std::max(inputVelocityFloat * burstFactor, walkVelocity * 1.15f), dribbleVelocity, sprintVelocity);
    forceMagnet = true;
    if (ballDist < 4.2f) {
      extraHaste = true;
    }
  }

  // Dynamic athletic lunge reach on 50/50 loose balls within leg-extension radius
  if (!teamHasBestPossession && ballDist < 2.15f && match->GetBall()->Predict(120).coords[2] < 1.25f) {
    byAnyMeansInterfere = true;
    PlayerRuntimeState &st = GetPlayerState(player, match->GetActualTime_ms());
    st.activeIntent = e_AIIntent_LooseBallLunge;
    st.intentExpire_ms = match->GetActualTime_ms() + 260;
  }
}

void AIAdaptiveRegistry::AdaptFinalMovementCommand(
    Match *match, const MentalImage *mentalImage, Player *player,
    bool isOnBallCarrier, PlayerCommand &moveCmd) {

  if (!masterEnabled) return;
  if (moveCmd.desiredFunctionType != e_FunctionType_Movement || !moveCmd.useDesiredMovement) return;
  if (player->GetFormationEntry().role == e_PlayerRole_GK) return;

  Vector3 myPos = player->GetPosition();
  Vector3 ballPos = mentalImage->GetBallPrediction(60).Get2D();
  float ballDist = (ballPos - myPos).GetLength();

  // 1. Biomechanical Turn-Before-Run (off-ball players orient hips/torso into sharp turns before sprinting)
  if (!isOnBallCarrier && IsModuleEnabled(e_AIModule_TurnBeforeRun) &&
      moveCmd.desiredVelocityFloat > idleDribbleSwitch + 0.3f) {

    // Allow defenders within 4.2m of opponent ball-carrier to jockey backward/sideways naturally
    Player *oppCarrier = match->GetTeam(abs(player->GetTeamID() - 1))->GetDesignatedTeamPossessionPlayer();
    bool isCloseJockeying = (oppCarrier && match->GetBestPossessionTeamID() != player->GetTeamID() &&
                             (oppCarrier->GetPosition() - myPos).GetLength() < 4.4f);

    if (!isCloseJockeying) {
      Vector3 bodyDir = player->GetBodyDirectionVec().Get2D().GetNormalized(player->GetDirectionVec());
      Vector3 targetDir = moveCmd.desiredDirection.Get2D().GetNormalized(bodyDir);
      radian turnAngle = bodyDir.GetAngle2D(targetDir); // signed [-pi .. pi]
      float absTurn = fabs(turnAngle);

      // Sharp direction change (> 62 degrees): initiate torso/hip turn first instead of instant sideways sprint
      if (absTurn > 0.34f * pi) {
        float wTurn = GetModuleWeight(e_AIModule_TurnBeforeRun);
        float agility = player->GetStat("physical_agility");

        // Maximum comfortable angular step while pivoting hips
        float maxStepRad = (0.42f + 0.24f * agility) * pi;
        if (absTurn > maxStepRad) {
          float sign = (turnAngle >= 0.0f) ? 1.0f : -1.0f;
          Vector3 curvedDir = bodyDir.GetRotated2D(sign * maxStepRad);
          moveCmd.desiredDirection = (targetDir * (1.0f - 0.65f * wTurn) + curvedDir * (0.65f * wTurn))
                                         .GetNormalized(targetDir);
        }

        // Moderate locomotion speed during the pivot so Humanoid selects crisp turn/accelerate anims
        float pivotSeverity = NormalizedClamp(absTurn, 0.34f * pi, 0.88f * pi);
        float pivotCapVel = walkVelocity * (1.0f - 0.38f * pivotSeverity * (1.0f - 0.35f * agility)) +
                            dribbleVelocity * (0.38f * pivotSeverity * (1.0f - 0.35f * agility));
        if (moveCmd.desiredVelocityFloat > pivotCapVel) {
          moveCmd.desiredVelocityFloat = moveCmd.desiredVelocityFloat * (1.0f - 0.72f * wTurn) +
                                         pivotCapVel * (0.72f * wTurn);
        }

        // Look into the turn arc so head + torso rotate cleanly into the new run line!
        moveCmd.useDesiredLookAt = true;
        moveCmd.desiredLookAt = myPos + moveCmd.desiredDirection * 10.0f;
        return;
      }
    }
  }

  // 2. Ball-Tracking Gaze for nearby off-ball players (feeds Procedural 3D Head Look-At IK!)
  if (!isOnBallCarrier && IsModuleEnabled(e_AIModule_LooseBallBurstLunge) && ballDist < 20.0f) {
    Vector3 toBall = (ballPos - myPos).Get2D();
    if (toBall.GetLength() > 0.35f) {
      Vector3 toBallDir = toBall.GetNormalized(moveCmd.desiredDirection);
      Vector3 bodyDir = player->GetBodyDirectionVec().Get2D().GetNormalized(moveCmd.desiredDirection);
      // Only track ball with head/eyes when within anatomically natural +/-115 deg of torso
      if (fabs(bodyDir.GetAngle2D(toBallDir)) < 0.64f * pi) {
        float gazeBlend = (1.0f - NormalizedClamp(ballDist, 3.0f, 20.0f)) *
                          (moveCmd.desiredVelocityFloat > walkSprintSwitch ? 0.45f : 0.78f) *
                          clamp(GetModuleWeight(e_AIModule_LooseBallBurstLunge), 0.0f, 1.2f);
        gazeBlend = clamp(gazeBlend, 0.0f, 0.85f);
        Vector3 curLookDir = (moveCmd.desiredLookAt - myPos).Get2D().GetNormalized(moveCmd.desiredDirection);
        Vector3 blendedLook = (curLookDir * (1.0f - gazeBlend) + toBallDir * gazeBlend).GetNormalized(toBallDir);
        moveCmd.useDesiredLookAt = true;
        moveCmd.desiredLookAt = myPos + blendedLook * 10.0f;
      }
    }
  }
}

// ============================================================================
// LAYER 4C: 5-CORRIDOR POSITIONAL PLAY & FORMATION SUPPORT (4-4-2 / 3-5-2 / 4-3-3)
// ============================================================================

e_TeamFormationArchetype AIAdaptiveRegistry::ClassifyTeamFormation(Team *team) {
  int tid = clamp(team->GetID(), 0, 1);
  unsigned long now_ms = team->GetMatch()->GetActualTime_ms();
  TeamFormationCache &cache = teamFormations[tid];

  if (cache.lastCheck_ms != 0 && now_ms >= cache.lastCheck_ms && (now_ms - cache.lastCheck_ms) < 600) {
    return cache.archetype;
  }

  std::vector<Player*> active;
  team->GetActivePlayers(active);
  int cbs = 0, fullbacks = 0, mids = 0, wingers = 0, strikers = 0;
  for (unsigned int i = 0; i < active.size(); i++) {
    e_PlayerRole r = active[i]->GetFormationEntry().role;
    if (r == e_PlayerRole_CB) cbs++;
    else if (r == e_PlayerRole_LB || r == e_PlayerRole_RB) fullbacks++;
    else if (r == e_PlayerRole_DM || r == e_PlayerRole_CM || r == e_PlayerRole_AM) mids++;
    else if (r == e_PlayerRole_LM || r == e_PlayerRole_RM) wingers++;
    else if (r == e_PlayerRole_CF) strikers++;
  }

  cache.lastCheck_ms = now_ms;
  cache.cbCount = cbs;
  cache.cfCount = strikers;

  if (cbs >= 3) {
    cache.archetype = e_Formation_Wingback_352;
  } else if (strikers == 1 && wingers >= 2 && mids >= 3) {
    cache.archetype = e_Formation_WideAttack_433;
  } else if (strikers == 1 && mids + wingers >= 5) {
    cache.archetype = e_Formation_Compact_451;
  } else {
    cache.archetype = e_Formation_Balanced_442;
  }
  return cache.archetype;
}

static int GetVerticalCorridorIndex(float y) {
  // 5 Positional-Play Vertical Corridors across [-pitchHalfH .. +pitchHalfH] ([-36 .. +36]):
  // 0: Right/Bottom Wing (<-20), 1: Half-Space (-20..-7.5), 2: Center (-7.5..+7.5),
  // 3: Half-Space (+7.5..+20),   4: Left/Top Wing (>+20)
  if (y < -20.0f) return 0;
  if (y < -7.5f)  return 1;
  if (y <= 7.5f)  return 2;
  if (y <= 20.0f) return 3;
  return 4;
}

static float GetCorridorCenterY(int corridor) {
  static const float centers[5] = { -26.5f, -13.5f, 0.0f, 13.5f, 26.5f };
  int idx = (corridor < 0) ? 0 : ((corridor > 4) ? 4 : corridor);
  return centers[idx];
}

Vector3 AIAdaptiveRegistry::AdaptFormationBaseShape(
    Team *team, Player *player, e_PlayerRole role,
    float possessionBias, const Vector3 &baseDesiredPos) {

  if (!IsModuleEnabled(e_AIModule_FormationCorridors)) return baseDesiredPos;
  if (role == e_PlayerRole_GK) return baseDesiredPos;

  e_TeamFormationArchetype form = ClassifyTeamFormation(team);
  float w = GetModuleWeight(e_AIModule_FormationCorridors);
  signed int side = team->GetSide();
  float attSign = (float)(-side);
  float defPhase = clamp(1.0f - possessionBias, 0.0f, 1.0f);
  Vector3 pos = baseDesiredPos;

  if (form == e_Formation_Wingback_352) {
    // 3-5-2 morphs into 5-3-2 out of possession (LM/RM drop to protect flanks) and pushes wingbacks high in attack
    if (role == e_PlayerRole_LM || role == e_PlayerRole_RM) {
      float shiftX = (-6.5f * defPhase + 4.5f * possessionBias) * attSign * w;
      pos.coords[0] += shiftX;
    } else if (role == e_PlayerRole_CB) {
      // Outside CBs spread wider during build-up to access half-space passing lanes
      pos.coords[1] *= (1.0f + 0.18f * possessionBias * w);
    }
  } else if (form == e_Formation_Balanced_442) {
    // 4-4-2 maintains tight longitudinal compactness between midfield 4 and defensive 4 when defending
    if ((role == e_PlayerRole_CM || role == e_PlayerRole_LM || role == e_PlayerRole_RM || role == e_PlayerRole_DM) && defPhase > 0.5f) {
      pos.coords[0] -= attSign * (2.4f * (defPhase - 0.5f) * 2.0f) * w;
      pos.coords[1] *= (1.0f - 0.10f * defPhase * w); // Narrow horizontal gaps centrally
    }
  } else if (form == e_Formation_WideAttack_433) {
    // 4-3-3 lone DM anchors behind the two 8s during possession (rest-defense pivot)
    if (role == e_PlayerRole_DM && possessionBias > 0.45f) {
      pos.coords[0] -= attSign * 3.0f * possessionBias * w;
      pos.coords[1] *= (1.0f - 0.35f * w);
    }
  }

  pos.coords[0] = clamp(pos.coords[0], -pitchHalfW, pitchHalfW);
  pos.coords[1] = clamp(pos.coords[1], -pitchHalfH, pitchHalfH);
  return pos;
}

Vector3 AIAdaptiveRegistry::AdaptSupportPositionCorridors(
    Match *match, const MentalImage *mentalImage,
    Player *player, Player *designatedCarrier,
    const Vector3 &candidateSupportPos) {

  if (!IsModuleEnabled(e_AIModule_FormationCorridors)) return candidateSupportPos;
  if (!designatedCarrier || player == designatedCarrier) return candidateSupportPos;
  if (player->GetFormationEntry().role == e_PlayerRole_GK) return candidateSupportPos;

  Team *team = player->GetTeam();
  // Apply Positional-Play corridor spacing primarily when team is in or contesting possession
  if (team->GetFadingTeamPossessionAmount() < 0.92f) return candidateSupportPos;

  unsigned long now_ms = match->GetActualTime_ms();
  e_TeamFormationArchetype form = ClassifyTeamFormation(team);
  float w = GetModuleWeight(e_AIModule_FormationCorridors);

  signed int side = team->GetSide();
  float attSign = (float)(-side);
  Vector3 adapted = candidateSupportPos;
  Vector3 carrierPos = designatedCarrier->GetPosition();
  e_PlayerRole role = player->GetDynamicFormationEntry().role;

  // Read carrier's live intent (if carrier is on our team)
  e_AIActiveIntent carrierIntent = e_AIIntent_None;
  if (designatedCarrier->GetTeamID() == team->GetID()) {
    const PlayerRuntimeState &carrierSt = GetPlayerState(designatedCarrier, now_ms);
    if (now_ms < carrierSt.intentExpire_ms) {
      carrierIntent = carrierSt.activeIntent;
    }
  }

  int myCorridor = GetVerticalCorridorIndex(adapted.coords[1]);
  int carrierCorridor = GetVerticalCorridorIndex(carrierPos.coords[1]);

  // 1. Formation-Specific Structural Nuances (4-4-2 vs 3-5-2 vs 4-3-3)
  if (form == e_Formation_Wingback_352) {
    // In 3-5-2, wide players (LB/RB/LM/RM) provide maximum touchline width in corridors 0 & 4,
    // while the 2 CFs split into central + ball-side half-space
    if (role == e_PlayerRole_LM || role == e_PlayerRole_LB || role == e_PlayerRole_RM || role == e_PlayerRole_RB) {
      float wideSign = (player->GetFormationEntry().position.coords[1] * -side >= 0.0f) ? 1.0f : -1.0f;
      adapted.coords[1] = adapted.coords[1] * (1.0f - 0.28f * w) + (wideSign * 26.0f) * (0.28f * w);
      adapted.coords[0] += attSign * 2.8f * w;
    }
  } else if (form == e_Formation_Balanced_442) {
    // In 4-4-2, if one CF drops short toward ball, partner CF stretches depth
    if (role == e_PlayerRole_CF && designatedCarrier->GetDynamicFormationEntry().role == e_PlayerRole_CF) {
      adapted.coords[0] += attSign * 3.5f * w;
    }
  } else if (form == e_Formation_WideAttack_433) {
    // In 4-3-3, CMs push into half-spaces (corridors 1 & 3) when wingers hold width (0 & 4)
    if (role == e_PlayerRole_CM || role == e_PlayerRole_AM) {
      int targetHalfSpace = (adapted.coords[1] >= 0.0f) ? 3 : 1;
      adapted.coords[1] = adapted.coords[1] * (1.0f - 0.22f * w) + GetCorridorCenterY(targetHalfSpace) * (0.22f * w);
    }
  }

  // 2. React to Carrier's Active Intent (1v1 Isolation Clearing vs La Pausa Check-To-Ball!)
  float distToCarrier = (adapted - carrierPos).GetLength();
  if (carrierIntent == e_AIIntent_TakeOn1v1 && distToCarrier < 18.0f) {
    // Do NOT clog the dribbler's 1v1 corridor! Shift into adjacent half-space for cutback/triangle
    if (myCorridor == carrierCorridor) {
      int adjCorridor = (carrierCorridor == 0) ? 1 : (carrierCorridor == 4 ? 3 : (carrierCorridor < 2 ? carrierCorridor + 1 : carrierCorridor - 1));
      adapted.coords[1] = adapted.coords[1] * (1.0f - 0.45f * w) + GetCorridorCenterY(adjCorridor) * (0.45f * w);
      adapted.coords[0] -= attSign * 1.8f * w; // Offer clean cutback/recycle angle
    }
  } else if (carrierIntent == e_AIIntent_HoldShield && distToCarrier < 22.0f &&
             (role == e_PlayerRole_CM || role == e_PlayerRole_AM || role == e_PlayerRole_DM || role == e_PlayerRole_LM || role == e_PlayerRole_RM)) {
    // Carrier is shielding under press (La Pausa): check toward carrier into an open passing lane!
    Vector3 toCarrier = (carrierPos - adapted).Get2D();
    if (toCarrier.GetLength() > 7.5f) {
      adapted += toCarrier.GetNormalized(Vector3(0)) * (2.6f * w);
    }
  }

  // 3. Anti-Crowding 5-Corridor Discipline: prevent two attacking teammates occupying same corridor & depth
  if (AI_GetMindSet(role) >= 0.35f) {
    std::vector<Player*> mates;
    team->GetActivePlayers(mates);
    for (unsigned int i = 0; i < mates.size(); i++) {
      Player *m = mates[i];
      if (m == player || m == designatedCarrier || m->GetFormationEntry().role == e_PlayerRole_GK) continue;
      // Deterministic priority so only one player shifts
      if (m->GetID() > player->GetID() && AI_GetMindSet(m->GetDynamicFormationEntry().role) <= AI_GetMindSet(role)) continue;

      Vector3 mPos = mentalImage->GetPlayerImage(m->GetID()).position;
      int mCorridor = GetVerticalCorridorIndex(mPos.coords[1]);
      float dx = fabs(mPos.coords[0] - adapted.coords[0]);
      if (mCorridor == GetVerticalCorridorIndex(adapted.coords[1]) && dx < 10.5f) {
        int shiftCorridor = (mCorridor <= 1) ? (mCorridor + 1) : (mCorridor >= 3 ? (mCorridor - 1) : ((adapted.coords[1] >= mPos.coords[1]) ? 3 : 1));
        adapted.coords[1] = adapted.coords[1] * (1.0f - 0.32f * w) + GetCorridorCenterY(shiftCorridor) * (0.32f * w);
        break;
      }
    }
  }

  // 4. Rest-Defense Safety Anchor (CBs and lone DM don't abandon central counter-attack protection)
  if (role == e_PlayerRole_CB) {
    float maxAdvanceX = carrierPos.coords[0] * attSign - 12.0f;
    if (adapted.coords[0] * attSign > maxAdvanceX) {
      adapted.coords[0] = adapted.coords[0] * (1.0f - 0.5f * w) + (maxAdvanceX * attSign) * (0.5f * w);
    }
  }

  adapted.coords[0] = clamp(adapted.coords[0], -pitchHalfW + 1.0f, pitchHalfW - 1.0f);
  adapted.coords[1] = clamp(adapted.coords[1], -pitchHalfH + 1.0f, pitchHalfH - 1.0f);
  return adapted;
}

// ============================================================================
// REAL-TIME HUD / DEBUG CAPTION BUILDER ([F7] IN-MATCH)
// ============================================================================

bool AIAdaptiveRegistry::BuildPlayerHudTag(Player *player, std::string &outCaption, Vector3 &outColor) const {
  const_cast<AIAdaptiveRegistry*>(this)->EnsureInitialized();
  if (!masterEnabled || hudMode == 0) return false;
  if (!player || !player->IsActive()) return false;

  unsigned long now_ms = player->GetTeam()->GetMatch()->GetActualTime_ms();
  int slot = ((player->GetID() % 64) + 64) % 64;
  if (playerStateOwnerID[slot] != player->GetID()) return false;

  const PlayerRuntimeState &st = playerStates[slot];
  bool hasFreshIntent = (now_ms <= st.intentExpire_ms && st.activeIntent != e_AIIntent_None);
  bool isCarrier = (player->GetTeam()->GetMatch()->GetDesignatedPossessionPlayer() == player);

  if (hudMode == 1) {
    // Mode 1: Clean, non-cluttered tags only on active ball carrier or lunging player
    if (!hasFreshIntent) return false;
    if (!isCarrier && st.activeIntent != e_AIIntent_LooseBallLunge) return false;
  }

  outColor = Vector3(210, 230, 255);
  if (hasFreshIntent) {
    switch (st.activeIntent) {
      case e_AIIntent_TakeOn1v1:
        outColor = Vector3(255, 195, 55); // Gold highlight for 1v1 Take-On!
        break;
      case e_AIIntent_HoldShield:
        outColor = Vector3(120, 235, 255); // Cyan highlight for La Pausa / Shield!
        break;
      case e_AIIntent_Pass:
        outColor = Vector3(145, 255, 155); // Crisp green for Vision-Aware Pass
        break;
      case e_AIIntent_Shot:
        outColor = Vector3(255, 110, 110); // Red for Shot
        break;
      case e_AIIntent_LooseBallLunge:
        outColor = Vector3(255, 160, 220); // Magenta for Lunge
        break;
      default:
        outColor = Vector3(225, 235, 245);
        break;
    }
    outCaption = std::string("[") + GetIntentShortName(st.activeIntent) + "]";
    if (hudMode == 2 && st.cachedArchetype != e_AIArchetype_Balanced) {
      outCaption += std::string(" ") + GetArchetypeShortName(st.cachedArchetype);
    }
    return true;
  }

  if (hudMode == 2 && player->GetFormationEntry().role != e_PlayerRole_GK) {
    outCaption = GetArchetypeShortName(st.cachedArchetype);
    outColor = Vector3(185, 200, 215);
    return true;
  }

  return false;
}

bool AIAdaptiveRegistry::BuildMentalImageHudSummary(Match *match, std::string &outLine1,
                                                    std::string &outLine2,
                                                    Vector3 &outAccentColor) const {
  if (!match || hudMode <= 0 || (!match->IsInPlay() && !match->IsInSetPiece())) {
    return false;
  }

  Player *carrier = nullptr;
  if (match->IsInSetPiece() && match->GetRefereeBuffer().taker && match->GetRefereeBuffer().taker->IsActive()) {
    carrier = match->GetRefereeBuffer().taker;
  } else {
    carrier = match->GetDesignatedPossessionPlayer();
  }
  if (!carrier || !carrier->IsActive()) return false;

  PlayerController *ctrl = static_cast<PlayerController*>(carrier->GetController());
  const MentalImage *mi = (ctrl && ctrl->GetMentalImage()) ? ctrl->GetMentalImage() : match->GetMentalImage(0);
  if (!mi) return false;

  int baseReact_ms = ctrl ? ctrl->GetReactionTime_ms() : 0;
  int snapLag_ms = mi->GetTimeStampNeg_ms();

  int totalPlayers = 0;
  float sumDev = 0.0f;
  float maxDev = 0.0f;
  for (int t = 0; t < 2; t++) {
    Team *tm = match->GetTeam(t);
    if (!tm) continue;
    std::vector<Player*> active;
    tm->GetActivePlayers(active);
    for (size_t i = 0; i < active.size(); i++) {
      Player *p = active[i];
      if (!p || !p->IsActive()) continue;
      PlayerImage img = mi->GetPlayerImage(p->GetID());
      float dev = (img.position.Get2D() - p->GetPosition().Get2D()).GetLength();
      sumDev += dev;
      if (dev > maxDev) maxDev = dev;
      totalPlayers++;
    }
  }
  float avgDev = (totalPlayers > 0) ? (sumDev / (float)totalPlayers) : 0.0f;

  outAccentColor = (snapLag_ms == 0) ? Vector3(85, 245, 175) : Vector3(95, 210, 255);

  char buf1[160];
  char buf2[160];
  std::string lastName = carrier->GetPlayerData()->GetLastName();
  if (lastName.size() > 14) lastName = lastName.substr(0, 14);
  std::snprintf(buf1, sizeof(buf1), "[F7] MENTAL IMAGE  |  %s  |  LAG: %d MS  (REACT: %d MS)",
                lastName.c_str(), snapLag_ms, baseReact_ms);
  std::snprintf(buf2, sizeof(buf2), "360 OMNI (%dP)  |  AVG OFFSET: %.2fM  |  MAX: %.2fM / 2.50M",
                totalPlayers, avgDev, maxDev);
  outLine1 = buf1;
  outLine2 = buf2;
  return true;
}

void AIAdaptiveRegistry::CaptureVisualDebugSnapshot(Match *match) {
  EnsureInitialized();
  buf_visualSnapshot.lines.clear();
  buf_visualSnapshot.active = false;

  if (!match || hudMode <= 0 || (!match->IsInPlay() && !match->IsInSetPiece())) {
    return;
  }

  Player *carrier = nullptr;
  if (match->IsInSetPiece() && match->GetRefereeBuffer().taker && match->GetRefereeBuffer().taker->IsActive()) {
    carrier = match->GetRefereeBuffer().taker;
  } else {
    carrier = match->GetDesignatedPossessionPlayer();
  }
  if (!carrier || !carrier->IsActive()) return;

  PlayerController *ctrl = static_cast<PlayerController*>(carrier->GetController());
  const MentalImage *mi = (ctrl && ctrl->GetMentalImage()) ? ctrl->GetMentalImage() : match->GetMentalImage(0);
  if (!mi) return;

  buf_visualSnapshot.active = true;
  int carrierSlot = ((carrier->GetID() % 64) + 64) % 64;
  const PlayerRuntimeState *carrierSt = (playerStateOwnerID[carrierSlot] == carrier->GetID()) ? &playerStates[carrierSlot] : nullptr;

  Vector3 carrierPos2D = carrier->GetPosition().Get2D();
  Vector3 carrierOrigin = carrierPos2D + Vector3(0, 0, 0.08f);
  float attSign = (float)(-carrier->GetTeam()->GetSide());
  Vector3 fallbackDir(attSign, 0, 0);
  Vector3 moveDir = carrier->GetDirectionVec().Get2D().GetNormalized(
      carrier->GetBodyDirectionVec().Get2D().GetNormalized(fallbackDir));
  Vector3 perpDir(-moveDir.coords[1], moveDir.coords[0], 0.0f);

  auto pushDiamondMarker = [&](const Vector3 &center, float radius, const Vector3 &color, int alpha, int thickness) {
    Vector3 n = center + Vector3(0.0f, radius, 0.0f);
    Vector3 e = center + Vector3(radius, 0.0f, 0.0f);
    Vector3 s = center + Vector3(0.0f, -radius, 0.0f);
    Vector3 w = center + Vector3(-radius, 0.0f, 0.0f);
    VisualLinePrimitive seg;
    seg.color = color;
    seg.alpha = alpha;
    seg.thickness = thickness;
    seg.arrowHead = false;
    seg.from = n; seg.to = e; buf_visualSnapshot.lines.push_back(seg);
    seg.from = e; seg.to = s; buf_visualSnapshot.lines.push_back(seg);
    seg.from = s; seg.to = w; buf_visualSnapshot.lines.push_back(seg);
    seg.from = w; seg.to = n; buf_visualSnapshot.lines.push_back(seg);
  };

  // 1. Observer Anchor, Move Direction & Original 180-deg bodyDirPenalty Hemisphere Divider
  pushDiamondMarker(carrierOrigin, 0.78f, Vector3(225, 250, 255), 235, 2);

  VisualLinePrimitive hemiBar;
  hemiBar.from = carrierOrigin - perpDir * 3.0f;
  hemiBar.to = carrierOrigin + perpDir * 3.0f;
  hemiBar.color = Vector3(160, 220, 255);
  hemiBar.alpha = 185;
  hemiBar.thickness = 2;
  hemiBar.arrowHead = false;
  buf_visualSnapshot.lines.push_back(hemiBar);

  VisualLinePrimitive moveArrow;
  moveArrow.from = carrierOrigin;
  moveArrow.to = carrierOrigin + moveDir * 3.1f;
  moveArrow.color = Vector3(255, 250, 205);
  moveArrow.alpha = 235;
  moveArrow.thickness = 2;
  moveArrow.arrowHead = true;
  buf_visualSnapshot.lines.push_back(moveArrow);

  // 2. All Active Players' Perceived MentalImage Positions, Lag Offsets & Extrapolated Velocities
  int carrierTeamID = carrier->GetTeamID();
  for (int t = 0; t < 2; t++) {
    Team *tm = match->GetTeam(t);
    if (!tm) continue;
    std::vector<Player*> active;
    tm->GetActivePlayers(active);
    bool isTeammate = (t == carrierTeamID);
    Vector3 markerColor = isTeammate ? Vector3(80, 240, 195) : Vector3(255, 110, 85);

    for (size_t i = 0; i < active.size(); i++) {
      Player *p = active[i];
      if (!p || !p->IsActive()) continue;

      PlayerImage img = mi->GetPlayerImage(p->GetID());
      Vector3 realPos2D = p->GetPosition().Get2D();
      Vector3 miPos2D = img.position.Get2D();
      Vector3 realOrigin = realPos2D + Vector3(0, 0, 0.08f);
      Vector3 miOrigin = miPos2D + Vector3(0, 0, 0.08f);

      if (p != carrier) {
        pushDiamondMarker(miOrigin, 0.58f, markerColor, 220, 2);
      }

      // Magenta tether from Real World position -> Perceived MentalImage position when lag/extrapolation differs
      float lagDist = (miPos2D - realPos2D).GetLength();
      if (lagDist > 0.04f) {
        VisualLinePrimitive lagTether;
        lagTether.from = realOrigin;
        lagTether.to = miOrigin;
        lagTether.color = Vector3(255, 85, 235);
        lagTether.alpha = 240;
        lagTether.thickness = 2;
        lagTether.arrowHead = false;
        buf_visualSnapshot.lines.push_back(lagTether);
      }

      // Perceived movement vector inside MentalImage
      Vector3 miVel2D = img.movement.Get2D();
      if (p != carrier && miVel2D.GetLength() > 0.65f) {
        VisualLinePrimitive velVec;
        velVec.from = miOrigin;
        velVec.to = miOrigin + miVel2D * 0.34f;
        velVec.color = markerColor;
        velVec.alpha = 195;
        velVec.thickness = 1;
        velVec.arrowHead = true;
        buf_visualSnapshot.lines.push_back(velVec);
      }
    }
  }

  // 3. Perceived Teammate Pass Links Colored by Original bodyDirPenalty (Front 180 = 1.0, Rear 180 = 0.3..1.0)
  Team *attTeam = carrier->GetTeam();
  std::vector<Player*> mates;
  attTeam->GetActivePlayers(mates);
  int bestPassTargetID = (carrierSt ? carrierSt->lastBestPassTargetID : -1);

  struct PerceivedMateLink {
    Player *p;
    Vector3 miPos2D;
    float dist;
    float dot;
  };
  std::vector<PerceivedMateLink> mateLinks;
  for (size_t i = 0; i < mates.size(); i++) {
    Player *m = mates[i];
    if (!m || m == carrier || m->GetFormationEntry().role == e_PlayerRole_GK) continue;
    Vector3 mMiPos2D = mi->GetPlayerImage(m->GetID()).position.Get2D();
    float d = (mMiPos2D - carrierPos2D).GetLength();
    if (d < 3.0f || d > 42.0f) continue;
    Vector3 toMate = (mMiPos2D - carrierPos2D).GetNormalized(fallbackDir);
    float dot = moveDir.GetDotProduct(toMate);
    mateLinks.push_back({m, mMiPos2D, d, dot});
  }
  std::sort(mateLinks.begin(), mateLinks.end(), [](const PerceivedMateLink &a, const PerceivedMateLink &b) {
    return a.dist < b.dist;
  });

  const int maxMateLinks = 6;
  bool drewBestPass = false;
  for (size_t i = 0; i < mateLinks.size() && (int)i < maxMateLinks; i++) {
    VisualLinePrimitive link;
    link.from = carrierOrigin;
    link.to = mateLinks[i].miPos2D + Vector3(0, 0, 0.08f);
    if (mateLinks[i].p->GetID() == bestPassTargetID) {
      link.color = Vector3(80, 255, 125);
      link.alpha = 245;
      link.thickness = 2;
      link.arrowHead = true;
      drewBestPass = true;
    } else if (mateLinks[i].dot >= 0.0f) {
      // Front 180-deg hemisphere: original bodyDirPenalty == 1.00 (no penalty)
      link.color = Vector3(105, 235, 190);
      link.alpha = 165;
      link.thickness = 1;
      link.arrowHead = false;
    } else {
      // Rear 180-deg hemisphere: original bodyDirPenalty scaled 0.30..0.99
      link.color = Vector3(245, 145, 70);
      link.alpha = 160;
      link.thickness = 1;
      link.arrowHead = false;
    }
    buf_visualSnapshot.lines.push_back(link);
  }

  if (!drewBestPass && bestPassTargetID != -1) {
    Player *bestTarget = match->GetPlayer(bestPassTargetID);
    if (bestTarget && bestTarget->IsActive() && bestTarget->GetTeamID() == carrierTeamID) {
      Vector3 bestMiPos = mi->GetPlayerImage(bestPassTargetID).position.Get2D() + Vector3(0, 0, 0.08f);
      VisualLinePrimitive link;
      link.from = carrierOrigin;
      link.to = bestMiPos;
      link.color = Vector3(80, 255, 125);
      link.alpha = 245;
      link.thickness = 2;
      link.arrowHead = true;
      buf_visualSnapshot.lines.push_back(link);
    }
  }

  // 4. MentalImage Perceived Ball Prediction Curve (0ms .. 900ms)
  const unsigned int predSteps_ms[7] = {0, 150, 300, 450, 600, 750, 900};
  Vector3 predPts[7];
  for (int k = 0; k < 7; k++) {
    Vector3 bp = mi->GetBallPrediction(predSteps_ms[k]);
    bp.coords[2] = std::max(0.08f, bp.coords[2]);
    predPts[k] = bp;
  }
  float totalBallPath = (predPts[6] - predPts[0]).GetLength();
  if (totalBallPath > 0.35f) {
    for (int k = 0; k < 6; k++) {
      if ((predPts[k + 1] - predPts[k]).GetLength() < 0.04f) continue;
      VisualLinePrimitive ballSeg;
      ballSeg.from = predPts[k];
      ballSeg.to = predPts[k + 1];
      ballSeg.color = Vector3(255, 225, 65);
      ballSeg.alpha = 235;
      ballSeg.thickness = 2;
      ballSeg.arrowHead = (k == 5 && totalBallPath > 1.2f);
      buf_visualSnapshot.lines.push_back(ballSeg);
    }
  }
}

void AIAdaptiveRegistry::FetchVisualDebugSnapshot() {
  fetched_visualSnapshot = buf_visualSnapshot;
}

static bool ProjectWorldToScreenOverlay(Match *match, const Vector3 &worldPos, int &outX, int &outY) {
  if (!std::isfinite(worldPos.coords[0]) || !std::isfinite(worldPos.coords[1]) || !std::isfinite(worldPos.coords[2])) {
    return false;
  }
  Vector3 camPos = match->GetCamera()->GetDerivedPosition();
  Vector3 camFwd = match->GetCamera()->GetDerivedRotation() * Vector3(0, 0, -1);
  if ((worldPos - camPos).GetDotProduct(camFwd) <= 1.15f) {
    return false;
  }
  Vector3 proj = GetProjectedCoord(worldPos, match->GetCamera());
  if (!std::isfinite(proj.coords[0]) || !std::isfinite(proj.coords[1]) ||
      proj.coords[0] < -2400.0f || proj.coords[0] > 2500.0f ||
      proj.coords[1] < -2400.0f || proj.coords[1] > 2500.0f) {
    return false;
  }
  int dudW = 0, dudH = 0;
  GetMenuTask()->GetWindowManager()->GetCoordinates(proj.coords[0], proj.coords[1], 1, 1, outX, outY, dudW, dudH);
  return true;
}

// Clips a ray endpoint from an in-front anchor against the camera near plane so large 38m / 220° cones never vanish!
static bool ProjectWorldRayClippedOverlay(Match *match, const Vector3 &anchorWorld, const Vector3 &targetWorld,
                                          int &outX, int &outY) {
  Vector3 camPos = match->GetCamera()->GetDerivedPosition();
  Vector3 camFwd = match->GetCamera()->GetDerivedRotation() * Vector3(0, 0, -1);
  float dAnchor = (anchorWorld - camPos).GetDotProduct(camFwd);
  float dTarget = (targetWorld - camPos).GetDotProduct(camFwd);
  const float kNearPlane = 1.25f;

  Vector3 safeWorld = targetWorld;
  if (dTarget < kNearPlane) {
    if (dAnchor <= kNearPlane + 0.05f) return false;
    float t = clamp((dAnchor - kNearPlane) / (dAnchor - dTarget), 0.0f, 1.0f);
    safeWorld = anchorWorld + (targetWorld - anchorWorld) * t;
  }
  return ProjectWorldToScreenOverlay(match, safeWorld, outX, outY);
}

static inline void ExpandDirtyBounds(int x, int y, int &minX, int &minY, int &maxX, int &maxY) {
  if (x < minX) minX = x;
  if (y < minY) minY = y;
  if (x > maxX) maxX = x;
  if (y > maxY) maxY = y;
}

static void DrawScreenLineThick(blunted::intrusive_ptr<Image2D> &overlay,
                                int x1, int y1, int x2, int y2,
                                const Vector3 &color, int alpha, int thickness, bool arrowHead,
                                int &minX, int &minY, int &maxX, int &maxY) {
  ExpandDirtyBounds(x1, y1, minX, minY, maxX, maxY);
  ExpandDirtyBounds(x2, y2, minX, minY, maxX, maxY);

  Line baseLine;
  baseLine.SetVertex(0, Vector3((float)x1, (float)y1, 0));
  baseLine.SetVertex(1, Vector3((float)x2, (float)y2, 0));
  overlay->DrawLine(baseLine, color, alpha);

  float dx = (float)(x2 - x1);
  float dy = (float)(y2 - y1);
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.0f) return;

  float nx = -dy / len;
  float ny = dx / len;

  if (thickness >= 2) {
    Line off1;
    off1.SetVertex(0, Vector3(x1 + nx, y1 + ny, 0));
    off1.SetVertex(1, Vector3(x2 + nx, y2 + ny, 0));
    overlay->DrawLine(off1, color, alpha);
  }
  if (thickness >= 3) {
    Line off2;
    off2.SetVertex(0, Vector3(x1 - nx, y1 - ny, 0));
    off2.SetVertex(1, Vector3(x2 - nx, y2 - ny, 0));
    overlay->DrawLine(off2, color, alpha);
  }

  if (arrowHead && len > 10.0f) {
    float ux = dx / len;
    float uy = dy / len;
    float headLen = 11.0f + thickness * 2.2f;
    float headHalfW = 5.0f + thickness * 1.4f;
    Triangle head;
    head.SetVertex(0, Vector3((float)x2, (float)y2, 0));
    head.SetVertex(1, Vector3(x2 - ux * headLen + nx * headHalfW, y2 - uy * headLen + ny * headHalfW, 0));
    head.SetVertex(2, Vector3(x2 - ux * headLen - nx * headHalfW, y2 - uy * headLen - ny * headHalfW, 0));
    overlay->DrawTriangle(head, color, alpha);
  }
}

void AIAdaptiveRegistry::RenderVisualDebugOverlay(Match *match) {
  blunted::intrusive_ptr<Image2D> overlay = GetDebugOverlay();
  if (overlay == blunted::intrusive_ptr<Image2D>()) return;

  int contextW = 0, contextH = 0, bpp = 0;
  GetScene2D()->GetContextSize(contextW, contextH, bpp);
  if (contextW <= 0 || contextH <= 0) return;

  bool shouldDraw = (fetched_visualSnapshot.active && hudMode > 0 && !match->GetPause());
  if (!shouldDraw) {
    if (wasOverlayDrawnLastFrame) {
      if (GetDebugMode() != e_DebugMode_AI) {
        if (prevDirtyW > 0 && prevDirtyH > 0) {
          overlay->DrawRectangle(prevDirtyX, prevDirtyY, prevDirtyW, prevDirtyH, Vector3(0, 0, 0), 0);
          overlay->OnChange();
        }
        overlay->Disable();
      }
      prevDirtyW = 0;
      prevDirtyH = 0;
      wasOverlayDrawnLastFrame = false;
    }
    return;
  }

  if (!overlay->IsEnabled()) {
    overlay->SetPokePriority(-1);
    overlay->Enable();
  }

  unsigned long nowWall_ms = EnvironmentManager::GetInstance().GetTime_ms();
  if (GetDebugMode() != e_DebugMode_AI && wasOverlayDrawnLastFrame &&
      (long)(nowWall_ms - lastOverlayUpload_ms) < 28 && (long)(nowWall_ms - lastOverlayUpload_ms) >= 0) {
    return;
  }
  lastOverlayUpload_ms = nowWall_ms;

  if (GetDebugMode() != e_DebugMode_AI) {
    if (prevDirtyW > 0 && prevDirtyH > 0) {
      overlay->DrawRectangle(prevDirtyX, prevDirtyY, prevDirtyW, prevDirtyH, Vector3(0, 0, 0), 0);
    } else if (!wasOverlayDrawnLastFrame) {
      overlay->DrawRectangle(0, 0, contextW, contextH, Vector3(0, 0, 0), 0);
    }
  }

  int minX = contextW, minY = contextH, maxX = 0, maxY = 0;

  // Render MentalImage Perceived Positions, Lag Tethers, Pass Links & Ball Prediction
  for (size_t i = 0; i < fetched_visualSnapshot.lines.size(); i++) {
    const VisualLinePrimitive &ln = fetched_visualSnapshot.lines[i];
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (ProjectWorldRayClippedOverlay(match, ln.from, ln.from, x1, y1) &&
        ProjectWorldRayClippedOverlay(match, ln.from, ln.to, x2, y2)) {
      DrawScreenLineThick(overlay, x1, y1, x2, y2,
                          ln.color, ln.alpha, ln.thickness, ln.arrowHead, minX, minY, maxX, maxY);
    }
  }

  if (minX <= maxX && minY <= maxY) {
    const int pad = 20;
    int cx0 = clamp(minX - pad, 0, contextW);
    int cy0 = clamp(minY - pad, 0, contextH);
    int cx1 = clamp(maxX + pad, 0, contextW);
    int cy1 = clamp(maxY + pad, 0, contextH);
    prevDirtyX = cx0;
    prevDirtyY = cy0;
    prevDirtyW = std::max(0, cx1 - cx0);
    prevDirtyH = std::max(0, cy1 - cy0);
  } else {
    prevDirtyW = 0;
    prevDirtyH = 0;
  }

  wasOverlayDrawnLastFrame = true;
  if (GetDebugMode() != e_DebugMode_AI) {
    overlay->OnChange();
  }
}
