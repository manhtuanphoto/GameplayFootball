// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_MATERIAL
#define _HPP_MATERIAL

#include "scene/resources/surface.hpp"

namespace blunted {

  struct Material {
    blunted::intrusive_ptr < Resource<Surface> > diffuseTexture;
    blunted::intrusive_ptr < Resource<Surface> > normalTexture;
    blunted::intrusive_ptr < Resource<Surface> > specularTexture;
    blunted::intrusive_ptr < Resource<Surface> > illuminationTexture;
    float shininess;
    float specular_amount;
    Vector3 self_illumination;
  };

}

#endif
