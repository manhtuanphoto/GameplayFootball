// Player-Centric Adaptive AI Pipeline for GameplayFootball
// Modular, weight-adaptive, hot-removable AI behavior layers over baseline AI.

#ifndef _HPP_AISUPPORT_ADAPTIVE_PIPELINE
#define _HPP_AISUPPORT_ADAPTIVE_PIPELINE

#include "../../gamedefines.hpp"
#include "mentalimage.hpp"

#include <array>
#include <string>
#include <vector>

class Match;
class Team;
class Player;

enum e_AIModuleId {
  e_AIModule_VisionPerception = 0,     // [1] FOV cone (mental_vision) & defender momentum reading
  e_AIModule_PlayerDisposition = 1,    // [2] 22-stat player archetype & pitch-zone risk gating
  e_AIModule_TakeOn1v1 = 2,            // [3] Active 1v1 Take-On (hesitation + diagonal beat-defender burst)
  e_AIModule_HoldAndShield = 3,        // [4] La Pausa & physical ball shielding under press
  e_AIModule_TurnBeforeRun = 4,        // [5] Biomechanical turn-before-sprint & body-angle kick cost
  e_AIModule_LooseBallBurstLunge = 5,  // [6] First-step burst, loose-ball lunge reach & ball-tracking gaze
  e_AIModule_FormationCorridors = 6,   // [7] 5-corridor Positional Play & 4-4-2 / 3-5-2 support triangles
  e_AIModule_COUNT = 7
};

enum e_AIPlayerArchetype {
  e_AIArchetype_Balanced = 0,
  e_AIArchetype_Playmaker,
  e_AIArchetype_WingerDribbler,
  e_AIArchetype_PressResistant,
  e_AIArchetype_Poacher,
  e_AIArchetype_Stopper
};

enum e_AIActiveIntent {
  e_AIIntent_None = 0,
  e_AIIntent_Pass,
  e_AIIntent_CarryOpen,
  e_AIIntent_TakeOn1v1,
  e_AIIntent_HoldShield,
  e_AIIntent_Shot,
  e_AIIntent_Clearance,
  e_AIIntent_LooseBallLunge,
  e_AIIntent_CorridorSupport
};

enum e_TeamFormationArchetype {
  e_Formation_Balanced_442 = 0,
  e_Formation_Wingback_352,
  e_Formation_WideAttack_433,
  e_Formation_Compact_451
};

struct AIModuleConfig {
  e_AIModuleId id;
  const char *configSlug;
  const char *shortName;
  const char *displayName;
  bool enabled;
  float weight; // [0.0 .. 2.0], default 1.0
};

struct DefenderMomentumInfo {
  bool valid = false;
  int playerID = -1;
  Vector3 position;
  Vector3 velocity;
  float distance = 30.0f;
  float forwardAlignment = 0.0f;   // dot(toDefender, attackDir)
  float closingSpeed = 0.0f;       // m/s closing toward carrier
  float lateralVel = 0.0f;         // defender velocity perpendicular to carrier->defender line
  float wrongFootScore = 0.0f;     // [0..1] how exploitable defender's committed momentum is
  int coverDefendersBehind = 0;    // count of 2nd-line defenders in cover shadow behind primary
  float closestAnyDist = 30.0f;
  Vector3 closestAnyPos;
};

struct PlayerDispositionProfile {
  e_AIPlayerArchetype archetype = e_AIArchetype_Balanced;
  float playmakerBias = 0.5f;
  float takeOn1v1Bias = 0.5f;
  float holdShieldBias = 0.5f;
  float composureUnderPress = 0.5f;
  float offBallWorkRate = 0.5f;
  float defensiveDiscipline = 0.5f;
  float takeOnRiskAllowance = 0.5f; // 0.0 if GK or last-line defender, up to 1.0 in attacking wing/half-space
  bool isLastLineDefender = false;
};

struct PassCandidateContext {
  Player *carrier = nullptr;
  Player *target = nullptr;
  e_FunctionType passType = e_FunctionType_ShortPass;
  float passDist = 0.0f;
  float passOdds = 0.0f;
  float underPressure = 0.0f;
  bool isThroughBall = false;
  bool isSwitchPlay = false;
  bool isWideCross = false;
};

struct OnBallArbitrationInput {
  Match *match = nullptr;
  Player *carrier = nullptr;
  const MentalImage *mentalImage = nullptr;
  const std::vector<PlayerImage> *opponentImages = nullptr;
  float baselineDribbleUtility = 0.0f;
  float bestPassUtility = -1.0f;
  float bestPassOdds = 0.0f;
  Player *bestPassTarget = nullptr;
  e_FunctionType bestPassType = e_FunctionType_ShortPass;
  bool baselineShouldShoot = false;
  bool is1v1Keeper = false;
  float bestShotUtility = -1.0f;
  float bestShotTargetY = 0.0f;
  float underPressure = 0.0f;
  Vector3 baselineDribbleDir;
  float baselineDribbleVel = 0.0f;
};

struct OnBallArbitrationResult {
  e_AIActiveIntent winningIntent = e_AIIntent_CarryOpen;
  bool executeShot = false;
  bool executePass = false;
  bool suppressPanicClearance = false;
  bool requestKnockOn = false;
  Vector3 adaptedDribbleDir;
  float adaptedDribbleVel = 0.0f;
  float winningConfidence = 0.5f;
};

class AIAdaptiveRegistry {
public:
  static AIAdaptiveRegistry &Instance();

  void EnsureInitialized();
  void SaveToConfig();
  void ResetRuntimeStates();

  bool IsMasterEnabled() const { return masterEnabled; }
  void SetMasterEnabled(bool enabled);
  bool ToggleMasterEnabled();

  bool IsModuleEnabled(e_AIModuleId id) const;
  float GetModuleWeight(e_AIModuleId id) const;
  bool ToggleModule(e_AIModuleId id);
  void SetModuleEnabled(e_AIModuleId id, bool enabled);
  void SetModuleWeight(e_AIModuleId id, float weight);
  const AIModuleConfig &GetModuleConfig(e_AIModuleId id) const;

  // HUD / Debug caption mode: 0 = Off, 1 = On-Ball & Key Intent Tags, 2 = Full Archetype + Intent Tags
  int GetHudMode() const { return hudMode; }
  int CycleHudMode();
  std::string BuildStatusSummary() const;

  // Layer 1: Opponent Momentum Analysis (reads perceived MentalImage positions)
  DefenderMomentumInfo AnalyzeDefenderSituations(Match *match, Player *carrier,
                                                 const std::vector<PlayerImage> &opponentImages) const;

  // Layer 2: 22-Stat Player Disposition & Pitch-Zone Risk Safety
  PlayerDispositionProfile BuildDispositionProfile(Match *match, Player *player) const;
  static const char *GetArchetypeShortName(e_AIPlayerArchetype archetype);
  static const char *GetIntentShortName(e_AIActiveIntent intent);

  // Layer 3: Pluggable On-Ball Intent Evaluators & Arbiter
  float AdaptPassCandidateUtility(const PassCandidateContext &ctx,
                                  const PlayerDispositionProfile &profile,
                                  float baseUtility) const;

  OnBallArbitrationResult ArbitrateOnBallIntent(const OnBallArbitrationInput &in,
                                                const PlayerDispositionProfile &profile,
                                                const DefenderMomentumInfo &defInfo);

  // Layer 4A & 4B: Off-BallLoose-Ball Burst, Lunge, Gaze & Turn-Before-Run
  void AdaptOffBallPursuitAndLunge(Match *match, const MentalImage *mentalImage, Player *player,
                                   bool teamHasBestPossession, float possessionAmount,
                                   Vector3 &inputDirection, float &inputVelocityFloat,
                                   bool &forceMagnet, bool &extraHaste, bool &byAnyMeansInterfere);

  void AdaptFinalMovementCommand(Match *match, const MentalImage *mentalImage, Player *player,
                                 bool isOnBallCarrier, PlayerCommand &moveCmd);

  // Layer 4C: 5-Corridor Positional Play & Formation Support (4-4-2 / 3-5-2 / 4-3-3)
  e_TeamFormationArchetype ClassifyTeamFormation(Team *team);
  Vector3 AdaptFormationBaseShape(Team *team, Player *player, e_PlayerRole role,
                                  float possessionBias, const Vector3 &baseDesiredPos);
  Vector3 AdaptSupportPositionCorridors(Match *match, const MentalImage *mentalImage,
                                        Player *player, Player *designatedCarrier,
                                        const Vector3 &candidateSupportPos);

  // Visual Debug / HUD Inspection ([F7] MentalImage Inspector)
  bool BuildPlayerHudTag(Player *player, std::string &outCaption, Vector3 &outColor) const;
  bool BuildMentalImageHudSummary(Match *match, std::string &outLine1,
                                  std::string &outLine2, Vector3 &outAccentColor) const;
  void CaptureVisualDebugSnapshot(Match *match);
  void FetchVisualDebugSnapshot();
  void RenderVisualDebugOverlay(Match *match);

private:
  AIAdaptiveRegistry();

  struct VisualLinePrimitive {
    Vector3 from;
    Vector3 to;
    Vector3 color;
    int alpha = 200;
    int thickness = 1;
    bool arrowHead = false;
  };

  struct VisualDebugSnapshot {
    bool active = false;
    std::vector<VisualLinePrimitive> lines;
  };

  struct PlayerRuntimeState {
    unsigned long lastUpdate_ms = 0;
    e_AIActiveIntent activeIntent = e_AIIntent_None;
    unsigned long intentExpire_ms = 0;
    e_AIPlayerArchetype cachedArchetype = e_AIArchetype_Balanced;

    // Hysteresis for 1v1 Take-On beat direction (+1 or -1)
    int lockedBeatSide = 0;
    int lockedDefenderID = -1;
    unsigned long beatLockUntil_ms = 0;

    // Tracking duration of continuous Hold/Shield (La Pausa) so player eventually releases ball
    unsigned long shieldStart_ms = 0;
    bool wasShielding = false;

    // Cached live decision targets for [F7] MentalImage inspector
    int lastBestPassTargetID = -1;
    int lastPrimaryDefID = -1;
    float lastShotTargetY = 0.0f;
    Vector3 lastAdaptedDir;
  };

  struct TeamFormationCache {
    unsigned long lastCheck_ms = 0;
    e_TeamFormationArchetype archetype = e_Formation_Balanced_442;
    int cbCount = 2;
    int cfCount = 2;
  };

  bool initialized = false;
  bool masterEnabled = true;
  int hudMode = 1; // 0 = OFF, 1 = ON ([F7] Vision Cone & Player Interactions)
  std::array<AIModuleConfig, e_AIModule_COUNT> modules;
  mutable std::array<PlayerRuntimeState, 64> playerStates; // Indexed by player->GetID() % 64 with ID check
  mutable std::array<int, 64> playerStateOwnerID;
  std::array<TeamFormationCache, 2> teamFormations;
  VisualDebugSnapshot buf_visualSnapshot;
  VisualDebugSnapshot fetched_visualSnapshot;
  bool wasOverlayDrawnLastFrame = false;
  unsigned long lastOverlayUpload_ms = 0;
  int prevDirtyX = 0;
  int prevDirtyY = 0;
  int prevDirtyW = 0;
  int prevDirtyH = 0;

  PlayerRuntimeState &GetPlayerState(Player *player, unsigned long now_ms) const;
};

#endif
