// Sandbox quick AI vs AI match setup page

#ifndef _HPP_MENU_SANDBOX
#define _HPP_MENU_SANDBOX

#include "utils/gui2/windowmanager.hpp"

#include "utils/gui2/widgets/menu.hpp"
#include "utils/gui2/widgets/root.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/widgets/slider.hpp"
#include "utils/gui2/widgets/iconselector.hpp"
#include "utils/gui2/widgets/caption.hpp"

#include "../../onthepitch/match.hpp"

using namespace blunted;

class SandboxPage : public Gui2Page {

  public:
    SandboxPage(Gui2WindowManager *windowManager, const Gui2PageData &pageData);
    virtual ~SandboxPage();

    void FocusTeamSelect1();
    void FocusTeamSelect2();
    void FocusStart();

    void SetupTeamSelect1();
    void SetupTeamSelect2();

    void RandomizeMatchup();
    void StartSandboxMatch();

    virtual void ProcessWindowingEvent(WindowingEvent *event);
    virtual void ProcessKeyboardEvent(KeyboardEvent *event);

  protected:
    bool SelectSpecificTeam(int slot, const std::string &teamID);

    Gui2Grid *grid;
    Gui2IconSelector *competitionSelect1;
    Gui2IconSelector *competitionSelect2;
    Gui2IconSelector *teamSelect1;
    Gui2IconSelector *teamSelect2;
    Gui2Slider *difficultySlider;
    Gui2Caption *endlessRuleCaption;
    Gui2Button *buttonStart;
    Gui2Button *buttonRandom;

};

class FreeKickPage : public Gui2Page {

  public:
    FreeKickPage(Gui2WindowManager *windowManager, const Gui2PageData &pageData);
    virtual ~FreeKickPage();

    void FocusTeamSelect1();
    void FocusTeamSelect2();
    void FocusStart();

    void SetupTeamSelect1();
    void SetupTeamSelect2();

    void CycleControlMode();
    void CycleSpotPreset();
    void RandomizeMatchup();
    void StartFreeKickMatch();

    virtual void ProcessWindowingEvent(WindowingEvent *event);
    virtual void ProcessKeyboardEvent(KeyboardEvent *event);

  protected:
    bool SelectSpecificTeam(int slot, const std::string &teamID);
    void UpdateOptionLabels();

    Gui2Grid *grid;
    Gui2IconSelector *competitionSelect1;
    Gui2IconSelector *competitionSelect2;
    Gui2IconSelector *teamSelect1;
    Gui2IconSelector *teamSelect2;
    Gui2Button *buttonControlMode;
    Gui2Button *buttonSpotPreset;
    Gui2Button *buttonStart;
    Gui2Button *buttonRandom;

    int controlMode; // 0: P1 Keyboard Takes FK, 1: P1 Defends, 2: AI vs AI, 3: Gamepad Takes FK (when gamepad present)
    int spotPreset;  // 0: Varied (18-29m), 1: Central (19-22m), 2: Wing Curlers (21-25m), 3: Long Range (27-32m)

};

#endif
