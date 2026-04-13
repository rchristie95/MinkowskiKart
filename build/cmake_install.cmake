# Install script for directory: C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy

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
  set(CMAKE_OBJDUMP "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/.build-tools/llvm-mingw/llvm-mingw-20260407-msvcrt-x86_64/bin/llvm-objdump.exe")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/bullet/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/enet/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/libsquish/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/graphics_engine/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/graphics_utils/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/tinygettext/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/mcpp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/irrlicht/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/angelscript/projects/cmake/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/lib/sheenbidi/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/bin/supertuxkart.exe")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/supertuxkart.exe" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/supertuxkart.exe")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/.build-tools/llvm-mingw/llvm-mingw-20260407-msvcrt-x86_64/bin/llvm-strip.exe" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/supertuxkart.exe")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/supertuxkart" TYPE DIRECTORY FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data" REGEX "/\\.svn$" EXCLUDE REGEX "/\\.git$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications" TYPE FILE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart.desktop")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/16x16/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_16.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/32x32/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_32.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/48x48/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_48.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/64x64/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_64.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/128x128/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_128.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_256.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_512.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/1024x1024/apps" TYPE FILE RENAME "supertuxkart.png" FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/supertuxkart_1024.png")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/metainfo" TYPE FILE FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/data/net.supertuxkart.SuperTuxKart.metainfo.xml")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE DIRECTORY FILES "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/bin/" FILES_MATCHING REGEX "/[^/]*\\.dll$")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/robso/OneDrive/Desktop/PersonalGames/MinkowskiKartStable - Copy/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
