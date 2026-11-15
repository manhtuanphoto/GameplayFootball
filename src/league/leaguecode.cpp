// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "leaguecode.hpp"

#include <filesystem>
#include "base/utils.hpp"
#include "utils/database.hpp"
#include "utils/xmlloader.hpp"

int CreateNewLeagueSave(const std::string &srcDbName, const std::string &saveName) {

  // copy db file

  int errorCode = 0;
  // 1 == could not create save dir
  // 2 == could not copy db file
  // 3 == could not open copied database
  // 4 == could not copy file

  std::filesystem::path source("databases");
  source /= srcDbName;
  source /= "database.sqlite";

  std::filesystem::path dest("saves");
  dest /= saveName;

  if (!CreateDirectory(dest)) {
    errorCode = 1; // could not create dir
  } else {
    if (!CopyFile(source, dest)) errorCode = 2; // could not copy file
  }


  // copy league db to tmp db

  std::error_code error;
  namespace fs = std::filesystem;
  fs::copy_file(dest / "database.sqlite", dest / "autosave.sqlite", fs::copy_options::overwrite_existing, error);


  // check db for graphics files and copy those

  Database *database;

  if (errorCode == 0) {
    database = GetDB();
    if (database != nullptr && !database->Load(dest.string() + "/autosave.sqlite")) {
      errorCode = 3; // could not open database
      return errorCode;
    }
  	
    std::vector<std::string> imageList;
    DatabaseResult *result = database->Query("select logo_url, kit_url from teams");
    for (unsigned int r = 0; r < result->data.size(); r++) {
      imageList.push_back(result->data.at(r).at(0));
      imageList.push_back(result->data.at(r).at(1));
    }
    delete result;
    result = database->Query("select logo_url from competitions");
    for (unsigned int r = 0; r < result->data.size(); r++) {
      imageList.push_back(result->data.at(r).at(0));
    }
    delete result;


    // create directories, copy files

    for (unsigned int i = 0; i < imageList.size(); i++) {
      //printf("copying %s\n", imageList.at(i).c_str());
      std::vector<std::string> tokens;
      tokenize(imageList.at(i), tokens, "/\\");

      std::filesystem::path newdir = dest;
      for (unsigned int x = 0; x < tokens.size() - 1; x++) {
        // does directory exist?
        newdir /= tokens.at(x);
        if (!std::filesystem::exists(newdir)) {
          std::filesystem::create_directories(newdir);
        }
      }
      std::filesystem::path destfile = newdir / tokens.at(tokens.size() - 1);
      std::filesystem::path sourcefile("databases");
      sourcefile /= srcDbName;
      sourcefile /= imageList.at(i);
      std::error_code copyErr;
      if (!std::filesystem::exists(destfile)) std::filesystem::copy_file(sourcefile, destfile, copyErr);
      if (copyErr) errorCode = 4;
    }
  } // if !error

  return errorCode;
}

bool PrepareDatabaseForLeague() {

  DatabaseResult *result = GetDB()->Query("CREATE TABLE settings(id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                                "managername VARCHAR(32), "
                                                                "team_id INTEGER, "
                                                                "currency VARCHAR(32), "
                                                                "difficulty FLOAT, "
                                                                "seasonyear INTEGER, "
                                                                "timestamp DATETIME)");
  delete result;

  result = GetDB()->Query("CREATE TABLE calendar(id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                                "timestamp DATETIME, "
                                                                "team1_id INTEGER, "
                                                                "team2_id INTEGER, "
                                                                "competition_id INTEGER, "
                                                                "tournament_id INTEGER, "
                                                                "timestamp DATETIME)");
  delete result;

  result = GetDB()->Query("ALTER TABLE players ADD COLUMN stats_temporal BLOB");
  delete result;

  // copy stats XML tree into a new XML tree as subset 'current' (this tree is also going to contain the archive per year)

  result = GetDB()->Query("SELECT id, stats FROM players");

  std::string insertTemporalStatsQuery = "begin transaction;";

  for (unsigned int r = 0; r < result->data.size(); r++) {
    std::string playerIDString = result->data.at(r).at(0);
    std::string statsString = result->data.at(r).at(1);

    XMLLoader loader;
    XMLTree tree = loader.Load(statsString);
//    loader.PrintTree(tree);
//    printf("\n\n\n");
    XMLTree resultTree;
    resultTree.children.insert(std::pair<std::string, XMLTree>("current", tree));

    std::string resultTreeString = loader.GetSource(resultTree);
    insertTemporalStatsQuery += "UPDATE players SET stats_temporal ='" + resultTreeString + "' WHERE id = " + playerIDString + ";";
  }

  delete result;

  insertTemporalStatsQuery += "commit;";
  DatabaseResult *insertTemporalStats = GetDB()->Query(insertTemporalStatsQuery);
  delete insertTemporalStats;

  return true;
}

bool SaveAutosaveToDatabase() {

  namespace fs = std::filesystem;
  fs::path dest("saves");
  dest /= GetActiveSaveDirectory();

  std::error_code error;

  // remove previous database
  if (fs::exists(dest / "database.sqlite", error)) fs::remove(dest / "database.sqlite", error);

  // copy autosave to database
  fs::copy_file(dest / "autosave.sqlite", dest / "database.sqlite", fs::copy_options::overwrite_existing, error);

  return !error;
}

bool SaveDatabaseToAutosave() {

  namespace fs = std::filesystem;
  fs::path dest("saves");
  dest /= GetActiveSaveDirectory();

  std::error_code error;

  // remove previous autosave
  if (fs::exists(dest / "autosave.sqlite", error)) fs::remove(dest / "autosave.sqlite", error);

  // copy database to autosave
  fs::copy_file(dest / "database.sqlite", dest / "autosave.sqlite", fs::copy_options::overwrite_existing, error);

  return !error;
}

bool LoadLeague() {
  return true;
}

void GenerateSeasonCalendars() {
  DatabaseResult *result = GetDB()->Query("SELECT strftime(\"%w\", timestamp) FROM settings LIMIT 1"); // day where season starts
  int dayOfWeek = atoi(result->data.at(0).at(0).c_str());
  delete result;

  // we want to start on a saturday (day of week '6')
  int offset = 6 - dayOfWeek;
  int daysToSkip = 4;
  int weekCounter = 0;
  bool weekend = true;

  bool ready = false;
  while (!ready) {

    offset += daysToSkip;
    if (daysToSkip == 3) {
      daysToSkip = 4;
      weekend = true;
      weekCounter++;
    } else {
      daysToSkip = 3;
      weekend = false;
    }
  }

  //result = GetDB()->Query("UPDATE settings SET timestamp = date(timestamp, '+" + int_to_str(offset) + " day')");
  //delete result;
}
