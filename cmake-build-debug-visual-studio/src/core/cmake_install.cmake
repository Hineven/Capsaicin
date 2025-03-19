# Install script for directory: D:/Coding/C++_Projects/Capsaicin/src/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
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
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/lib/capsaicin.lib")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/lib/capsaicin.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/capsaicin.dll")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin" TYPE SHARED_LIBRARY FILES "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/bin/capsaicin.dll")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/include/capsaicin/capsaicin.h;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/include/capsaicin/version.h;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/include/capsaicin/capsaicin_export.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/include/capsaicin" TYPE FILE FILES
    "D:/Coding/C++_Projects/Capsaicin/src/core/include/capsaicin.h"
    "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/src/core/version.h"
    "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/src/core/capsaicin_export.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/src")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin" TYPE DIRECTORY FILES "D:/Coding/C++_Projects/Capsaicin/src/core/src" FILES_MATCHING REGEX "/[^/]*\\.vert$" REGEX "/[^/]*\\.frag$" REGEX "/[^/]*\\.geom$" REGEX "/[^/]*\\.comp$" REGEX "/[^/]*\\.hlsl$" REGEX "/[^/]*\\.rt$")
endif()

