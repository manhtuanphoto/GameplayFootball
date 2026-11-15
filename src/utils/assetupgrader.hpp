#ifndef _HPP_UTILS_ASSETUPGRADER
#define _HPP_UTILS_ASSETUPGRADER

#include <string>

namespace blunted {

  struct AssetUpgradeSummary {
    int modelsConverted = 0;
    int modelsVerified = 0;
    int animationsConverted = 0;
    int animationsVerified = 0;
    int errors = 0;
    std::string reportPath;
  };

  // Automatically upgrades all legacy .ase 3D models and .anim skeletal animations
  // under mediaRoot (default "media") into modern, Blender-ready glTF 2.0 binary (.glb) assets,
  // preserving the original files safely as fallback backups and verifying 100% round-trip parity.
  bool RunAssetLibraryUpgrade(const std::string &mediaRoot, AssetUpgradeSummary &outSummary);

}

#endif
