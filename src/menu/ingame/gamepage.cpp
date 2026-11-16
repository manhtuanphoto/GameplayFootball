// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "gamepage.hpp"

#include "../pagefactory.hpp"

#include "main.hpp"

#include "replaymenu.hpp"
#include "phasemenu.hpp"
#include "gameover.hpp"

#include "../../onthepitch/match.hpp"
#include "../../hid/keyboard.hpp"
#include "systems/graphics/rendering/r3d_messages.hpp"

using namespace blunted;

std::string GamePage::BuildTestKeysCaptionText() const {
  int aa = clamp(GetConfiguration()->GetInt("context_aa", (int)e_AntiAliasing_FXAA_Quality), 0, 3);
  const char *aaShort = "FXAA+";
  if (aa == 0) aaShort = "OFF";
  else if (aa == 1) aaShort = "Fast";
  else if (aa == 3) aaShort = "Ultra";
  return "[F1-F6] Sun/Cam/Ball/Mode/" + f5ModeLabel + "/IK | [F7] Mental | [F10] AA:" + aaShort + " | [F9] Hide";
}

GamePage::GamePage(Gui2WindowManager *windowManager, const Gui2PageData &pageData) : Gui2Page(windowManager, pageData), match(0), hotkeyBg(0), controlsCaption(0), actionsCaption(0), testKeysCaption(0), showHotkeyBar(true) {

  std::string keySprint = "E";
  std::string keyDribble = "C";
  std::string keySpecial = "Z";
  std::string keyPass = "S";
  std::string keyThrough = "W";
  std::string keyHigh = "A";
  std::string keyShot = "D";
  std::string keySwitch = "Q";

  const std::vector<IHIDevice*> &controllers = GetControllers();
  for (unsigned int c = 0; c < controllers.size(); c++) {
    if (controllers.at(c)->GetDeviceType() == e_HIDeviceType_Keyboard) {
      HIDKeyboard *kb = static_cast<HIDKeyboard*>(controllers.at(c));
      keySprint = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_Sprint));
      keyDribble = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_Dribble));
      keySpecial = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_Special));
      keyPass = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_ShortPass));
      keyThrough = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_LongPass));
      keyHigh = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_HighPass));
      keyShot = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_Shot));
      keySwitch = SDL_GetKeyName(kb->GetFunctionMapping(e_ButtonFunction_Switch));
      break;
    }
  }

  bool isFKMode = (GetMenuTask()->GetSpecialMatchMode() == e_SpecialMatchMode_FreeKick);
  f5ModeLabel = isFKMode ? "NewFK" : "Rst";
  std::string line1 = "[Arrows] Move  [" + keySprint + "] Sprint  [" + keyDribble + "] Slow  [" + keySpecial + "] Special  [" + keySwitch + "] Switch";
  std::string line2 = "[" + keyPass + "] Pass/Press  [" + keyThrough + "] Thru/GK  [" + keyHigh + "] Cross/Slide  [" + keyShot + "] Shot/TeamPress";
  std::string line3 = BuildTestKeysCaptionText();

  const float fontH = 1.62f;
  const float cardTop = 1.4f;
  const float cardH = 6.1f;

  // Create captions first to measure exact rendered widths, then size & place the translucent top-bar card
  controlsCaption = new Gui2Caption(windowManager, "caption_game_controls", 0, cardTop + 0.30f, 60.0f, fontH, line1);
  controlsCaption->SetColor(Vector3(245, 240, 200));
  controlsCaption->SetOutlineColor(Vector3(0, 0, 0));
  controlsCaption->SetTransparency(0.06f);

  actionsCaption = new Gui2Caption(windowManager, "caption_game_actions", 0, cardTop + 2.15f, 60.0f, fontH, line2);
  actionsCaption->SetColor(Vector3(225, 245, 215));
  actionsCaption->SetOutlineColor(Vector3(0, 0, 0));
  actionsCaption->SetTransparency(0.06f);

  testKeysCaption = new Gui2Caption(windowManager, "caption_game_testkeys", 0, cardTop + 4.00f, 64.0f, fontH, line3);
  testKeysCaption->SetColor(Vector3(155, 215, 255));
  testKeysCaption->SetOutlineColor(Vector3(0, 0, 0));
  testKeysCaption->SetTransparency(0.06f);

  float maxTextW = std::max(controlsCaption->GetTextWidthPercent(),
                            std::max(actionsCaption->GetTextWidthPercent(),
                                     testKeysCaption->GetTextWidthPercent()));
  float sbRight = 29.0f;
  float cardW = clamp(maxTextW + 2.2f, 36.0f, 98.4f - sbRight);
  float cardX = 98.2f - cardW;

  hotkeyBg = new Gui2Image(windowManager, "image_game_hotkey_bg", cardX, cardTop, cardW, cardH);
  int px = 0, py = 0, pw = 0, ph = 0;
  windowManager->GetCoordinates(cardX, cardTop, cardW, cardH, px, py, pw, ph);
  hotkeyBg->GetImage2D()->DrawRectangle(0, 0, pw, ph, Vector3(10, 16, 28), 140);
  hotkeyBg->GetImage2D()->DrawRectangle(0, 0, 3, ph, Vector3(95, 200, 255), 215);
  hotkeyBg->GetImage2D()->OnChange();
  this->AddView(hotkeyBg);
  hotkeyBg->Show();

  this->AddView(controlsCaption);
  controlsCaption->SetPosition(cardX + 1.1f, cardTop + 0.30f);
  controlsCaption->Show();

  this->AddView(actionsCaption);
  actionsCaption->SetPosition(cardX + 1.1f, cardTop + 2.15f);
  actionsCaption->Show();

  this->AddView(testKeysCaption);
  testKeysCaption->SetPosition(cardX + 1.1f, cardTop + 4.00f);
  testKeysCaption->Show();

  this->Show();

  this->SetFocus();
}

GamePage::~GamePage() {
  if (Verbose()) printf("disconnecting signals\n");
  matchPhaseConn.disconnect();
  shortReplayConn.disconnect();
  extendedReplayConn.disconnect();
  gameOverConn.disconnect();
}

void GamePage::Process() {

  if (!match) {
    GetGameTask()->matchLifetimeMutex.lock();
    if (GetGameTask()->GetMatch() != 0) {

      match = GetGameTask()->GetMatch();

      if (Verbose()) printf("connecting signals\n");

      matchPhaseConn = match->sig_OnMatchPhaseChange.connect(blunted::Bind(&GamePage::GoMatchPhasePage, this));
      shortReplayConn = match->sig_OnShortReplayMoment.connect(blunted::Bind(&GamePage::GoShortReplayPage, this));
      extendedReplayConn = match->sig_OnExtendedReplayMoment.connect(blunted::Bind(&GamePage::GoExtendedReplayPage, this));
      gameOverConn = match->sig_OnGameOver.connect(blunted::Bind(&GamePage::GoGameOverPage, this));

    }
    GetGameTask()->matchLifetimeMutex.unlock();
  }

}

void GamePage::GoShortReplayPage() {
  // todo
  CreatePage((int)e_PageID_Replay);
}

void GamePage::GoExtendedReplayPage() {

  this->Exit();

  Properties properties;
  ReplayPage *replayPage = static_cast<ReplayPage*>(windowManager->GetPageFactory()->CreatePage((int)e_PageID_Replay, properties, 0));

  // todo: use properties instead?
  int replayHistoryOffset_ms = match->GetReplaySize_ms();
  bool stayInReplay = true;
  replayPage->Autorun(replayHistoryOffset_ms, stayInReplay);

  delete this;
}

void GamePage::GoMatchPhasePage() {

  e_MatchPhase nextPhase = match->GetMatchPhase();

  Properties properties;
  properties.Set("nextphase", (int)nextPhase);
  CreatePage((int)e_PageID_MatchPhase, properties);

}

void GamePage::GoGameOverPage() {
  CreatePage((int)e_PageID_GameOver);
}

void GamePage::OnCreatedMatch() {
}

void GamePage::ProcessWindowingEvent(WindowingEvent *event) {
  if (Verbose()) if (event->IsEscape()) printf("escape!\n");
  event->Ignore();
}

void GamePage::ProcessKeyboardEvent(KeyboardEvent *event) {

  if (event->GetKeyOnce(SDLK_F10)) {
    int curAA = clamp(GetConfiguration()->GetInt("context_aa", (int)e_AntiAliasing_FXAA_Quality), 0, 3);
    int nextAA = (curAA + 1) % 4;
    GetConfiguration()->Set("context_aa", nextAA);
    GetConfiguration()->SaveFile(GetConfigFilename());

    blunted::intrusive_ptr<Renderer3DMessage_SetAntiAliasing> setAA(new Renderer3DMessage_SetAntiAliasing((e_AntiAliasingMode)nextAA));
    GetGraphicsSystem()->GetRenderer3D()->messageQueue.PushMessage(setAA);

    if (testKeysCaption) {
      testKeysCaption->SetCaption(BuildTestKeysCaptionText());
    }
    return;
  }

  if (event->GetKeyOnce(SDLK_F9)) {
    showHotkeyBar = !showHotkeyBar;
    if (showHotkeyBar) {
      if (hotkeyBg) hotkeyBg->Show();
      if (controlsCaption) controlsCaption->Show();
      if (actionsCaption) actionsCaption->Show();
      if (testKeysCaption) testKeysCaption->Show();
    } else {
      if (hotkeyBg) hotkeyBg->Hide();
      if (controlsCaption) controlsCaption->Hide();
      if (actionsCaption) actionsCaption->Hide();
      if (testKeysCaption) testKeysCaption->Hide();
    }
    return;
  }

  if (event->GetKeyOnce(SDLK_ESCAPE)) {

    // check which team the keyboard belongs to
    int controllerID = 0;
    const std::vector<IHIDevice*> &controllers = GetControllers();
    for (unsigned int c = 0; c < controllers.size(); c++) {
      if (controllers.at(c)->GetDeviceType() == e_HIDeviceType_Keyboard) {
        controllerID = c;
        break;
      }
    }

    if (Verbose()) printf("controller index %i (keyboard) pressed start\n", controllerID);

    int teamID = 0;
    const std::vector<SideSelection> sides = GetMenuTask()->GetControllerSetup();
    for (unsigned int s = 0; s < sides.size(); s++) {
      if (sides.at(s).controllerID == (signed int)controllerID) {
        teamID = (sides.at(s).side > 0) ? 1 : 0;
        break;
      }
    }

    if (Verbose()) printf ("team belonging to this controller seems to be %i\n", teamID);

    Properties properties;
    properties.Set("teamID", teamID);
    CreatePage((int)e_PageID_Ingame, properties);
    return;
  }

  event->Ignore();
}

void GamePage::ProcessJoystickEvent(JoystickEvent *event) {

  // oof, the problem is that we are using a GUI joystick event to find out what ingame HID controller pressed <start>.
  // I think we need a new system in which the game doesn't use its own input system, but uses the GUI one.
  // for now, this hax will have to do

  const std::vector<IHIDevice*> &controllers = GetControllers();
  for (unsigned int c = 0; c < controllers.size(); c++) {

    if (controllers.at(c)->GetDeviceType() == e_HIDeviceType_Gamepad) {
      HIDGamepad *gamepad = static_cast<HIDGamepad*>(controllers.at(c));
      int joyID = gamepad->GetGamepadID(); // these should be the same IDs the GUI system uses as joyID

      if (event->GetButton(joyID, gamepad->GetControllerMapping(gamepad->GetFunctionMapping(e_ButtonFunction_Start)))) {

        if (Verbose()) printf("controller index %i, gamepad/joy ID %i pressed start\n", c, joyID);

        // check which team this controller belongs to
        int teamID = 0;
        const std::vector<SideSelection> sides = GetMenuTask()->GetControllerSetup();
        for (unsigned int s = 0; s < sides.size(); s++) {
          if (sides.at(s).controllerID == (signed int)c) {
            teamID = (sides.at(s).side > 0) ? 1 : 0;
            break;
          }
        }

        if (Verbose()) printf ("team belonging to this controller seems to be %i\n", teamID);

        Properties properties;
        properties.Set("teamID", teamID);
        CreatePage((int)e_PageID_Ingame, properties);
        return;
      }

    }

  }

  event->Ignore();
}
