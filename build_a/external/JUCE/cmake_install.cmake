# Install script for directory: G:/plug-code/external/JUCE

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/PlugCode")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
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

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("G:/plug-code/build_a/external/JUCE/modules/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("G:/plug-code/build_a/external/JUCE/extras/Build/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/JUCE-8.0.15" TYPE FILE FILES
    "G:/plug-code/build_a/external/JUCE/JUCEConfigVersion.cmake"
    "G:/plug-code/build_a/external/JUCE/JUCEConfig.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/FindCppwinrt.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/FindWebView2.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/FindWindowsMIDIServices.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/JUCECheckAtomic.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/JUCEHelperTargets.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/JUCEModuleSupport.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/JUCEUtils.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/JuceLV2Defines.h.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/LaunchScreen.storyboard"
    "G:/plug-code/external/JUCE/extras/Build/CMake/PIPAudioProcessor.cpp.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/PIPAudioProcessorWithARA.cpp.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/PIPComponent.cpp.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/PIPConsole.cpp.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/RecentFilesMenuTemplate.nib"
    "G:/plug-code/external/JUCE/extras/Build/CMake/UnityPluginGUIScript.cs.in"
    "G:/plug-code/external/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/copyDir.cmake"
    "G:/plug-code/external/JUCE/extras/Build/CMake/juce_runtime_arch_detection.cpp"
    "G:/plug-code/external/JUCE/extras/Build/CMake/juce_LinuxSubprocessHelper.cpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/JUCE-8.0.15" TYPE DIRECTORY FILES "G:/plug-code/external/JUCE/extras/Build/CMake/juce_vst3_helper")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "G:/plug-code/build_a/external/JUCE/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
