// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "cameramenu.hpp"

#include "../main.hpp"

using namespace blunted;

CameraPage::CameraPage(Gui2WindowManager *windowManager, const Gui2PageData &pageData) : Gui2Page(windowManager, pageData) {

  Gui2Caption *title = new Gui2Caption(windowManager, "caption_camera_title", 20, 12, 60, 3, "Broadcast camera setup");
  this->AddView(title);
  title->Show();

  Gui2Grid *presetGrid = new Gui2Grid(windowManager, "campresetgrid", 18, 18, 64, 14);
  Gui2Button *btnBroadcast = new Gui2Button(windowManager, "btn_cam_broadcast", 0, 0, 30, 3, "Preset: Broadcast TV");
  Gui2Button *btnTactical  = new Gui2Button(windowManager, "btn_cam_tactical",  0, 0, 30, 3, "Preset: Tactical Wide");
  Gui2Button *btnTele      = new Gui2Button(windowManager, "btn_cam_tele",      0, 0, 30, 3, "Preset: Tele Classic");
  Gui2Button *btnAction    = new Gui2Button(windowManager, "btn_cam_action",    0, 0, 30, 3, "Preset: Action Close");

  btnBroadcast->sig_OnClick.connect(blunted::Bind(&CameraPage::ApplyPreset, this, 0.55f, 0.42f, 0.38f, 0.22f));
  btnTactical->sig_OnClick.connect(blunted::Bind(&CameraPage::ApplyPreset, this, 0.85f, 0.72f, 0.45f, 0.08f));
  btnTele->sig_OnClick.connect(blunted::Bind(&CameraPage::ApplyPreset, this, 0.62f, 0.48f, 0.16f, 0.0f));
  btnAction->sig_OnClick.connect(blunted::Bind(&CameraPage::ApplyPreset, this, 0.28f, 0.22f, 0.52f, 0.40f));

  presetGrid->AddView(btnBroadcast, 0, 0);
  presetGrid->AddView(btnTactical,  0, 1);
  presetGrid->AddView(btnTele,      1, 0);
  presetGrid->AddView(btnAction,    1, 1);
  presetGrid->UpdateLayout(0.5);
  this->AddView(presetGrid);
  presetGrid->Show();

  sliderZoom = new Gui2Slider(windowManager, "camzoomslider", 0, 0, 36, 6, "zoom");
  sliderHeight = new Gui2Slider(windowManager, "camheightslider", 0, 0, 36, 6, "height");
  sliderFOV = new Gui2Slider(windowManager, "camfovslider", 0, 0, 36, 6, "fov (perspective)");
  sliderAngleFactor = new Gui2Slider(windowManager, "camangleslider", 0, 0, 36, 6, "horizontal angle");
  sliderZoom->AddHelperValue(Vector3(80, 80, 250), "default", _default_CameraZoom);
  sliderHeight->AddHelperValue(Vector3(80, 80, 250), "default", _default_CameraHeight);
  sliderFOV->AddHelperValue(Vector3(80, 80, 250), "default", _default_CameraFOV);
  sliderAngleFactor->AddHelperValue(Vector3(80, 80, 250), "default", _default_CameraAngleFactor);

  Gui2Grid *grid = new Gui2Grid(windowManager, "camgrid", 30, 34, 44, 48);

  grid->AddView(sliderZoom, 0, 0);
  grid->AddView(sliderHeight, 1, 0);
  grid->AddView(sliderFOV, 2, 0);
  grid->AddView(sliderAngleFactor, 3, 0);

  grid->UpdateLayout(0.5);

  sliderZoom->sig_OnChange.connect(blunted::Bind(&CameraPage::UpdateCamera, this));
  sliderHeight->sig_OnChange.connect(blunted::Bind(&CameraPage::UpdateCamera, this));
  sliderFOV->sig_OnChange.connect(blunted::Bind(&CameraPage::UpdateCamera, this));
  sliderAngleFactor->sig_OnChange.connect(blunted::Bind(&CameraPage::UpdateCamera, this));
  this->sig_OnClose.connect(blunted::Bind(&CameraPage::OnClose, this));

  this->AddView(grid);
  grid->Show();

  btnBroadcast->SetFocus();

  float zoom, height, fov, angleFactor;
  GetGameTask()->GetMatch()->GetCameraParams(zoom, height, fov, angleFactor);

  sliderZoom->SetValue(zoom);
  sliderHeight->SetValue(height);
  sliderFOV->SetValue(fov);
  sliderAngleFactor->SetValue(angleFactor);

  this->Show();
}

CameraPage::~CameraPage() {
}

void CameraPage::OnClose() {
  if (Verbose()) printf("saving camera settings\n");
  GetConfiguration()->SaveFile(GetConfigFilename());
}

void CameraPage::ApplyPreset(float zoom, float height, float fov, float angle) {
  sliderZoom->SetValue(zoom);
  sliderHeight->SetValue(height);
  sliderFOV->SetValue(fov);
  sliderAngleFactor->SetValue(angle);
  UpdateCamera();
}

void CameraPage::UpdateCamera() {
  GetConfiguration()->Set("camera_zoom", sliderZoom->GetValue());
  GetConfiguration()->Set("camera_height", sliderHeight->GetValue());
  GetConfiguration()->Set("camera_fov", sliderFOV->GetValue());
  GetConfiguration()->Set("camera_anglefactor", sliderAngleFactor->GetValue());
  GetGameTask()->GetMatch()->SetCameraParams(sliderZoom->GetValue(), sliderHeight->GetValue(), sliderFOV->GetValue(), sliderAngleFactor->GetValue());
}
