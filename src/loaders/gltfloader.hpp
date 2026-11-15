#ifndef _HPP_LOADERS_GLTF
#define _HPP_LOADERS_GLTF

#include "defines.hpp"
#include "types/loader.hpp"
#include "scene/resources/geometrydata.hpp"

namespace blunted {

  class GLTFLoader : public Loader<GeometryData> {

    public:
      GLTFLoader();
      virtual ~GLTFLoader();

      virtual void Load(std::string filename, blunted::intrusive_ptr < Resource <GeometryData> > resource);

  };

}

#endif
