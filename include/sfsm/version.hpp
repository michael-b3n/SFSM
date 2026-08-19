#pragma once

#define SFSM_VERSION_MAJOR  0
#define SFSM_VERSION_MINOR  1
#define SFSM_VERSION_PATCH  0
#define SFSM_VERSION_STRING "0.1.0"

namespace sfsm
{

///
/// Version of the library, kept in sync with the project version in CMakeLists.txt.
///
inline constexpr int version_major = SFSM_VERSION_MAJOR;
inline constexpr int version_minor = SFSM_VERSION_MINOR;
inline constexpr int version_patch = SFSM_VERSION_PATCH;

} // namespace sfsm
