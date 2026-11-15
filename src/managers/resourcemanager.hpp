// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_MANAGERS_RESOURCE
#define _HPP_MANAGERS_RESOURCE

#include "defines.hpp"

#include "base/log.hpp"
#include "base/utils.hpp"

#include "types/lockable.hpp"
#include "types/loader.hpp"

#include "managers/environmentmanager.hpp"

#include <filesystem>
#include <vector>

namespace blunted {

  template <typename T>
  class Resource;

  template <typename T>
  class ResourceManager {

    public:
      ResourceManager(const std::string &typeDescription) : typeDescription(typeDescription) {};

      ~ResourceManager() {
        std::vector<blunted::intrusive_ptr<Resource<T>>> deferredRelease;
        resources.Lock();
        deferredRelease.reserve(resources.data.size());
        for (auto &kv : resources.data) {
          deferredRelease.push_back(kv.second);
        }
        resources.data.clear();
        resources.Unlock();
        deferredRelease.clear();
        loaders.clear();
      };

      void RegisterLoader(const std::string &extension, Loader<T> *loader) {
        //printf("registering loader for extension %s\n", extension.c_str());
        loaders.insert(std::make_pair(extension, loader));
      }

      blunted::intrusive_ptr < Resource<T> > Fetch(const std::string &filename, bool load = true, bool useExisting = true) {
        bool foo;
        return Fetch(filename, load, foo, useExisting);
      }

      blunted::intrusive_ptr < Resource<T> > Fetch(const std::string &filename, bool load, bool &alreadyThere, bool useExisting) {
        std::string effectiveFilename = filename;
        if (load) {
          std::string extCheck = get_file_extension(effectiveFilename);
          std::transform(extCheck.begin(), extCheck.end(), extCheck.begin(), ::tolower);
          if (extCheck == "ase" && (loaders.find("glb") != loaders.end() || loaders.find("gltf") != loaders.end())) {
            size_t dotPos = effectiveFilename.find_last_of('.');
            if (dotPos != std::string::npos) {
              std::string stem = effectiveFilename.substr(0, dotPos);
              std::error_code ec;
              if (loaders.find("glb") != loaders.end() && std::filesystem::exists(stem + ".glb", ec)) {
                effectiveFilename = stem + ".glb";
              } else if (loaders.find("gltf") != loaders.end() && std::filesystem::exists(stem + ".gltf", ec)) {
                effectiveFilename = stem + ".gltf";
              }
            }
          }
        }
        std::string adaptedFilename = get_file_name(effectiveFilename);

        // resource already loaded?

        bool success = false;
        blunted::intrusive_ptr < Resource<T> > foundResource;

        if (useExisting) {
          foundResource = Find(adaptedFilename, success);
        }

        if (success) {
          // resource is already there! w00t, that'll win us some cycles
          // (or user wants a new copy)

          alreadyThere = true;

          return foundResource;
        }

        else {

          // create resource

          alreadyThere = false;
          blunted::intrusive_ptr < Resource <T> > resource(new Resource<T>(adaptedFilename));

          // try to load

          if (load) {
            std::string extension = get_file_extension(effectiveFilename);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            typename std::map < std::string, Loader<T>* >::iterator iter = loaders.find(extension);
            if (iter != loaders.end()) {
              (*iter).second->Load(effectiveFilename, resource);
            } else {
              Log(e_FatalError, "ResourceManager<>", "Load", "There is no loader for " + effectiveFilename);
            }
          }
          Register(resource);
          return resource;
        }
      }

      blunted::intrusive_ptr < Resource<T> > FetchCopy(const std::string &filename, const std::string &newName) {
        blunted::intrusive_ptr < Resource<T> > resource = Fetch(filename);

        // todo: this lock is probably needed, but slows down everything very much.. find out why
        resource->resourceMutex.lock();
        blunted::intrusive_ptr < Resource<T> > resourceCopy(new Resource<T>(*resource, newName));
        resource->resourceMutex.unlock();

        Register(resourceCopy);
        return resourceCopy;
      }

      blunted::intrusive_ptr < Resource<T> > FetchCopy(const std::string &filename, const std::string &newName, bool &alreadyThere) {
        bool existingFound = false;
        blunted::intrusive_ptr < Resource<T> > resourceCopy = Find(newName, existingFound);
        if (existingFound) {
          alreadyThere = true;
          return resourceCopy;
        } else {
          blunted::intrusive_ptr < Resource<T> > resource = Fetch(filename, true, alreadyThere, true);

          // todo: this lock is probably needed, but slows down everything very much.. find out why
          resource->resourceMutex.lock();
          resourceCopy = blunted::intrusive_ptr < Resource<T> >(new Resource<T>(*resource, newName));
          resource->resourceMutex.unlock();

          Register(resourceCopy);
        }

        return resourceCopy;
      }

      void RemoveUnused() {
        // Defer intrusive_ptr destruction until after unlocking `resources` so GL DeleteTexture->Wait() never blocks holding this mutex
        std::vector<blunted::intrusive_ptr<Resource<T>>> deferredRelease;

        resources.Lock();

        typename std::map < std::string, blunted::intrusive_ptr< Resource<T> > >::iterator resIter = resources.data.begin();
        while (resIter != resources.data.end()) {
          if (resIter->second->GetRefCount() == 1) {
            deferredRelease.push_back(resIter->second);
            resources.data.erase(resIter++);
          } else {
            ++resIter;
          }
        }

        resources.Unlock();
        deferredRelease.clear();
      }

    protected:

      blunted::intrusive_ptr < Resource<T> > Find(const std::string &identString, bool &success) {
        resources.Lock();
        typename std::map < std::string, blunted::intrusive_ptr< Resource<T> > >::iterator resIter = resources.data.find(identString);
        if (resIter != resources.data.end()) {
          success = true;
          blunted::intrusive_ptr < Resource<T> > resource = (*resIter).second;
          resources.Unlock();
          return resource;
        } else {
          success = false;
          resources.Unlock();
          return blunted::intrusive_ptr < Resource<T> >();
        }
      }

      void Register(blunted::intrusive_ptr < Resource<T> > resource) {

        resources.Lock();

        //printf("registering %s\n", resource->GetIdentString().c_str());
        if (resources.data.find(resource->GetIdentString()) != resources.data.end()) {
          resources.Unlock(); RemoveUnused(); resources.Lock();
          if (resources.data.find(resource->GetIdentString()) != resources.data.end()) {
            Log(e_FatalError, "ResourceManager", "Register", "Duplicate key '" + resource->GetIdentString() + "'");
          }
        }
        resources.data.insert(std::make_pair(resource->GetIdentString(), resource));

        resources.Unlock();
      }

      std::map < std::string, Loader<T>* > loaders;

      Lockable < std::map < std::string, blunted::intrusive_ptr < Resource <T> > > > resources;

      std::string typeDescription;

    private:

  };

}

#endif
