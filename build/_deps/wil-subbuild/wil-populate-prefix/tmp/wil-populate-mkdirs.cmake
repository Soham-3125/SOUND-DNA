# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "E:/Nanu/sound_dna_simple/build/_deps/wil-src")
  file(MAKE_DIRECTORY "E:/Nanu/sound_dna_simple/build/_deps/wil-src")
endif()
file(MAKE_DIRECTORY
  "E:/Nanu/sound_dna_simple/build/_deps/wil-build"
  "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix"
  "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/tmp"
  "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/src/wil-populate-stamp"
  "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/src"
  "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/src/wil-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/src/wil-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/Nanu/sound_dna_simple/build/_deps/wil-subbuild/wil-populate-prefix/src/wil-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
