// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "pagefactory.hpp"

#include "mainmenu.hpp"
#include "credits.hpp"
#include "settings.hpp"
#include "controllerselect.hpp"
#include "startmatch/teamselect.hpp"
#include "startmatch/sandbox.hpp"
#include "startmatch/matchoptions.hpp"
#include "startmatch/loadingmatch.hpp"
#include "ingame/gamepage.hpp"
#include "ingame/phasemenu.hpp"
#include "ingame/gameover.hpp"
#include "ingame/ingame.hpp"
#include "ingame/replaymenu.hpp"
#include "gameplan.hpp"
#include "cameramenu.hpp"
#include "visualoptions.hpp"
#include "league/league.hpp"
#include "league/league_calendar.hpp"
#include "league/league_forward.hpp"
#include "league/league_inbox.hpp"
#include "league/league_management.hpp"
#include "league/league_standings.hpp"
#include "league/league_system.hpp"
#include "league/league_team.hpp"

#include "../main.hpp"

Gui2Page *PageFactory::CreatePage(const Gui2PageData &pageData) {

  Gui2Page *page = 0;

  if (GetGameTask()->GetMenuScene()) {
    GetGameTask()->GetMenuScene()->RandomizeTargetLocation();
  }

  switch (pageData.pageID) {

    case e_PageID_MainMenu:
      page = new MainMenuPage(windowManager, pageData);
      break;

    case e_PageID_Game:
      page = new GamePage(windowManager, pageData);
      break;

    case e_PageID_Intro:
      page = new IntroPage(windowManager, pageData);
      break;

    case e_PageID_Outro:
      page = new OutroPage(windowManager, pageData);
      break;

    case e_PageID_Credits:
      page = new CreditsPage(windowManager, pageData);
      break;

    case e_PageID_Settings:
      page = new SettingsPage(windowManager, pageData);
      break;

    case e_PageID_ControllerSelect:
      page = new ControllerSelectPage(windowManager, pageData);
      break;

    case e_PageID_TeamSelect:
      page = new TeamSelectPage(windowManager, pageData);
      break;

    case e_PageID_Sandbox:
      page = new SandboxPage(windowManager, pageData);
      break;

    case e_PageID_FreeKick:
      page = new FreeKickPage(windowManager, pageData);
      break;

    case e_PageID_MatchOptions:
      page = new MatchOptionsPage(windowManager, pageData);
      break;

    case e_PageID_LoadingMatch:
      page = new LoadingMatchPage(windowManager, pageData);
      break;

    case e_PageID_MatchPhase:
      page = new MatchPhasePage(windowManager, pageData);
      break;

    case e_PageID_GameOver:
      page = new GameOverPage(windowManager, pageData);
      break;

    case e_PageID_Ingame:
      page = new IngamePage(windowManager, pageData);
      break;

    case e_PageID_PreQuit:
      page = new PreQuitPage(windowManager, pageData);
      break;

    case e_PageID_GamePlan:
      page = new GamePlanPage(windowManager, pageData);
      break;

    case e_PageID_VisualOptions:
      page = new VisualOptionsPage(windowManager, pageData);
      break;

    case e_PageID_Replay:
      page = new ReplayPage(windowManager, pageData);
      break;

    case e_PageID_Camera:
      page = new CameraPage(windowManager, pageData);
      break;

    case e_PageID_Gameplay:
      page = new GameplayPage(windowManager, pageData);
      break;

    case e_PageID_Controller:
      page = new ControllerPage(windowManager, pageData);
      break;

    case e_PageID_Keyboard:
      page = new KeyboardPage(windowManager, pageData);
      break;

    case e_PageID_Gamepads:
      page = new GamepadsPage(windowManager, pageData);
      break;

    case e_PageID_GamepadSetup:
      page = new GamepadSetupPage(windowManager, pageData);
      break;

    case e_PageID_GamepadCalibration:
      page = new GamepadCalibrationPage(windowManager, pageData);
      break;

    case e_PageID_GamepadMapping:
      page = new GamepadMappingPage(windowManager, pageData);
      break;

    case e_PageID_GamepadFunction:
      page = new GamepadFunctionPage(windowManager, pageData);
      break;

    case e_PageID_Graphics:
      page = new GraphicsPage(windowManager, pageData);
      break;

    case e_PageID_Audio:
      page = new AudioPage(windowManager, pageData);
      break;


    // league mode

    case e_PageID_League_Start:
      page = new LeagueStartPage(windowManager, pageData);
      break;

    case e_PageID_League_Start_Load:
      page = new LeagueStartLoadPage(windowManager, pageData);
      break;

    case e_PageID_League_Start_New:
      page = new LeagueStartNewPage(windowManager, pageData);
      break;

    case e_PageID_League:
      page = new LeaguePage(windowManager, pageData);
      break;

    case e_PageID_League_Forward:
      page = new LeagueForwardPage(windowManager, pageData);
      break;

    case e_PageID_League_Inbox:
      page = new LeagueInboxPage(windowManager, pageData);
      break;

    case e_PageID_League_Team:
      page = new LeagueTeamPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_Formation:
      page = new LeagueTeamFormationPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_PlayerSelection:
      page = new LeagueTeamPlayerSelectionPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_Tactics:
      page = new LeagueTeamTacticsPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_PlayerOverview:
      page = new LeagueTeamPlayerOverviewPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_PlayerDevelopment:
      page = new LeagueTeamPlayerDevelopmentPage(windowManager, pageData);
      break;

    case e_PageID_League_Team_Setup:
      page = new LeagueTeamSetupPage(windowManager, pageData);
      break;

    case e_PageID_League_Calendar:
      page = new LeagueCalendarPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings:
      page = new LeagueStandingsPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_League:
      page = new LeagueStandingsLeaguePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_League_Table:
      page = new LeagueStandingsLeagueTablePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_League_Stats:
      page = new LeagueStandingsLeagueStatsPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_NCup:
      page = new LeagueStandingsNCupPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_NCup_Tree:
      page = new LeagueStandingsNCupTreePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_NCup_Stats:
      page = new LeagueStandingsNCupStatsPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup1:
      page = new LeagueStandingsICup1Page(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup1_GroupTable:
      page = new LeagueStandingsICup1GroupTablePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup1_Tree:
      page = new LeagueStandingsICup1TreePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup1_Stats:
      page = new LeagueStandingsICup1StatsPage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup2:
      page = new LeagueStandingsICup2Page(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup2_GroupTable:
      page = new LeagueStandingsICup2GroupTablePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup2_Tree:
      page = new LeagueStandingsICup2TreePage(windowManager, pageData);
      break;

    case e_PageID_League_Standings_ICup2_Stats:
      page = new LeagueStandingsICup2StatsPage(windowManager, pageData);
      break;

    case e_PageID_League_Management:
      page = new LeagueManagementPage(windowManager, pageData);
      break;

    case e_PageID_League_Management_Contracts:
      page = new LeagueManagementContractsPage(windowManager, pageData);
      break;

    case e_PageID_League_Management_Transfers:
      page = new LeagueManagementTransfersPage(windowManager, pageData);
      break;

    case e_PageID_League_System:
      page = new LeagueSystemPage(windowManager, pageData);
      break;

    case e_PageID_League_System_Save:
      page = new LeagueSystemSavePage(windowManager, pageData);
      break;

    case e_PageID_League_System_Settings:
      page = new LeagueSystemSettingsPage(windowManager, pageData);
      break;


    default:
      page = 0;
      break;

  }

  if (page != 0) {
    if (pageData.pageID != e_PageID_Game &&
        pageData.pageID != e_PageID_Intro &&
        pageData.pageID != e_PageID_Outro &&
        pageData.pageID != e_PageID_LoadingMatch) {
      Gui2Image *menuHintBg = new Gui2Image(windowManager, "image_menu_hotkey_bg_" + int_to_str(pageData.pageID), 12.0f, 95.6f, 76.0f, 3.4f);
      menuHintBg->LoadImage("media/menu/backgrounds/black.png");
      page->AddView(menuHintBg);
      menuHintBg->Show();

      std::string menuHintText = "[Arrows] Navigate   |   [Enter / S] Select   |   [Esc / W] Back   |   [Left / Right] Adjust Option";
      Gui2Caption *menuHintCap = new Gui2Caption(windowManager, "caption_menu_hotkey_" + int_to_str(pageData.pageID), 0, 96.1f, 76.0f, 2.1f, menuHintText);
      menuHintCap->SetColor(Vector3(215, 230, 250));
      menuHintCap->SetOutlineColor(Vector3(0, 0, 0));
      menuHintCap->SetTransparency(0.10f);
      page->AddView(menuHintCap);
      float mw = menuHintCap->GetTextWidthPercent();
      menuHintCap->SetPosition(50.0f - mw * 0.5f, 96.1f);
      menuHintCap->Show();
    }

    windowManager->GetPagePath()->Push(pageData);
    windowManager->GetRoot()->AddView(page);
  }

  return page;

}
