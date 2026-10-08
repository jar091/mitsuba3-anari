// mitsuba-anari: ANARI library entry object for the "mitsuba" device library.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/backend/LibraryImpl.h>

namespace mitsuba_anari {

// LibraryImpl for anariLoadLibrary("mitsuba", ...).
struct Library final : public anari::LibraryImpl {
  Library(void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
  const char **getDeviceSubtypes() override;
};

} // namespace mitsuba_anari
