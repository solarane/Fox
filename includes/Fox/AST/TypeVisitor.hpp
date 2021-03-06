//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.     
// File : TypeVisitor.hpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
// This file declares the TypeVisitor class
//----------------------------------------------------------------------------//

#pragma once

#include "Type.hpp"
#include "Types.hpp"
#include "Fox/Common/Errors.hpp"
#include <utility>

namespace fox {
  /// TypeVisitor
  ///    A visitor class for the TypeBase hierarchy.
  template<typename Derived, typename Rtr = void, typename ... Args>
  class TypeVisitor {
    public:
      // Visit Types dispatch method
      Rtr visit(Type type, Args... args) {
        assert(type && "Cannot be used on a null pointer");
        switch (type->getKind()) {
          #define TYPE(ID,PARENT)                                             \
            case TypeKind::ID:                                                \
              return static_cast<Derived*>(this)->                            \
                visit##ID(type->getAs<ID>(), ::std::forward<Args>(args)...);
          #include "TypeNodes.def"
          default:
            fox_unreachable("Unknown node");
        }
      }

      // Sometimes, visit methods might not return (e.g. call fox_unreachable)
      // and MSVC is sort of pedantic about this and will complain about 
      // unreachable code in the .def file. 
      // The error it emits is unhelpful and not useful, so we're going to
      // disable it for visit methods.
      #pragma warning(push)
      #pragma warning(disable:4702)

      #define VISIT_METHOD(NODE, PARENT)                      \
      Rtr visit##NODE(NODE* node, Args... args) {             \
        return static_cast<Derived*>(this)->                  \
          visit##PARENT(node, ::std::forward<Args>(args)...); \
      }

      #define TYPE(ID, PARENT) VISIT_METHOD(ID, PARENT)
      #define ABSTRACT_TYPE(ID, PARENT) VISIT_METHOD(ID, PARENT)
      #include "TypeNodes.def"
      #undef VISIT_METHOD

      #pragma warning(pop)
  };
}