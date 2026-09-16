# plug-code — multi-effet séquencé maison (Lascaux Lab)
# J2 : compilation reproductible + VST3 de mesure. J3 : le socle (moteur sans hôte).
# Chaîne figée : MSVC 2022 (Build Tools 17.14), CMake >= 3.22, JUCE 8.0.15
# (sous-module external/JUCE, commit 91ad83ae34a81e0833b1a2b0866f54846370ae53).
# Licence : AGPLv3 (usage personnel, voir LICENSE).

cmake_minimum_required(VERSION 3.22)

project(PlugCode VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Cible x64 uniquement.")
endif()

# JUCE figé : on refuse de construire si le sous-module n'est pas au commit attendu.
set(PLUG_JUCE_EXPECTED_COMMIT "91ad83ae34a81e0833b1a2b0866f54846370ae53")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" rev-parse HEAD
        OUTPUT_VARIABLE PLUG_JUCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE PLUG_JUCE_GIT_RESULT)
    if(PLUG_JUCE_GIT_RESULT EQUAL 0 AND NOT PLUG_JUCE_COMMIT STREQUAL PLUG_JUCE_EXPECTED_COMMIT)
        message(FATAL_ERROR "external/JUCE est au commit ${PLUG_JUCE_COMMIT}, attendu ${PLUG_JUCE_EXPECTED_COMMIT} (tag 8.0.15). Chaîne non reproductible.")
    endif()
endif()

add_subdirectory(external/JUCE)

# Échafaudage J2 : éditeur générique JUCE pour que Live puisse « configurer »
# et automatiser les paramètres. Pas l'interface du produit. Reste ON jusqu'au J4
# (décision pilote, ETAT Rév. 4).
option(PLUG_J2_GENERIC_EDITOR "Éditeur générique JUCE (échafaudage J2)" ON)

# Identité VST3 — FIGÉE. Le couple (fabricant, code) et le nom du produit
# forment l'identité que Live mémorise dans ses projets. Ne jamais changer.
set(PLUG_PRODUCT_NAME "Plug")
set(PLUG_COMPANY_NAME "Lascaux Lab")
set(PLUG_MANUFACTURER_CODE "Lscx")
set(PLUG_PLUGIN_CODE "Plug")

# Le socle : tout src/ hors l'adaptateur hôte. Testable sans Ableton (§5, §8.3).
set(PLUG_ENGINE_SOURCES
    src/ParameterGrid.h
    src/ParameterGrid.cpp
    src/GridMap.h
    src/StepValue.h
    src/StepValue.cpp
    src/StateSchema.h
    src/StateSchema.cpp
    src/Clock.h
    src/Modulation.h
    src/Skill.h
    src/Skill.cpp
    src/Engine.h
    src/Engine.cpp
    src/dummies/DummySkills.h
    src/dummies/DummySkills.cpp
    src/BlockTimer.h)

set(PLUG_CORE_SOURCES
    ${PLUG_ENGINE_SOURCES}
    src/PlugProcessor.h
    src/PlugProcessor.cpp)

set(PLUG_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

juce_add_plugin(Plug
    PRODUCT_NAME "${PLUG_PRODUCT_NAME}"
    COMPANY_NAME "${PLUG_COMPANY_NAME}"
    PLUGIN_MANUFACTURER_CODE "${PLUG_MANUFACTURER_CODE}"
    PLUGIN_CODE "${PLUG_PLUGIN_CODE}"
    BUNDLE_ID "lab.lascaux.plug"
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Plug PRIVATE ${PLUG_CORE_SOURCES})
target_include_directories(Plug PRIVATE src)
target_compile_definitions(Plug PUBLIC
    ${PLUG_COMMON_DEFS}
    JUCE_VST3_CAN_REPLACE_VST2=0
    PLUG_J2_TIMING=1
    PLUG_J2_GENERIC_EDITOR=$<BOOL:${PLUG_J2_GENERIC_EDITOR}>)
target_link_libraries(Plug
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Banc J2 : grille, état, coût par bloc du processeur, sans enveloppe VST3.
juce_add_console_app(PlugBench PRODUCT_NAME "PlugBench")
target_sources(PlugBench PRIVATE bench/main.cpp ${PLUG_CORE_SOURCES})
target_include_directories(PlugBench PRIVATE src)
target_compile_definitions(PlugBench PRIVATE
    ${PLUG_COMMON_DEFS}
    PLUG_J2_TIMING=0
    PLUG_J2_GENERIC_EDITOR=0
    JUCE_STANDALONE_APPLICATION=1
    PLUG_NO_PLUGIN_FACTORY=1)
target_link_libraries(PlugBench
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Rendu hors hôte J3 : lit un WAV, applique un état, écrit un WAV ; matrice de test (§3.6).
juce_add_console_app(PlugRender PRODUCT_NAME "PlugRender")
target_sources(PlugRender PRIVATE render/main.cpp ${PLUG_ENGINE_SOURCES})
target_include_directories(PlugRender PRIVATE src)
target_compile_definitions(PlugRender PRIVATE
    ${PLUG_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(PlugRender
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
