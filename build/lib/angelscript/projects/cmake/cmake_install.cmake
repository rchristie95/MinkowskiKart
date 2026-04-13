# Install script for directory: C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/lib/angelscript/projects/cmake

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/SuperTuxKart")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "RelWithDebInfo")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/.build-tools/llvm-mingw/llvm-mingw-20260407-msvcrt-x86_64/bin/llvm-objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/libangelscript.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/lib/angelscript/projects/cmake/../../include/angelscript.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript/AngelscriptTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript/AngelscriptTargets.cmake"
         "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/CMakeFiles/Export/7d8cbdd81f211f8a933f4e480fe3726f/AngelscriptTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript/AngelscriptTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript/AngelscriptTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript" TYPE FILE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/CMakeFiles/Export/7d8cbdd81f211f8a933f4e480fe3726f/AngelscriptTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript" TYPE FILE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/CMakeFiles/Export/7d8cbdd81f211f8a933f4e480fe3726f/AngelscriptTargets-relwithdebinfo.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Angelscript" TYPE FILE FILES
    "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/lib/angelscript/projects/cmake/cmake/AngelscriptConfig.cmake"
    "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/Angelscript/AngelscriptConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartClaude - Copy/build/lib/angelscript/projects/cmake/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
