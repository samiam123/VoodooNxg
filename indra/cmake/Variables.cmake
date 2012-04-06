# -*- cmake -*-
#
# Definitions of variables used throughout the Second Life build
# process.
#
# Platform variables:
#
#   DARWIN  - Mac OS X
#   LINUX   - Linux
#   WINDOWS - Windows
#
# What to build:
#
#   VIEWER - viewer and other viewer-side components
#   SERVER - simulator and other server-side bits


# Relative and absolute paths to subtrees.

set(LIBS_CLOSED_PREFIX)
set(LIBS_OPEN_PREFIX)
set(LIBS_SERVER_PREFIX)
set(SCRIPTS_PREFIX ../scripts)
set(SERVER_PREFIX)
set(VIEWER_PREFIX)

set(LIBS_CLOSED_DIR ${CMAKE_SOURCE_DIR}/${LIBS_CLOSED_PREFIX})
set(LIBS_OPEN_DIR ${CMAKE_SOURCE_DIR}/${LIBS_OPEN_PREFIX})
set(LIBS_SERVER_DIR ${CMAKE_SOURCE_DIR}/${LIBS_SERVER_PREFIX})
set(SCRIPTS_DIR ${CMAKE_SOURCE_DIR}/${SCRIPTS_PREFIX})
set(SERVER_DIR ${CMAKE_SOURCE_DIR}/${SERVER_PREFIX})
set(VIEWER_DIR ${CMAKE_SOURCE_DIR}/${VIEWER_PREFIX})
set(LL_TESTS OFF CACHE BOOL "Build and run unit and integration tests (disable for build timing runs to reduce variation)")
set(VISTA_ICON OFF CACHE BOOL "Allow vista icon with pre 2008 Visual Studio IDEs. (Assumes replacement old rcdll.dll with new rcdll.dll from win sdk 7.0 or later)")

set(LIBS_PREBUILT_DIR ${CMAKE_SOURCE_DIR}/../libraries CACHE PATH
    "Location of prebuilt libraries.")

if (EXISTS ${CMAKE_SOURCE_DIR}/Server.cmake)
  # We use this as a marker that you can try to use the proprietary libraries.
  set(INSTALL_PROPRIETARY ON CACHE BOOL "Install proprietary binaries")
endif (EXISTS ${CMAKE_SOURCE_DIR}/Server.cmake)


if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(WINDOWS ON BOOL FORCE)
  set(ARCH i686)
  set(LL_ARCH ${ARCH}_win32)
  set(LL_ARCH_DIR ${ARCH}-win32)
  set(WORD_SIZE 32)
endif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(LINUX ON BOOl FORCE)

  # If someone has specified a word size, use that to determine the
  # architecture.  Otherwise, let the architecture specify the word size.
  if (WORD_SIZE EQUAL 32)
    set(ARCH i686)
  elseif (WORD_SIZE EQUAL 64)
    set(ARCH x86_64)
  else (WORD_SIZE EQUAL 32)
    if(CMAKE_SIZEOF_VOID_P MATCHES 4)
      set(ARCH i686)
      set(WORD_SIZE 32)
    else(CMAKE_SIZEOF_VOID_P MATCHES 4)
      set(ARCH x86_64)
      set(WORD_SIZE 64)
    endif(CMAKE_SIZEOF_VOID_P MATCHES 4)
  endif (WORD_SIZE EQUAL 32)

  set(LL_ARCH ${ARCH}_linux)
  set(LL_ARCH_DIR ${ARCH}-linux)
endif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(DARWIN 1)

  #SDK Compiler and Deployment targets for XCode
  if (${XCODE_VERSION} VERSION_LESS 4.0.0)
    set(CMAKE_OSX_SYSROOT /Developer/SDKs/MacOSX10.5.sdk)
    set(CMAKE_XCODE_ATTIBUTE_GCC_VERSION "4.2")
  else (${XCODE_VERSION} VERSION_LESS 4.0.0)
    set(CMAKE_OSX_SYSROOT /Developer/SDKs/MacOSX10.6.sdk)
    set(CMAKE_XCODE_ATTRIBUTE_GCC_VERSION "com.apple.compilers.llvmgcc42")
  endif (${XCODE_VERSION} VERSION_LESS 4.0.0)

  set(CMAKE_OSX_DEPLOYMENT_TARGET 10.5)

  # NOTE: To attempt an i386/PPC Universal build, add this on the configure line:
  # -DCMAKE_OSX_ARCHITECTURES:STRING='i386;ppc'
  # Build only for i386 by default, system default on MacOSX 10.6 is x86_64
  if (NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES i386)
  endif (NOT CMAKE_OSX_ARCHITECTURES)

  if (CMAKE_OSX_ARCHITECTURES MATCHES "i386" AND CMAKE_OSX_ARCHITECTURES MATCHES "ppc")
    set(ARCH universal)
  else (CMAKE_OSX_ARCHITECTURES MATCHES "i386" AND CMAKE_OSX_ARCHITECTURES MATCHES "ppc")
    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "ppc")
      set(ARCH ppc)
    else (${CMAKE_SYSTEM_PROCESSOR} MATCHES "ppc")
      set(ARCH i386)
    endif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "ppc")
  endif (CMAKE_OSX_ARCHITECTURES MATCHES "i386" AND CMAKE_OSX_ARCHITECTURES MATCHES "ppc")

  set(LL_ARCH ${ARCH}_darwin)
  set(LL_ARCH_DIR universal-darwin)
  set(WORD_SIZE 32)
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")


if (WINDOWS)
  set(PREBUILT_TYPE windows)
elseif(DARWIN)
  set(PREBUILT_TYPE darwin)
elseif(LINUX AND WORD_SIZE EQUAL 32)
  set(PREBUILT_TYPE linux)
elseif(LINUX AND WORD_SIZE EQUAL 64)
  set(PREBUILT_TYPE linux64)
endif(WINDOWS)


# Default deploy grid
set(GRID agni CACHE STRING "Target Grid")

set(VIEWER ON CACHE BOOL "Build Second Life viewer.")
set(VIEWER_CHANNEL "Voodoo" CACHE STRING "Viewer Channel Name")
set(VIEWER_LOGIN_CHANNEL ${VIEWER_CHANNEL} CACHE STRING "Fake login channel for A/B Testing")
set(VIEWER_BRANDING_ID "Voodoo" CACHE STRING "Viewer branding id (currently secondlife|snowglobe)")

# *TODO: break out proper Branding-secondlife.cmake, Branding-snowglobe.cmake, etc
set(VIEWER_BRANDING_NAME "Voodoo")
set(VIEWER_BRANDING_NAME_CAMELCASE "Voodoo")

set(STANDALONE OFF CACHE BOOL "Do not use Linden-supplied prebuilt libraries.")

if (NOT STANDALONE AND EXISTS ${CMAKE_SOURCE_DIR}/llphysics)
    set(SERVER ON CACHE BOOL "Build Second Life server software.")
endif (NOT STANDALONE AND EXISTS ${CMAKE_SOURCE_DIR}/llphysics)

if (LINUX AND SERVER AND VIEWER)
  MESSAGE(FATAL_ERROR "
The indra source does not currently support building SERVER and VIEWER at the same time.
Please set one of these values to OFF in your CMake cache file.
(either by running ccmake or by editing CMakeCache.txt by hand)
For more information, please see JIRA DEV-14943 - Cmake Linux cannot build both VIEWER and SERVER in one build environment
  ")
endif (LINUX AND SERVER AND VIEWER)

source_group("CMake Rules" FILES CMakeLists.txt)
