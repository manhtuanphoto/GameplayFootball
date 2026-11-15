// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "properties.hpp"

#include "defines.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <fstream>
#include <iostream>

using namespace std;

namespace blunted {

  std::string Properties::emptyString = "";

  Properties::Properties() {
  }

  Properties::~Properties() {
  }

  bool Properties::Exists(const char *name) const {
    return properties.find(std::string_view(name)) != properties.end();
  }

  void Properties::Set(const char *name, const std::string &value) {
    std::string key(name);
    properties[key] = value;
    numericProperties[std::move(key)] = static_cast<real>(atof(value.c_str()));
  }

  void Properties::SetInt(const char *name, int value) {
    std::string value_str = int_to_str(value);
    std::string key(name);
    properties[key] = std::move(value_str);
    numericProperties[std::move(key)] = static_cast<real>(value);
  }

  void Properties::Set(const char *name, real value) {
    std::string value_str = real_to_str(value);
    Set(name, value_str);
  }

  void Properties::SetBool(const char *name, bool value) {
    std::string value_str = value ? "true" : "false";
    std::string key(name);
    properties[key] = std::move(value_str);
    numericProperties[std::move(key)] = 0.0f;
  }

  const std::string &Properties::Get(const char *name, const std::string &defaultValue) const {
    auto iter = properties.find(std::string_view(name));
    if (iter == properties.end()) {
      return defaultValue;
    } else {
      return iter->second;
    }
  }

  bool Properties::GetBool(const char *name, bool defaultValue) const {
    auto iter = properties.find(std::string_view(name));
    if (iter == properties.end()) {
      return defaultValue;
    } else {
      return iter->second == "true";
    }
  }

  real Properties::GetReal(const char *name, real defaultValue) const {
    auto iter = numericProperties.find(std::string_view(name));
    if (iter == numericProperties.end()) {
      return defaultValue;
    } else {
      return iter->second;
    }
  }

  int Properties::GetInt(const char *name, int defaultValue) const {
    auto iter = numericProperties.find(std::string_view(name));
    if (iter == numericProperties.end()) {
      return defaultValue;
    } else {
      return int(floor(iter->second));
    }
  }

  Vector3 Properties::GetVector3(const char *name, Vector3 defaultValue) const {
    auto iter = properties.find(std::string_view(name));
    if (iter != properties.end()) {
      return GetVectorFromString(iter->second);
    } else {
      return defaultValue;
    }
  }

  void Properties::AddProperties(const Properties *userprops) {
    if (!userprops) return;
    AddProperties(*userprops);
  }

  void Properties::AddProperties(const Properties &userprops) {
    const map_Properties *userpropdata = userprops.GetProperties();
    for (auto iter = userpropdata->begin(); iter != userpropdata->end(); ++iter) {
      properties[iter->first] = iter->second;
      auto numIt = userprops.numericProperties.find(iter->first);
      if (numIt != userprops.numericProperties.end()) {
        numericProperties[iter->first] = numIt->second;
      } else {
        numericProperties[iter->first] = static_cast<real>(atof(iter->second.c_str()));
      }
    }
  }

  const map_Properties *Properties::GetProperties() const {
    return &properties;
  }

  void Properties::LoadFile(const std::string &filename) {
    std::vector <std::string> content;
    file_to_vector(filename, content);

    int contentSize = content.size();
    for (int i = 0; i < contentSize; i++) {
      int param_start = content.at(i).find_first_of('"') + 1;
      int param_end = content.at(i).find_first_of('"', param_start);
      int value_start = content.at(i).find_first_of('"', param_end + 1) + 1;
      int value_end = content.at(i).find_first_of('"', value_start);

      std::string param, value;

      if (param_start < param_end && value_start < value_end && param_end < value_start) {
        param = content.at(i).substr(param_start, param_end - param_start);
        value = content.at(i).substr(value_start, value_end - value_start);
        this->Set(param.c_str(), value);
      }
    }
  }

  void Properties::SaveFile(const std::string &filename) const {
    ofstream cfile;
    cfile.open(filename.c_str(), ios::out);
    map_Properties::const_iterator iter = properties.begin();
    while (iter != properties.end()) {
      std::string bla = "\"" + iter->first + "\" \"" + iter->second + "\"\n";
      cfile << bla.c_str();

      iter++;
    }
    cfile.close();
  }

  void Properties::Print() const {
    map_Properties::const_iterator iter = properties.begin();
    while (iter != properties.end()) {
      std::string bla = "\"" + iter->first + "\" \"" + iter->second + "\"\n";
      cout << bla.c_str();

      iter++;
    }
  }

}
