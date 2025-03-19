# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Coding/C++_Projects/Capsaicin/third_party/gfx/third_party/cgltf"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-build"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/tmp"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/src/cgltf-populate-stamp"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/src"
  "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/src/cgltf-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/src/cgltf-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Coding/C++_Projects/Capsaicin/cmake-build-debug-visual-studio/_deps/cgltf-subbuild/cgltf-populate-prefix/src/cgltf-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
