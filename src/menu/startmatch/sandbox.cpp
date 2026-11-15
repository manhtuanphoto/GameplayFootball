// Sandbox quick AI vs AI match setup page

#include "sandbox.hpp"

#include "../../main.hpp"
#include "utils/database.hpp"
#include "../pagefactory.hpp"

using namespace blunted;

// Defined in teamselect.cpp
void AddCompetitions(Gui2IconSelector *selector);
void AddTeams(Gui2IconSelector *selector, const std::string &competition_id);

SandboxPage::SandboxPage(Gui2WindowManager *windowManager, const Gui2PageData &pageData) : Gui2Page(windowManager, pageData) {

  Gui2Image *bgLeft = new Gui2Image(windowManager, "sandbox_image_bg1", 17, 17, 32, 68);
  this->AddView(bgLeft);
  bgLeft->LoadImage("media/menu/backgrounds/black.png");
  bgLeft->Show();

  Gui2Image *bgRight = new Gui2Image(windowManager, "sandbox_image_bg2", 51, 17, 32, 68);
  this->AddView(bgRight);
  bgRight->LoadImage("media/menu/backgrounds/black.png");
  bgRight->Show();

  Gui2Caption *title = new Gui2Caption(windowManager, "sandbox_caption_title", 17, 9, 66, 3.5f, "Sandbox Mode  -  Continuous Endless AI vs AI");
  this->AddView(title);
  title->SetPosition(50.0f - title->GetTextWidthPercent() * 0.5f, 9.0f);
  title->Show();

  Gui2Caption *cpu1Caption = new Gui2Caption(windowManager, "sandbox_caption_cpu1", 19, 13.5f, 28, 3, "CPU 1 (Home)");
  this->AddView(cpu1Caption);
  cpu1Caption->Show();

  Gui2Caption *cpu2Caption = new Gui2Caption(windowManager, "sandbox_caption_cpu2", 53, 13.5f, 28, 3, "CPU 2 (Away)");
  this->AddView(cpu2Caption);
  cpu2Caption->Show();

  Gui2Caption *hintCaption = new Gui2Caption(windowManager, "sandbox_caption_hint", 17, 87, 66, 2.5f, "Endless Mode: 2 teams compete non-stop without time limit  |  [R] Random Matchup  |  [F4] Take Control");
  this->AddView(hintCaption);
  hintCaption->SetColor(Vector3(190, 205, 225));
  hintCaption->SetTransparency(0.35f);
  hintCaption->SetPosition(50.0f - hintCaption->GetTextWidthPercent() * 0.5f, 87.0f);
  hintCaption->Show();

  grid = new Gui2Grid(windowManager, "sandbox_grid", 18, 18, 64, 66);

  competitionSelect1 = new Gui2IconSelector(windowManager, "sandbox_competition1", 0, 0, 30, 17, "League (Home)");
  competitionSelect2 = new Gui2IconSelector(windowManager, "sandbox_competition2", 0, 0, 30, 17, "League (Away)");
  teamSelect1 = new Gui2IconSelector(windowManager, "sandbox_team1", 0, 0, 30, 17, "Team (Home)");
  teamSelect2 = new Gui2IconSelector(windowManager, "sandbox_team2", 0, 0, 30, 17, "Team (Away)");

  difficultySlider = new Gui2Slider(windowManager, "sandbox_slider_difficulty", 0, 0, 30, 6, "AI intensity / tempo");
  endlessRuleCaption = new Gui2Caption(windowManager, "sandbox_caption_endless", 0, 0, 30, 3.0f, "Time: Endless Continuous Play");
  endlessRuleCaption->SetColor(Vector3(160, 240, 190));

  buttonStart = new Gui2Button(windowManager, "sandbox_button_start", 0, 0, 30, 3.5f, "Start Endless Sandbox");
  buttonRandom = new Gui2Button(windowManager, "sandbox_button_random", 0, 0, 30, 3.5f, "Random Matchup");

  float difficulty = GetConfiguration()->GetReal("match_difficulty", 0.8f);
  difficultySlider->SetValue(difficulty);

  competitionSelect1->sig_OnChange.connect(blunted::Bind(&SandboxPage::SetupTeamSelect1, this));
  competitionSelect2->sig_OnChange.connect(blunted::Bind(&SandboxPage::SetupTeamSelect2, this));

  competitionSelect1->sig_OnClick.connect(blunted::Bind(&SandboxPage::FocusTeamSelect1, this));
  teamSelect1->sig_OnClick.connect(blunted::Bind(&SandboxPage::FocusTeamSelect2, this));
  competitionSelect2->sig_OnClick.connect(blunted::Bind(&SandboxPage::FocusTeamSelect2, this));
  teamSelect2->sig_OnClick.connect(blunted::Bind(&SandboxPage::FocusStart, this));

  buttonStart->sig_OnClick.connect(blunted::Bind(&SandboxPage::StartSandboxMatch, this));
  buttonRandom->sig_OnClick.connect(blunted::Bind(&SandboxPage::RandomizeMatchup, this));

  AddCompetitions(competitionSelect1);
  AddCompetitions(competitionSelect2);
  AddTeams(teamSelect1, competitionSelect1->GetSelectedEntryID());
  AddTeams(teamSelect2, competitionSelect2->GetSelectedEntryID());

  // Initialize with saved distinct teams, or classic rivalry / random pair
  int prevTeam1 = GetMenuTask()->GetTeamID(0);
  int prevTeam2 = GetMenuTask()->GetTeamID(1);
  bool loadedPrev = false;
  if (prevTeam1 > 0 && prevTeam2 > 0 && prevTeam1 != prevTeam2) {
    bool ok1 = SelectSpecificTeam(0, int_to_str(prevTeam1));
    bool ok2 = SelectSpecificTeam(1, int_to_str(prevTeam2));
    loadedPrev = (ok1 && ok2);
  }
  if (!loadedPrev) {
    // Try Barcelona (3) vs R. Madrid (8), otherwise randomize distinct teams
    bool ok1 = SelectSpecificTeam(0, "3");
    bool ok2 = SelectSpecificTeam(1, "8");
    if (!ok1 || !ok2 || teamSelect1->GetSelectedEntryID() == teamSelect2->GetSelectedEntryID()) {
      RandomizeMatchup();
    }
  }

  grid->AddView(competitionSelect1, 0, 0);
  grid->AddView(competitionSelect2, 0, 1);
  grid->AddView(teamSelect1, 1, 0);
  grid->AddView(teamSelect2, 1, 1);
  grid->AddView(difficultySlider, 2, 0);
  grid->AddView(endlessRuleCaption, 2, 1);
  grid->AddView(buttonStart, 3, 0);
  grid->AddView(buttonRandom, 3, 1);
  grid->UpdateLayout(1.0f, 1.0f, 0.6f, 0.6f);

  this->AddView(grid);
  grid->Show();

  GetMenuTask()->SetActiveJoystickID(0);
  GetMenuTask()->EnableKeyboard();

  buttonStart->SetFocus();

  this->Show();
}

SandboxPage::~SandboxPage() {
  GetMenuTask()->SetActiveJoystickID(0);
  GetMenuTask()->EnableKeyboard();
}

bool SandboxPage::SelectSpecificTeam(int slot, const std::string &teamID) {
  DatabaseResult *result = GetDB()->Query("select league_id from teams where id = " + teamID + " limit 1");
  bool success = false;
  if (result->data.size() > 0) {
    std::string leagueID = result->data.at(0).at(0);
    Gui2IconSelector *compSel = (slot == 0) ? competitionSelect1 : competitionSelect2;
    Gui2IconSelector *teamSel = (slot == 0) ? teamSelect1 : teamSelect2;
    if (compSel->SelectEntryByID(leagueID, false)) {
      teamSel->ClearEntries();
      AddTeams(teamSel, compSel->GetSelectedEntryID());
      success = teamSel->SelectEntryByID(teamID, false);
    }
  }
  delete result;
  return success;
}

void SandboxPage::FocusTeamSelect1() {
  teamSelect1->SetFocus();
}

void SandboxPage::FocusTeamSelect2() {
  teamSelect2->SetFocus();
}

void SandboxPage::FocusStart() {
  buttonStart->SetFocus();
}

void SandboxPage::SetupTeamSelect1() {
  teamSelect1->ClearEntries();
  AddTeams(teamSelect1, competitionSelect1->GetSelectedEntryID());
}

void SandboxPage::SetupTeamSelect2() {
  teamSelect2->ClearEntries();
  AddTeams(teamSelect2, competitionSelect2->GetSelectedEntryID());
}

void SandboxPage::RandomizeMatchup() {
  DatabaseResult *result = GetDB()->Query("select id from teams");
  int totalTeams = (int)result->data.size();
  if (totalTeams >= 2) {
    int idx1 = clamp(int(floor(random(0.0f, (float)totalTeams - 0.001f))), 0, totalTeams - 1);
    int idx2 = clamp(int(floor(random(0.0f, (float)(totalTeams - 1) - 0.001f))), 0, totalTeams - 2);
    if (idx2 >= idx1) idx2++;

    std::string id1 = result->data.at(idx1).at(0);
    std::string id2 = result->data.at(idx2).at(0);
    SelectSpecificTeam(0, id1);
    SelectSpecificTeam(1, id2);
  }
  delete result;
}

void SandboxPage::StartSandboxMatch() {
  // Set all controllers to neutral (side = 0) so both teams run pure AI (0 human gamers)
  std::vector<SideSelection> sides;
  const std::vector<IHIDevice*> &controllers = GetControllers();
  for (unsigned int i = 0; i < controllers.size(); i++) {
    SideSelection side;
    side.controllerID = (int)i;
    side.controllerImage = 0;
    side.side = 0;
    sides.push_back(side);
  }
  GetMenuTask()->SetControllerSetup(sides);
  GetMenuTask()->SetTeamIDs(teamSelect1->GetSelectedEntryID(), teamSelect2->GetSelectedEntryID());
  GetMenuTask()->SetSpecialMatchMode(e_SpecialMatchMode_SandboxEndless);

  GetConfiguration()->Set("match_difficulty", difficultySlider->GetValue());
  GetConfiguration()->SaveFile(GetConfigFilename());

  this->Exit();

  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);

  delete this;
}

void SandboxPage::ProcessWindowingEvent(WindowingEvent *event) {
  if (event->IsEscape()) {
    GoBack();
    return;
  }
  Gui2Page::ProcessWindowingEvent(event);
}

void SandboxPage::ProcessKeyboardEvent(KeyboardEvent *event) {
  if (event->GetKeyOnce(SDLK_r)) {
    RandomizeMatchup();
    return;
  }
  Gui2Page::ProcessKeyboardEvent(event);
}


// ============================================================================
// FreeKickPage - Dedicated Direct & Indirect Free Kick Practice / Challenge
// ============================================================================

FreeKickPage::FreeKickPage(Gui2WindowManager *windowManager, const Gui2PageData &pageData)
    : Gui2Page(windowManager, pageData),
      controlMode(0),
      spotPreset(GetMenuTask()->GetFreeKickPreset()) {

  Gui2Image *bgLeft = new Gui2Image(windowManager, "fk_image_bg1", 17, 17, 32, 68);
  this->AddView(bgLeft);
  bgLeft->LoadImage("media/menu/backgrounds/black.png");
  bgLeft->Show();

  Gui2Image *bgRight = new Gui2Image(windowManager, "fk_image_bg2", 51, 17, 32, 68);
  this->AddView(bgRight);
  bgRight->LoadImage("media/menu/backgrounds/black.png");
  bgRight->Show();

  Gui2Caption *title = new Gui2Caption(windowManager, "fk_caption_title", 17, 9, 66, 3.5f, "Free Kick Mode  -  Set-Piece Practice & Challenge");
  this->AddView(title);
  title->SetPosition(50.0f - title->GetTextWidthPercent() * 0.5f, 9.0f);
  title->Show();

  Gui2Caption *attCaption = new Gui2Caption(windowManager, "fk_caption_att", 19, 13.5f, 28, 3, "Free Kick Taker (Attack)");
  this->AddView(attCaption);
  attCaption->Show();

  Gui2Caption *defCaption = new Gui2Caption(windowManager, "fk_caption_def", 53, 13.5f, 28, 3, "Wall & Goalkeeper (Defend)");
  this->AddView(defCaption);
  defCaption->Show();

  Gui2Caption *hintCaption = new Gui2Caption(windowManager, "fk_caption_hint", 17, 87, 66, 2.4f, "[D] Shoot over Wall  |  [A] Lob / Cross  |  [S] Layoff Pass  |  [F5] Instant Next Free Kick Spot");
  this->AddView(hintCaption);
  hintCaption->SetColor(Vector3(190, 225, 250));
  hintCaption->SetTransparency(0.25f);
  hintCaption->SetPosition(50.0f - hintCaption->GetTextWidthPercent() * 0.5f, 87.0f);
  hintCaption->Show();

  grid = new Gui2Grid(windowManager, "fk_grid", 18, 18, 64, 66);

  competitionSelect1 = new Gui2IconSelector(windowManager, "fk_competition1", 0, 0, 30, 17, "League (Attack)");
  competitionSelect2 = new Gui2IconSelector(windowManager, "fk_competition2", 0, 0, 30, 17, "League (Defend)");
  teamSelect1 = new Gui2IconSelector(windowManager, "fk_team1", 0, 0, 30, 17, "Team (Attack)");
  teamSelect2 = new Gui2IconSelector(windowManager, "fk_team2", 0, 0, 30, 17, "Team (Defend)");

  buttonControlMode = new Gui2Button(windowManager, "fk_button_control", 0, 0, 30, 3.5f, "");
  buttonSpotPreset = new Gui2Button(windowManager, "fk_button_spot", 0, 0, 30, 3.5f, "");
  buttonStart = new Gui2Button(windowManager, "fk_button_start", 0, 0, 30, 3.5f, "Start Free Kick");
  buttonRandom = new Gui2Button(windowManager, "fk_button_random", 0, 0, 30, 3.5f, "Random Matchup");

  UpdateOptionLabels();

  competitionSelect1->sig_OnChange.connect(blunted::Bind(&FreeKickPage::SetupTeamSelect1, this));
  competitionSelect2->sig_OnChange.connect(blunted::Bind(&FreeKickPage::SetupTeamSelect2, this));

  competitionSelect1->sig_OnClick.connect(blunted::Bind(&FreeKickPage::FocusTeamSelect1, this));
  teamSelect1->sig_OnClick.connect(blunted::Bind(&FreeKickPage::FocusTeamSelect2, this));
  competitionSelect2->sig_OnClick.connect(blunted::Bind(&FreeKickPage::FocusTeamSelect2, this));
  teamSelect2->sig_OnClick.connect(blunted::Bind(&FreeKickPage::FocusStart, this));

  buttonControlMode->sig_OnClick.connect(blunted::Bind(&FreeKickPage::CycleControlMode, this));
  buttonSpotPreset->sig_OnClick.connect(blunted::Bind(&FreeKickPage::CycleSpotPreset, this));
  buttonStart->sig_OnClick.connect(blunted::Bind(&FreeKickPage::StartFreeKickMatch, this));
  buttonRandom->sig_OnClick.connect(blunted::Bind(&FreeKickPage::RandomizeMatchup, this));

  AddCompetitions(competitionSelect1);
  AddCompetitions(competitionSelect2);
  AddTeams(teamSelect1, competitionSelect1->GetSelectedEntryID());
  AddTeams(teamSelect2, competitionSelect2->GetSelectedEntryID());

  int prevTeam1 = GetMenuTask()->GetTeamID(0);
  int prevTeam2 = GetMenuTask()->GetTeamID(1);
  bool loadedPrev = false;
  if (prevTeam1 > 0 && prevTeam2 > 0 && prevTeam1 != prevTeam2) {
    bool ok1 = SelectSpecificTeam(0, int_to_str(prevTeam1));
    bool ok2 = SelectSpecificTeam(1, int_to_str(prevTeam2));
    loadedPrev = (ok1 && ok2);
  }
  if (!loadedPrev) {
    bool ok1 = SelectSpecificTeam(0, "3");
    bool ok2 = SelectSpecificTeam(1, "8");
    if (!ok1 || !ok2 || teamSelect1->GetSelectedEntryID() == teamSelect2->GetSelectedEntryID()) {
      RandomizeMatchup();
    }
  }

  grid->AddView(competitionSelect1, 0, 0);
  grid->AddView(competitionSelect2, 0, 1);
  grid->AddView(teamSelect1, 1, 0);
  grid->AddView(teamSelect2, 1, 1);
  grid->AddView(buttonControlMode, 2, 0);
  grid->AddView(buttonSpotPreset, 2, 1);
  grid->AddView(buttonStart, 3, 0);
  grid->AddView(buttonRandom, 3, 1);
  grid->UpdateLayout(1.0f, 1.0f, 0.6f, 0.6f);

  this->AddView(grid);
  grid->Show();

  GetMenuTask()->SetActiveJoystickID(0);
  GetMenuTask()->EnableKeyboard();

  buttonStart->SetFocus();

  this->Show();
}

FreeKickPage::~FreeKickPage() {
  GetMenuTask()->SetActiveJoystickID(0);
  GetMenuTask()->EnableKeyboard();
}

void FreeKickPage::UpdateOptionLabels() {
  bool hasGamepad = (GetControllers().size() > 1);
  int maxControlModes = hasGamepad ? 4 : 3;
  if (controlMode >= maxControlModes) controlMode = 0;

  if (controlMode == 0) {
    buttonControlMode->SetCaption("Role: P1 Takes FK (Keyb)");
  } else if (controlMode == 1) {
    buttonControlMode->SetCaption("Role: P1 Defends Wall/GK");
  } else if (controlMode == 2) {
    buttonControlMode->SetCaption("Role: AI vs AI Free Kicks");
  } else {
    buttonControlMode->SetCaption("Role: P1 Takes FK (Pad)");
  }

  spotPreset = ((spotPreset % 4) + 4) % 4;
  if (spotPreset == 0) {
    buttonSpotPreset->SetCaption("Spot: Varied (18m - 29m)");
  } else if (spotPreset == 1) {
    buttonSpotPreset->SetCaption("Spot: Central (19m - 22m)");
  } else if (spotPreset == 2) {
    buttonSpotPreset->SetCaption("Spot: Wing Curl (21m - 25m)");
  } else {
    buttonSpotPreset->SetCaption("Spot: Long Range (27m - 32m)");
  }
}

void FreeKickPage::CycleControlMode() {
  bool hasGamepad = (GetControllers().size() > 1);
  int maxControlModes = hasGamepad ? 4 : 3;
  controlMode = (controlMode + 1) % maxControlModes;
  UpdateOptionLabels();
}

void FreeKickPage::CycleSpotPreset() {
  spotPreset = (spotPreset + 1) % 4;
  UpdateOptionLabels();
}

bool FreeKickPage::SelectSpecificTeam(int slot, const std::string &teamID) {
  DatabaseResult *result = GetDB()->Query("select league_id from teams where id = " + teamID + " limit 1");
  bool success = false;
  if (result->data.size() > 0) {
    std::string leagueID = result->data.at(0).at(0);
    Gui2IconSelector *compSel = (slot == 0) ? competitionSelect1 : competitionSelect2;
    Gui2IconSelector *teamSel = (slot == 0) ? teamSelect1 : teamSelect2;
    if (compSel->SelectEntryByID(leagueID, false)) {
      teamSel->ClearEntries();
      AddTeams(teamSel, compSel->GetSelectedEntryID());
      success = teamSel->SelectEntryByID(teamID, false);
    }
  }
  delete result;
  return success;
}

void FreeKickPage::FocusTeamSelect1() {
  teamSelect1->SetFocus();
}

void FreeKickPage::FocusTeamSelect2() {
  teamSelect2->SetFocus();
}

void FreeKickPage::FocusStart() {
  buttonStart->SetFocus();
}

void FreeKickPage::SetupTeamSelect1() {
  teamSelect1->ClearEntries();
  AddTeams(teamSelect1, competitionSelect1->GetSelectedEntryID());
}

void FreeKickPage::SetupTeamSelect2() {
  teamSelect2->ClearEntries();
  AddTeams(teamSelect2, competitionSelect2->GetSelectedEntryID());
}

void FreeKickPage::RandomizeMatchup() {
  DatabaseResult *result = GetDB()->Query("select id from teams");
  int totalTeams = (int)result->data.size();
  if (totalTeams >= 2) {
    int idx1 = clamp(int(floor(random(0.0f, (float)totalTeams - 0.001f))), 0, totalTeams - 1);
    int idx2 = clamp(int(floor(random(0.0f, (float)(totalTeams - 1) - 0.001f))), 0, totalTeams - 2);
    if (idx2 >= idx1) idx2++;

    std::string id1 = result->data.at(idx1).at(0);
    std::string id2 = result->data.at(idx2).at(0);
    SelectSpecificTeam(0, id1);
    SelectSpecificTeam(1, id2);
  }
  delete result;
}

void FreeKickPage::StartFreeKickMatch() {
  std::vector<SideSelection> sides;
  const std::vector<IHIDevice*> &controllers = GetControllers();
  for (unsigned int i = 0; i < controllers.size(); i++) {
    SideSelection side;
    side.controllerID = (int)i;
    side.controllerImage = 0;
    side.side = 0;
    if (controlMode == 0 && i == 0) {
      side.side = -1; // Keyboard controls Team 0 (Attacking Free Kick Taker)
    } else if (controlMode == 1 && i == 0) {
      side.side = 1;  // Keyboard controls Team 1 (Defending Wall / Goalkeeper)
    } else if (controlMode == 3 && i == 1) {
      side.side = -1; // Gamepad 1 controls Team 0 (Attacking Free Kick Taker)
    }
    sides.push_back(side);
  }

  GetMenuTask()->SetControllerSetup(sides);
  GetMenuTask()->SetTeamIDs(teamSelect1->GetSelectedEntryID(), teamSelect2->GetSelectedEntryID());
  GetMenuTask()->SetSpecialMatchMode(e_SpecialMatchMode_FreeKick);
  GetMenuTask()->SetFreeKickPreset(spotPreset);

  this->Exit();

  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);

  delete this;
}

void FreeKickPage::ProcessWindowingEvent(WindowingEvent *event) {
  if (event->IsEscape()) {
    GoBack();
    return;
  }
  Gui2Page::ProcessWindowingEvent(event);
}

void FreeKickPage::ProcessKeyboardEvent(KeyboardEvent *event) {
  if (event->GetKeyOnce(SDLK_r)) {
    RandomizeMatchup();
    return;
  }
  Gui2Page::ProcessKeyboardEvent(event);
}
