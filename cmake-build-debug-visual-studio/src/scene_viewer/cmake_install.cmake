# Install script for directory: D:/Coding/C++_Projects/Capsaicin/src/scene_viewer

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
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/scene_viewer.exe")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin" TYPE EXECUTABLE FILES "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/bin/scene_viewer.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/capsaicin.dll;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/dxcompiler.dll;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/dxil.dll;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/WinPixEventRuntime.dll;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/D3D12Core.dll;D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/d3d12SDKLayers.dll")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin" TYPE FILE FILES
    "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/bin/capsaicin.dll"
    "D:/Coding/C++_Projects/Capsaicin/third_party/gfx/third_party/dxc/bin/x64/dxcompiler.dll"
    "D:/Coding/C++_Projects/Capsaicin/third_party/gfx/third_party/dxc/bin/x64/dxil.dll"
    "D:/Coding/C++_Projects/Capsaicin/third_party/gfx/third_party/WinPixEventRuntime/bin/x64/WinPixEventRuntime.dll"
    "D:/Coding/C++_Projects/Capsaicin/third_party/agility_sdk/D3D12Core.dll"
    "D:/Coding/C++_Projects/Capsaicin/third_party/agility_sdk/d3d12SDKLayers.dll"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin/assets")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/install/bin" TYPE DIRECTORY FILES "D:/Coding/C++_Projects/Capsaicin/src/scene_viewer/../../assets" FILES_MATCHING REGEX "/[^/]*\\.gltf$" REGEX "/[^/]*\\.bin$" REGEX "/[^/]*\\.png$" REGEX "/[^/]*\\.ktx2$")
endif()

