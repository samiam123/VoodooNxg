# -*- cmake -*-
#
# Compilation options shared by all Second Life components.

include(Variables)


# Portable compilation flags.

set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -DLL_DEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE
    "-DLL_RELEASE=1 -DLL_RELEASE_FOR_DOWNLOAD=1 -D_SECURE_SCL=0 -DLL_SEND_CRASH_REPORTS=1 -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE
    "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASESSE2
    "-DLL_RELEASE=1 -DLL_RELEASE_FOR_DOWNLOAD=1 -D_SECURE_SCL=0 -DLL_SEND_CRASH_REPORTS=1 -DNDEBUG")
#llimage now requires this (?)
set(CMAKE_C_FLAGS_RELEASESSE2
    "${CMAKE_CXX_FLAGS_RELEASESSE2}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO 
    "-DLL_RELEASE=1 -D_SECURE_SCL=0 -DLL_SEND_CRASH_REPORTS=0 -DNDEBUG -DLL_RELEASE_WITH_DEBUG_INFO=1")

# Don't bother with a MinSizeRel build.

set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release;ReleaseSSE2;Debug" CACHE STRING
    "Supported build types." FORCE)

# Platform-specific compilation flags.

if (WINDOWS)
  # Remove default /Zm1000 flag that cmake inserts
  string (REPLACE "/Zm1000" " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

  # Don't build DLLs.
  set(BUILD_SHARED_LIBS OFF)

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi /MDd /arch:SSE2"
      CACHE STRING "C++ compiler debug options" FORCE)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO 
      "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Zi /MD /MP /arch:SSE2"
      CACHE STRING "C++ compiler release-with-debug options" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASE
      "${CMAKE_CXX_FLAGS_RELEASE} ${LL_CXX_FLAGS} /O2 /Zi /MD /MP /arch:SSE /fp:fast"
      CACHE STRING "C++ compiler release options" FORCE)
  set(CMAKE_C_FLAGS_RELEASE
      "${CMAKE_C_FLAGS_RELEASE} ${LL_C_FLAGS} /O2 /Zi /MD /MP /arch:SSE /fp:fast"
      CACHE STRING "C compiler release options" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASESSE2
      "${CMAKE_CXX_FLAGS_RELEASESSE2} ${LL_CXX_FLAGS} /O2 /Zi /MD /MP /arch:SSE2 /fp:fast"
      CACHE STRING "C++ compiler release-SSE2 options" FORCE)
  set(CMAKE_C_FLAGS_RELEASESSE2
      "${CMAKE_C_FLAGS_RELEASESSE2} ${LL_C_FLAGS} /O2 /Zi /MD /MP /arch:SSE2 /fp:fast"
      CACHE STRING "C compiler release-SSE2 options" FORCE)

  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LARGEADDRESSAWARE")
           
  set(CMAKE_CXX_STANDARD_LIBRARIES "")
  set(CMAKE_C_STANDARD_LIBRARIES "")
	
  add_definitions(
      /DLL_WINDOWS=1
      /DUNICODE
      /D_UNICODE 
      /GS
      /TP
      #/W3
      /c
      /Zc:forScope
      /nologo
      /Oy-
      )
     
  # configure win32 API for windows XP+ compatibility
  set(WINVER "0x0501" CACHE STRING "Win32 API Target version (see http://msdn.microsoft.com/en-us/library/aa383745%28v=VS.85%29.aspx)")
  add_definitions("/DWINVER=${WINVER}" "/D_WIN32_WINNT=${WINVER}")

  if(MSVC80 OR MSVC90 OR MSVC10)
    set(CMAKE_CXX_FLAGS_RELEASE
      "${CMAKE_CXX_FLAGS_RELEASE} -D_SECURE_STL=0 -D_HAS_ITERATOR_DEBUGGING=0"
      CACHE STRING "C++ compiler release options" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASESSE2
      "${CMAKE_CXX_FLAGS_RELEASESSE2} -D_SECURE_STL=0 -D_HAS_ITERATOR_DEBUGGING=0"
      CACHE STRING "C++ compiler release-SSE2 options" FORCE)
    set(CMAKE_C_FLAGS_RELEASESSE2
      "${CMAKE_CXX_FLAGS_RELEASESSE2} -D_SECURE_STL=0 -D_HAS_ITERATOR_DEBUGGING=0"
      CACHE STRING "C compiler release-SSE2 options" FORCE)
    add_definitions(
      /Zc:wchar_t-
      )
  endif (MSVC80 OR MSVC90 OR MSVC10)
  
  # Are we using the crummy Visual Studio KDU build workaround?
  if (NOT VS_DISABLE_FATAL_WARNINGS)
    add_definitions(/WX)
  endif (NOT VS_DISABLE_FATAL_WARNINGS)
  
  # Various libs are compiler specific, generate some variables here we can just use
  # when we require them instead of reimplementing the test each time.
  
  if (MSVC71)
	    set(MSVC_DIR 7.1)
	    set(MSVC_SUFFIX 71)
    elseif (MSVC80)
	    set(MSVC_DIR 8.0)
	    set(MSVC_SUFFIX 80)
    elseif (MSVC90)
	    set(MSVC_DIR 9.0)
	    set(MSVC_SUFFIX 90)
    elseif (MSVC10)
	    set(MSVC_DIR 10.0)
	    set(MSVC_SUFFIX 100)
    endif (MSVC71)

  if (MSVC10)
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /MANIFEST:NO")
    SET(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /MANIFEST:NO")
  endif(MSVC10)

    
endif (WINDOWS)

set (GCC_EXTRA_OPTIMIZATIONS "-ffast-math")

if (LINUX)
  set(CMAKE_SKIP_RPATH TRUE)

  # Here's a giant hack for Fedora 8, where we can't use
  # _FORTIFY_SOURCE if we're using a compiler older than gcc 4.1.

  find_program(GXX g++)
  mark_as_advanced(GXX)

  if (GXX)
    execute_process(
        COMMAND ${GXX} --version
        COMMAND sed "s/^[gc+ ]*//"
        COMMAND head -1
        OUTPUT_VARIABLE GXX_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
  else (GXX)
    set(GXX_VERSION x)
  endif (GXX)

  # The quoting hack here is necessary in case we're using distcc or
  # ccache as our compiler.  CMake doesn't pass the command line
  # through the shell by default, so we end up trying to run "distcc"
  # " g++" - notice the leading space.  Ugh.

  execute_process(
      COMMAND sh -c "${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1} --version"
      COMMAND sed "s/^[gc+ ]*//"
      COMMAND head -1
      OUTPUT_VARIABLE CXX_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (${GXX_VERSION} STREQUAL ${CXX_VERSION})
    add_definitions(-D_FORTIFY_SOURCE=2)
  else (${GXX_VERSION} STREQUAL ${CXX_VERSION})
    if (NOT ${GXX_VERSION} MATCHES " 4.1.*Red Hat")
      add_definitions(-D_FORTIFY_SOURCE=2)
    endif (NOT ${GXX_VERSION} MATCHES " 4.1.*Red Hat")
  endif (${GXX_VERSION} STREQUAL ${CXX_VERSION})

  #Lets actually get a numerical version of gxx's version
  STRING(REGEX REPLACE ".* ([0-9])\\.([0-9])\\.([0-9]).*" "\\1\\2\\3" CXX_VERSION ${CXX_VERSION})

  #gcc 4.3 and above don't like the LL boost
  if(${CXX_VERSION} GREATER 429)
    add_definitions(-Wno-parentheses)
  endif (${CXX_VERSION} GREATER 429)

  #gcc 4.6 has a new spammy warning
  if(NOT ${CXX_VERSION} LESS 460)
    add_definitions(-Wno-unused-but-set-variable)
  endif (NOT ${CXX_VERSION} LESS 460)

  # End of hacks.

  add_definitions(
      -DLL_LINUX=1
      -D_REENTRANT
      -fexceptions
      -fno-math-errno
      -fno-strict-aliasing
      -fsigned-char
      -g
      -pthread
      )

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

  add_definitions(-DAPPID=secondlife)
  add_definitions(-fvisibility=hidden)
  # don't catch SIGCHLD in our base application class for the viewer - some of our 3rd party libs may need their *own* SIGCHLD handler to work.  Sigh!  The viewer doesn't need to catch SIGCHLD anyway.
  add_definitions(-DLL_IGNORE_SIGCHLD)
  if (NOT STANDALONE)
    # this stops us requiring a really recent glibc at runtime
    add_definitions(-fno-stack-protector)
  endif (NOT STANDALONE)
  if (${ARCH} STREQUAL "x86_64")
    add_definitions(-DLINUX64=1 -pipe)
    set(CMAKE_CXX_FLAGS_RELEASESSE2 "${CMAKE_CXX_FLAGS_RELEASESSE2} -fomit-frame-pointer -mmmx -msse -mfpmath=sse -msse2 -ffast-math -ftree-vectorize -fweb -fexpensive-optimizations -frename-registers")
    set(CMAKE_C_FLAGS_RELEASESSE2 "${CMAKE_C_FLAGS_RELEASESSE2} -fomit-frame-pointer -mmmx -msse -mfpmath=sse -msse2 -ffast-math -ftree-vectorize -fweb -fexpensive-optimizations -frename-registers")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fomit-frame-pointer -mmmx -msse -mfpmath=sse -msse2 -ffast-math -ftree-vectorize -fweb -fexpensive-optimizations -frename-registers")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fomit-frame-pointer -mmmx -msse -mfpmath=sse -msse2 -ffast-math -ftree-vectorize -fweb -fexpensive-optimizations -frename-registers")
  else (${ARCH} STREQUAL "x86_64")
    if (NOT STANDALONE)
  	set(MARCH_FLAG " -march=pentium4")
    endif (NOT STANDALONE)
    set(CMAKE_CXX_FLAGS_RELEASESSE2 "${CMAKE_CXX_FLAGS_RELEASESSE2}${MARCH_FLAG} -mfpmath=sse,387 -msse2 ${GCC_EXTRA_OPTIMIZATIONS}")
    set(CMAKE_C_FLAGS_RELEASESSE2 "${CMAKE_C_FLAGS_RELEASESSE2}${MARCH_FLAG} -mfpmath=sse,387 -msse2 ${GCC_EXTRA_OPTIMIZATIONS}")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}${MARCH_FLAG} -mfpmath=sse,387 -msse2 ${GCC_EXTRA_OPTIMIZATIONS}")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}${MARCH_FLAG} -mfpmath=sse,387 -msse2 ${GCC_EXTRA_OPTIMIZATIONS}")
  endif (${ARCH} STREQUAL "x86_64")

  set(CMAKE_CXX_FLAGS_DEBUG "-fno-inline ${CMAKE_CXX_FLAGS_DEBUG} -msse2")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${CMAKE_CXX_FLAGS_RELEASE}")
  set(CMAKE_C_FLAGS_RELEASE "-O3 ${CMAKE_C_FLAGS_RELEASE}")
  set(CMAKE_CXX_FLAGS_RELEASESSE2 "-O3 ${CMAKE_CXX_FLAGS_RELEASESSE2}")
  set(CMAKE_C_FLAGS_RELEASESSE2 "-O3 ${CMAKE_C_FLAGS_RELEASESSE2}")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif (LINUX)


if (DARWIN)
  add_definitions(-DLL_DARWIN=1 -D_XOPEN_SOURCE)
  set(CMAKE_CXX_LINK_FLAGS "-Wl,-headerpad_max_install_names,-search_paths_first")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_CXX_LINK_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlong-branch")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mlong-branch")
  # NOTE: it's critical that the optimization flag is put in front.
  # NOTE: it's critical to have both CXX_FLAGS and C_FLAGS covered.
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -msse3 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O3 -msse3 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -msse3 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -msse3 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
  set(CMAKE_CXX_FLAGS_RELEASESSE2 "${CMAKE_CXX_FLAGS_RELEASESSE2} -O3 -msse2 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
  set(CMAKE_C_FLAGS_RELEASESSE2 "${CMAKE_C_FLAGS_RELEASESSE2} -O3 -msse2 -mtune=generic -mfpmath=sse ${GCC_EXTRA_OPTIMIZATIONS}")
endif (DARWIN)


if (LINUX OR DARWIN)
  set(GCC_WARNINGS "-Wall -Wno-sign-compare -Wno-trigraphs")

  if (NOT GCC_DISABLE_FATAL_WARNINGS)
    set(GCC_WARNINGS "${GCC_WARNINGS} -Werror")
  endif (NOT GCC_DISABLE_FATAL_WARNINGS)

  set(GCC_CXX_WARNINGS "${GCC_WARNINGS} -Wno-reorder -Wno-non-virtual-dtor -Woverloaded-virtual")

  set(CMAKE_C_FLAGS "${GCC_WARNINGS} ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${GCC_CXX_WARNINGS} ${CMAKE_CXX_FLAGS}")

  if (WORD_SIZE EQUAL 32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
  elseif (WORD_SIZE EQUAL 64)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64")
  endif (WORD_SIZE EQUAL 32)
endif (LINUX OR DARWIN)


if (STANDALONE)
  add_definitions(-DLL_STANDALONE=1)
else (STANDALONE)
  set(${ARCH}_linux_INCLUDES
      ELFIO
      atk-1.0
      glib-2.0
      gstreamer-0.10
      gtk-2.0
      llfreetype2
      pango-1.0
      )
endif (STANDALONE)

if(1 EQUAL 1)
	add_definitions(-DOPENSIM_RULES=1)
	add_definitions(-DMESH_ENABLED=1)
endif(1 EQUAL 1)

SET( CMAKE_EXE_LINKER_FLAGS_RELEASESSE2
    "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING
    "Flags used for linking binaries under SSE2 build."
    FORCE )
SET( CMAKE_SHARED_LINKER_FLAGS_RELEASESSE2
    "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING
    "Flags used by the shared libraries linker under SSE2 build."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_RELEASESSE2
    CMAKE_C_FLAGS_RELEASESSE2
    CMAKE_EXE_LINKER_FLAGS_RELEASESSE2
    CMAKE_SHARED_LINKER_FLAGS_RELEASESSE2 )
