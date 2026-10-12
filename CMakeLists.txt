cmake_minimum_required(VERSION 3.22)

project(OniriqueTuner VERSION 1.1.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
#  JUCE 8 (téléchargé automatiquement depuis GitHub : nécessite Git + Internet).
#  Pour réutiliser une copie déjà présente sur le disque, configurer avec
#  -DFETCHCONTENT_SOURCE_DIR_JUCE="chemin/vers/JUCE" (voir README.md).
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
#  Logo et polices de la charte ONIRIQUE, intégrés au binaire.
# ---------------------------------------------------------------------------
juce_add_binary_data(OniriqueTunerData SOURCES
    Source/GUI/Assets/sigil.png
    Source/GUI/Assets/Unbounded800.ttf
    Source/GUI/Assets/GrenzeGotisch600.ttf
    Source/GUI/Assets/HankenGrotesk600.ttf
    Source/GUI/Assets/JetBrainsMono500.ttf
    Source/GUI/Assets/JetBrainsMono600.ttf)
set_target_properties(OniriqueTunerData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Sur Mac, le plugin sort aussi en Audio Unit, le seul format que lisent Logic Pro et GarageBand.
if(APPLE)
    set(ONIRIQUE_FORMATS VST3 AU)
else()
    set(ONIRIQUE_FORMATS VST3)
endif()

juce_add_plugin(OniriqueTuner
    COMPANY_NAME                "Onirique"
    PLUGIN_MANUFACTURER_CODE    Onrq
    PLUGIN_CODE                 Otn1
    FORMATS                     ${ONIRIQUE_FORMATS}
    PRODUCT_NAME                "ONIRIQUE TUNER"
    DESCRIPTION                 "Correction de hauteur pour la voix"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx "Pitch Shift"
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueTuner PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/Parameters.h
    Source/Parameters.cpp
    Source/DSP/Fft.h
    Source/DSP/Scales.h
    Source/DSP/SincTable.h
    Source/DSP/PitchDetector.h
    Source/DSP/PitchDetector.cpp
    Source/DSP/PitchCorrector.h
    Source/DSP/PitchCorrector.cpp
    Source/DSP/TuneEngine.h
    Source/DSP/TuneEngine.cpp
    Source/DSP/Vocoder.h
    Source/DSP/Vocoder.cpp
    Source/GUI/Theme.h
    Source/GUI/TunerLookAndFeel.h
    Source/GUI/TunerLookAndFeel.cpp
    Source/GUI/Controls.h
    Source/GUI/Controls.cpp
    Source/GUI/PitchViews.h
    Source/GUI/PitchViews.cpp)

# Licence ONIRIQUE : le dossier License/ est le même dans tous les plugins (voir License/README.md).
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueTuner)

target_include_directories(OniriqueTuner PRIVATE Source)

target_compile_definitions(OniriqueTuner PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(MSVC)
    target_compile_options(OniriqueTuner PRIVATE /utf-8)
endif()

target_link_libraries(OniriqueTuner
    PRIVATE
        OniriqueTunerData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Outil de développement : rend l'interface dans une image PNG sans ouvrir de
#  fenêtre ni de DAW. N'est pas construit par défaut.
#      cmake --build build --config Release --target OniriqueTunerSnapshot
# ---------------------------------------------------------------------------
add_executable(OniriqueTunerSnapshot EXCLUDE_FROM_ALL Tools/Snapshot.cpp)
target_include_directories(OniriqueTunerSnapshot PRIVATE
    Source
    $<TARGET_PROPERTY:OniriqueTuner,INCLUDE_DIRECTORIES>)
target_compile_definitions(OniriqueTunerSnapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueTuner,COMPILE_DEFINITIONS>)
target_link_libraries(OniriqueTunerSnapshot PRIVATE OniriqueTuner)

if(MSVC)
    target_compile_options(OniriqueTunerSnapshot PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
#  Outil de développement : petit hôte VST3 qui charge le fichier .vst3 comme
#  le ferait un DAW et vérifie nom, paramètres, latence, sauvegarde, interface.
#      cmake --build build --config Release --target OniriqueTunerHostTest
# ---------------------------------------------------------------------------
juce_add_console_app(OniriqueTunerHostTest PRODUCT_NAME "OniriqueTunerHostTest")
set_target_properties(OniriqueTunerHostTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(OniriqueTunerHostTest PRIVATE Tools/HostTest.cpp)
target_compile_definitions(OniriqueTunerHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(OniriqueTunerHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(OniriqueTunerHostTest PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
#  Outil de développement : banc d'essai de la licence (vérification des
#  fichiers .lic, et son du plugin en temps réel puis en export).
#      cmake --build build --config Release --target OniriqueLicenseTest
# ---------------------------------------------------------------------------
juce_add_console_app(OniriqueLicenseTest PRODUCT_NAME "OniriqueLicenseTest")
set_target_properties(OniriqueLicenseTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(OniriqueLicenseTest PRIVATE
    Tools/LicenseTest.cpp
    License/License.cpp
    License/LicenseCore.cpp
    License/tweetnacl.c)
target_include_directories(OniriqueLicenseTest PRIVATE .)
target_compile_definitions(OniriqueLicenseTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(OniriqueLicenseTest PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(OniriqueLicenseTest PRIVATE /utf-8)
endif()
