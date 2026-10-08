// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "core/Object.h"

#include <anari/frontend/type_utility.h>

namespace mitsuba_anari {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, MitsubaGlobalState *s)
    : helium::BaseObject(type, s)
{}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  // no-op
}

bool Object::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint64_t size, uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  }

  return false;
}

bool Object::isValid() const
{
  return true;
}

MitsubaGlobalState *Object::deviceState() const
{
  return (MitsubaGlobalState *)helium::BaseObject::m_state;
}

// UnknownObject definitions //////////////////////////////////////////////////

UnknownObject::UnknownObject(ANARIDataType type, MitsubaGlobalState *s)
    : Object(type, s)
{
  // Unknown/unimplemented subtypes are never silent (master prompt §12/§30):
  // the object exists so lifetime rules still hold, but it is invalid and the
  // application is told why.
  reportMessage(ANARI_SEVERITY_WARNING,
      "[mitsuba] created unknown/unimplemented object subtype (type %s)",
      anari::toString(type));
}

UnknownObject::UnknownObject(
    ANARIDataType type, std::string_view subtype, MitsubaGlobalState *s)
    : Object(type, s)
{
  const std::string name(subtype);
  reportMessage(ANARI_SEVERITY_WARNING,
      "[mitsuba] %s subtype '%s' is not supported by the mitsuba device; "
      "objects using it are ignored",
      anari::toString(type),
      name.c_str());
}

bool UnknownObject::isValid() const
{
  return false;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Object *);
