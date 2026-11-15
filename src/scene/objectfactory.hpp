// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_OBJECTFACTORY
#define _HPP_OBJECTFACTORY

#include "defines.hpp"

#include "object.hpp"

#include "types/singleton.hpp"

namespace blunted {

  class ObjectFactory : public Singleton<ObjectFactory> {

    public:
      ObjectFactory();
      virtual ~ObjectFactory();

      virtual void Exit();

      blunted::intrusive_ptr<Object> CreateObject(const std::string &name, std::string objectTypeStr, std::string postfix = "_copy");
      blunted::intrusive_ptr<Object> CreateObject(const std::string &name, e_ObjectType objectType, std::string postfix = "_copy");
      blunted::intrusive_ptr<Object> CopyObject(blunted::intrusive_ptr<Object> source, std::string postfix = "_copy");
      void RegisterPrototype(e_ObjectType objectType, blunted::intrusive_ptr<Object> prototype);

    protected:
      std::map <e_ObjectType, blunted::intrusive_ptr<Object> > prototypes;

  };

}

#endif
