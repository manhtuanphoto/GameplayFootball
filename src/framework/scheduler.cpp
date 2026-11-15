// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "scheduler.hpp"

#include "managers/resourcemanagerpool.hpp"
#include "managers/environmentmanager.hpp"
#include "managers/taskmanager.hpp"
#include "base/log.hpp"

namespace blunted {

  Scheduler::Scheduler(TaskManager *taskManager) : taskManager(taskManager) {
    cleanUpTimeOffset = 0;
    previousTime_ms = 0;
  }

  Scheduler::~Scheduler() {
  }

  void Scheduler::Exit() {

    if (GetSequenceCount() != 0) { // shouldn't happen
      sequences.Lock();
      for (unsigned int i = 0; i < sequences.data.size(); i++) {
        printf("sequence '%s' is stuck on entry #%i!\n", sequences.data.at(i)->taskSequence->GetName().c_str(), sequences.data.at(i)->programCounter);
      }
      sequences.Unlock();
    }
    assert(GetSequenceCount() == 0);
  }

  int Scheduler::GetSequenceCount() {
    sequences.Lock();
    int size = sequences.data.size();
    sequences.Unlock();
    return size;
  }

  void Scheduler::RegisterTaskSequence(std::shared_ptr<TaskSequence> sequence) {
    if (sequence->GetEntryCount() == 0) Log(e_FatalError, "Scheduler", "RegisterTaskSequence", "Trying to add a sequence without entries");
    sequence->AddTerminator();
    std::shared_ptr<TaskSequenceProgram> program(new TaskSequenceProgram());
    unsigned long time_ms = EnvironmentManager::GetInstance().GetTime_ms();
    program->taskSequence = sequence;
    program->programCounter = 0;
    program->previousProgramCounter = -1;
    program->sequenceStartTime = time_ms;
    program->lastSequenceTime = 0;
    program->startTime = time_ms;
    program->timesRan = 0;
    program->paused = false;
    program->dueQuit = false;
    program->readyToQuit = false;
    sequences.Lock();
    sequences.data.push_back(program);
    sequences.Unlock();
  }

  void Scheduler::UnregisterTaskSequence(std::shared_ptr<TaskSequence> sequence) {
    // todo
  }

  void Scheduler::UnregisterTaskSequence(const std::string &name) {
    sequences.Lock();
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {
        program->dueQuit = true;
        break;
      }
    }
    sequences.Unlock();
  }

  void Scheduler::PauseTaskSequence(const std::string &name) {
    sequences.Lock();
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {
        program->paused = true;
        break;
      }
    }
    sequences.Unlock();
  }

  void Scheduler::UnpauseTaskSequence(const std::string &name) {
    sequences.Lock();
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {
        program->paused = false;
        break;
      }
    }
    sequences.Unlock();
  }

  void Scheduler::ResetTaskSequenceTime(const std::string &name) {
    sequences.Lock();
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {
        program->startTime = EnvironmentManager::GetInstance().GetTime_ms();// - startTime_ms;
        program->timesRan = 0;
        break;
      }
    }
    sequences.Unlock();
  }

  unsigned long Scheduler::GetTaskSequenceTime_ms(const std::string &name) {
    unsigned int resultTime_ms = 0;
    sequences.Lock();
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {

        if (program->taskSequence->GetSkippable()) {
          resultTime_ms = program->sequenceStartTime + program->taskSequence->GetSequenceTime();
        } else {
          resultTime_ms = program->startTime + program->taskSequence->GetSequenceTime() * program->timesRan;
        }

        break;
      }
    }
    sequences.Unlock();
    return resultTime_ms;
  }

  TaskSequenceInfo Scheduler::GetTaskSequenceInfo(const std::string &name) {
    TaskSequenceInfo info;
    sequences.Lock(); // todo: cache this to overcome threading traffic slowdowns?
    for (unsigned int i = 0; i < sequences.data.size(); i++) {
      std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(i);
      if (program->taskSequence->GetName() == name) {

        info.sequenceStartTime_ms = program->sequenceStartTime;
        info.lastSequenceTime_ms = program->lastSequenceTime;
        info.startTime_ms = program->startTime;
        info.sequenceTime_ms = program->taskSequence->GetSequenceTime();
        info.timesRan = program->timesRan;

        break;
      }
    }
    sequences.Unlock();
    return info;
  }

  bool Scheduler::Run() {

    bool verbose = false;

    // sequenced version, the best yet!
    // thought up by Jurian Broertjes & Bastiaan Konings Schuiling

    std::unique_lock<std::mutex> lock(somethingIsDoneMutex);

    unsigned int firstSequence = 0;
    int quiterations = 0;
    bool sequencesQuitMessageDone = false;

    while (EnvironmentManager::GetInstance().GetQuit() == false || GetSequenceCount() > 0) {

      if (EnvironmentManager::GetInstance().GetQuit()) {

        // let sequences finish
        if (!sequencesQuitMessageDone) {
          sequences.Lock();
          for (unsigned int i = 0; i < sequences.data.size(); i++) {
            sequences.data.at(i)->dueQuit = true;
          }
          sequences.Unlock();
          sequencesQuitMessageDone = true;
        }

        // check if we're stuck
        if (sequencesQuitMessageDone && GetSequenceCount() > 0) {
          quiterations++;
          if (quiterations > 1000) {
            // something won't shut up!
            sequences.Lock();
            for (unsigned int i = 0; i < sequences.data.size(); i++) {
              printf("sequence '%s' is stuck on entry #%i!\n", sequences.data.at(i)->taskSequence->GetName().c_str(), sequences.data.at(i)->programCounter);
            }
            sequences.Unlock();
            quiterations = 0;
          }
        }
      }

      unsigned long time_ms = EnvironmentManager::GetInstance().GetTime_ms();
      unsigned long timeDiff_ms = time_ms - previousTime_ms;
      previousTime_ms = time_ms;

      cleanUpTimeOffset += timeDiff_ms;


      // find first sequence entry that needs to be started

      TaskSequenceQueueEntry dueEntry;

      sequences.Lock();

      bool someSequenceNeedsDeleting = false;

      for (unsigned int i = 0; i < sequences.data.size(); i++) {
        int programIndex = (i + firstSequence) % sequences.data.size();
        std::shared_ptr<TaskSequenceProgram> program = sequences.data.at(programIndex);

        if (verbose) printf("sequence %i, previous program counter %i, program counter %i\n", i, program->previousProgramCounter, program->programCounter);

        // check if previous entry is ready
        bool previousEntryIsReady = true;
        if (program->previousProgramCounter != -1) {

          previousEntryIsReady = program->taskSequence->GetEntry(program->previousProgramCounter)->IsReady();

          if (previousEntryIsReady && program->programCounter == program->taskSequence->GetEntryCount()) {
            program->programCounter = 0;
            program->timesRan++;
            program->lastSequenceTime = time_ms - program->sequenceStartTime;
          }

        }

        if (verbose) {
          if (previousEntryIsReady) {
            printf("sequence %i counter %i is ready\n", programIndex, program->previousProgramCounter);
          } else {
            printf("sequence %i counter %i is not ready\n", programIndex, program->previousProgramCounter);
          }
        }

        if (previousEntryIsReady) {

          long timeUntilDueEntry_ms = 0; // if programCounter != 0, we just want to start the next entry ASAP

          if (program->programCounter == 0) { // else, (re)starting sequence; find out when it's due

            if (program->dueQuit == true) {

              program->readyToQuit = true;
              someSequenceNeedsDeleting = true;

            } else if (program->paused == true) {

              if (!program->taskSequence->GetSkippable()) program->startTime += timeDiff_ms;

            } else { // not quitting or paused

              if (program->taskSequence->GetSkippable()) {
                // use relative time: don't mind if last frame lasted too long
                timeUntilDueEntry_ms = (program->sequenceStartTime + program->taskSequence->GetSequenceTime()) - time_ms;
                //printf("wiieee: %i .. %li .. %li\n", program->taskSequence->GetFrameTime(), time_ms, startTimeRel_ms);
              } else {
                // use absolute time: if not enough iterations have been done to get to frametime * timesran, start immediately
                timeUntilDueEntry_ms = (program->startTime + program->taskSequence->GetSequenceTime() * program->timesRan) - time_ms;
                //printf("wiieee: %i .. %li .. %li\n", program->taskSequence->GetSequenceTime(), time_ms, timeUntilDueEntry_ms);
              }

            }

          }

          if (!program->readyToQuit && !program->paused && (timeUntilDueEntry_ms < dueEntry.timeUntilDueEntry_ms || dueEntry.program == std::shared_ptr<TaskSequenceProgram>())) {
            dueEntry.program = program;
            dueEntry.timeUntilDueEntry_ms = timeUntilDueEntry_ms;
          }

        }

      } // checked all sequences and (hopefully) found an entry that is due next


      // delete sequences that are ready to pussy out

      if (someSequenceNeedsDeleting) {

        std::vector < std::shared_ptr<TaskSequenceProgram> >::iterator quiterator = sequences.data.begin();
        while (quiterator != sequences.data.end()) {
          std::shared_ptr<TaskSequenceProgram> program = *quiterator;
          if (program->readyToQuit == true) {
            quiterator = sequences.data.erase(quiterator);
          } else {
            quiterator++;
          }
        }

        // sequences.Unlock();
        // continue;

      } else { // (no deletes)

        firstSequence++;
        if (firstSequence >= sequences.data.size()) firstSequence = 0;

        if (dueEntry.program != std::shared_ptr<TaskSequenceProgram>() && dueEntry.timeUntilDueEntry_ms <= 0) {
          // Dispatch immediately without OS wait_for overhead; also dispatch any other sequences already due!
          for (unsigned int s = 0; s < sequences.data.size(); s++) {
            int pIdx = (s + firstSequence) % sequences.data.size();
            std::shared_ptr<TaskSequenceProgram> prog = sequences.data.at(pIdx);
            if (prog->readyToQuit || prog->paused) continue;
            bool prevReady = (prog->previousProgramCounter == -1) ||
                             prog->taskSequence->GetEntry(prog->previousProgramCounter)->IsReady();
            if (!prevReady) continue;
            if (prog->previousProgramCounter != -1 && prog->programCounter == prog->taskSequence->GetEntryCount()) {
              prog->programCounter = 0;
              prog->timesRan++;
              prog->lastSequenceTime = time_ms - prog->sequenceStartTime;
            }
            long dueMs = 0;
            if (prog->programCounter == 0) {
              if (prog->dueQuit) {
                prog->readyToQuit = true;
                continue;
              }
              if (prog->taskSequence->GetSkippable()) {
                dueMs = (prog->sequenceStartTime + prog->taskSequence->GetSequenceTime()) - time_ms;
              } else {
                dueMs = (prog->startTime + prog->taskSequence->GetSequenceTime() * prog->timesRan) - time_ms;
              }
            }
            if (dueMs <= 0) {
              if (prog->programCounter == 0) {
                prog->sequenceStartTime = time_ms;
              }
              prog->taskSequence->GetEntry(prog->programCounter)->Reset();
              prog->taskSequence->GetEntry(prog->programCounter)->Execute();
              prog->previousProgramCounter = prog->programCounter;
              prog->programCounter++;
            }
          }
        } else {
          long timeout_ms = 100;
          if (dueEntry.program != std::shared_ptr<TaskSequenceProgram>()) {
            timeout_ms = std::max(dueEntry.timeUntilDueEntry_ms, (long)1);
          }
          sequences.Unlock();
          somethingIsDone.wait_for(lock, std::chrono::milliseconds(timeout_ms));
          sequences.Lock();
        }

      }

      sequences.Unlock();


      // cleanup unused resources

      if (cleanUpTimeOffset > 5000) { // every 5 seconds
        //printf("cleanup..\n");
        ResourceManagerPool::GetInstance().CleanUp();
        cleanUpTimeOffset = 0;
      }

    }

    return true;
  }


}
