// written by bastiaan konings schuiling 2008 - 2015
// Broadcast Bottom-Corner Player Nameplates & Stamina Bars (mapped dynamically to Left/Right pitch sides)

#ifndef _HPP_GUI2_VIEW_PLAYERHUD
#define _HPP_GUI2_VIEW_PLAYERHUD

#include "utils/gui2/view.hpp"
#include "scene/objects/image2d.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

#include <string>

using namespace blunted;

class Match;
class Team;
class Player;

class Gui2PlayerHUD : public Gui2View {

  public:
    Gui2PlayerHUD(Gui2WindowManager *windowManager, Match *match);
    virtual ~Gui2PlayerHUD();

    void GetImages(std::vector < blunted::intrusive_ptr<Image2D> > &target);

    void PreparePutBuffers();
    void FetchPutBuffers();
    virtual void Put();
    virtual void Redraw();

  protected:
    struct SidePanelSnapshot {
      bool valid = false;
      int teamID = 0;
      int playerID = -1;
      int staminaPct = 100; // [0..100]
      std::string displayLabel;
    };

    Player *ResolveDisplayedPlayer(Team *team) const;
    int ComputeSquadNumber(Team *team, Player *player) const;
    std::string FormatBroadcastPlayerName(Player *player, int squadNum, bool isRightSide) const;
    void DrawFallbackCrest(Gui2Image *crestImg, const Vector3 &col1, const Vector3 &col2);
    void RedrawSidePlate(int slot, int teamID, int staminaPct);
    void UpdateSidePlateStaminaOnly(int slot, int staminaPct);

    Match *match;

    float cardWidthPct;
    float cardHeightPct;
    float leftCardXPct;
    float rightCardXPct;
    float cardYPct;
    float crestWidthPct;

    // Indexed by slot: [0] = Left pitch side (GetSide() == -1), [1] = Right pitch side (GetSide() == +1)
    Gui2Image *plateImg[2];
    Gui2Caption *nameCaption[2];

    // Indexed by teamID (0 or 1), repositioned smoothly when teams swap sides at half-time!
    Gui2Image *teamCrestImg[2];

    SidePanelSnapshot buf_panels[2];
    SidePanelSnapshot fetched_panels[2];

    struct VisionHudSnapshot {
      bool visible = false;
      std::string line1;
      std::string line2;
      Vector3 accentColor = Vector3(95, 210, 255);
    };
    Gui2Image *visionCardBg;
    Gui2Caption *visionLine1Cap;
    Gui2Caption *visionLine2Cap;
    VisionHudSnapshot buf_visionHud;
    VisionHudSnapshot fetched_visionHud;
    bool cachedVisionVisible;
    std::string cachedVisionLine1;
    std::string cachedVisionLine2;
    Vector3 cachedVisionAccent;
    void RedrawVisionCardBg(const Vector3 &accentColor);

    int cachedTeamID[2];
    int cachedPlayerID[2];
    int cachedStaminaPct[2];
    std::string cachedLabel[2];
    int cachedBarTrackX[2] = {0, 0};
    int cachedBarTrackY[2] = {0, 0};
    int cachedBarTrackW[2] = {0, 0};
    int cachedBarTrackH[2] = {0, 0};
    int prepCachedTeamID[2] = {-1, -1};
    int prepCachedPlayerID[2] = {-1, -1};
    std::string prepCachedLabel[2];
    unsigned long lastVisionTextUpdate_ms = 0;
};

#endif
