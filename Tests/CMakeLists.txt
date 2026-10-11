cmake_minimum_required(VERSION 3.22)

# Banc de test du moteur audio, sans JUCE : se configure et se compile en
# quelques secondes. Voir README.md à la racine pour les commandes.
project(OniriqueTunerEngineTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(engine_test
    engine_test.cpp
    ../Source/DSP/PitchDetector.cpp
    ../Source/DSP/PitchCorrector.cpp
    ../Source/DSP/TuneEngine.cpp
    ../Source/DSP/Vocoder.cpp)

target_include_directories(engine_test PRIVATE ../Source)

if(MSVC)
    target_compile_options(engine_test PRIVATE /utf-8 /W4)
endif()
