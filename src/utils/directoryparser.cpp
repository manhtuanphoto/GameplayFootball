// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "directoryparser.hpp"

#include "base/log.hpp"

namespace fs = std::filesystem;

namespace blunted {

  DirectoryParser::DirectoryParser() {
  }

  DirectoryParser::~DirectoryParser() {
  }

  void DirectoryParser::Parse(std::filesystem::path path, const std::string &extension, std::vector<std::string> &files, bool recurse) {
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
      Log(e_Error, "DirectoryParser", "Parse", "Could not open directory " + path.string() + " for reading");
      return;
    }

    for (const auto &entry : fs::directory_iterator(path, ec)) {
      if (entry.is_directory(ec)) {
        if (recurse) {
          fs::path thePath = path / entry.path().filename();
          Parse(thePath, extension, files, recurse);
        }
      } else {
        fs::path thePath = path / entry.path().filename();
        if (thePath.extension() == "." + extension) {
          files.push_back(path.generic_string() + "/" + thePath.filename().string());
        }
      }
    }
  }

}
