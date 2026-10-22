cmake_minimum_required(VERSION 3.22)

# En Mac: binario universal (Apple Silicon + Intel), desde macOS 11. En Linux/Windows no hace nada.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS mínimo")
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquitecturas de Mac")
endif()

project(BarulloTune VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: carpeta hermana ../JUCE, o la que se indique con -DJUCE_DIR=...
if(NOT JUCE_DIR)
  set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
endif()
get_filename_component(JUCE_DIR "${JUCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
  add_subdirectory("${JUCE_DIR}" JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# Núcleo común de Escoita: a versión fixada ou, se existe, a carpeta irmá ../escoita-nucleo
# (para traballar no núcleo e probalo nos plugins á vez); tamén -DESCOITA_NUCLEO=<carpeta>
set(ESCOITA_NUCLEO_VERSION "v0.3.0")
if(NOT ESCOITA_NUCLEO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../escoita-nucleo/escoita_nucleo")
  set(ESCOITA_NUCLEO "${CMAKE_CURRENT_SOURCE_DIR}/../escoita-nucleo")
endif()
if(ESCOITA_NUCLEO)
  add_subdirectory("${ESCOITA_NUCLEO}" escoita-nucleo)
else()
  include(FetchContent)
  FetchContent_Declare(escoita_nucleo GIT_REPOSITORY https://github.com/callejodev-dotcom/escoita-nucleo.git
                       GIT_TAG ${ESCOITA_NUCLEO_VERSION} GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(escoita_nucleo)
endif()

# Formatos por sistema: Mac → AU, VST3 y app; Windows → VST3 y app; Linux → VST3, LV2 y app
if(APPLE)
  set(FORMATS AU VST3 Standalone)
elseif(WIN32)
  set(FORMATS VST3 Standalone)
else()
  set(FORMATS VST3 LV2 Standalone)
endif()

juce_add_plugin(BarulloTune
  COMPANY_NAME "Escoita"
  COMPANY_WEBSITE "https://www.escoita.link"
  COMPANY_EMAIL "ola@escoita.link"
  BUNDLE_ID "link.escoita.barullotune"
  PLUGIN_MANUFACTURER_CODE Esco
  PLUGIN_CODE Btun
  PRODUCT_NAME "Barullo Tune"
  DESCRIPTION "Corrector de afinación para voz: do efecto robot á corrección natural"
  FORMATS ${FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx "Pitch Shift"
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  LV2URI "https://www.escoita.link/barullotune"
  LV2_SHARED_LIBRARY_NAME "BarulloTune"
  COPY_PLUGIN_AFTER_BUILD FALSE)

# CLAP (clap-juce-extensions, licencia MIT): se descarga al configurar
option(BARULLO_CLAP "Compilar también el formato CLAP" ON)
if(BARULLO_CLAP)
  include(FetchContent)
  FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d   # main (2026): a 0.26.0 aínda non vai con JUCE 8
    GIT_SUBMODULES_RECURSE TRUE)
  if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)   # FetchContent_Populate + add_subdirectory(... EXCLUDE_FROM_ALL) ata CMake 3.28
  endif()
  FetchContent_GetProperties(clap-juce-extensions)
  if(NOT clap-juce-extensions_POPULATED)
    FetchContent_Populate(clap-juce-extensions)
    add_subdirectory(${clap-juce-extensions_SOURCE_DIR} ${clap-juce-extensions_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
  clap_juce_extensions_plugin(TARGET BarulloTune
    CLAP_ID "link.escoita.barullotune"
    CLAP_FEATURES audio-effect pitch-correction stereo mono
    CLAP_MANUAL_URL "https://www.escoita.link"
    CLAP_SUPPORT_URL "https://www.escoita.link")
endif()

# tipografías Montserrat e Inter (licenza OFL, ver Resources/Montserrat-OFL.txt e Inter-OFL.txt)
juce_add_binary_data(BTFonts SOURCES
  Resources/Inter-Medium.ttf
  Resources/Montserrat-SemiBold.ttf
  Resources/Montserrat-Bold.ttf)

target_sources(BarulloTune PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  third_party/mts-esp/libMTSClient.cpp)

target_include_directories(BarulloTune PRIVATE Source third_party/mts-esp)
# o cliente de MTS-ESP (de fóra) sen os avisos do noso código; en Linux precisa dlopen
if(NOT MSVC)
  set_source_files_properties(third_party/mts-esp/libMTSClient.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
target_link_libraries(BarulloTune PRIVATE ${CMAKE_DL_LIBS})

# Windows: el código está en UTF-8 (tildes, ñ, ♯)
if(MSVC)
  target_compile_options(BarulloTune PRIVATE /utf-8 /bigobj)
endif()

target_compile_definitions(BarulloTune PUBLIC
  BARULLO_VERSION="${PROJECT_VERSION}"
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BarulloTune
  PRIVATE
    BTFonts
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
escoita_produto(BarulloTune NOME "Barullo Tune" VERSION ${PROJECT_VERSION} REPO callejodev-dotcom/barullotune)

# Avisos como erros no código propio (non no de JUCE)
option(BARULLO_WERROR "Avisos como errores en Source/ y tools/" OFF)
set(BARULLO_OWN_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp tools/RenderTest.cpp)
if(BARULLO_WERROR)
  if(MSVC)
    set_source_files_properties(${BARULLO_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "/WX")
  else()
    set_source_files_properties(${BARULLO_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "-Werror")
  endif()
endif()

# Sanitizadores (memoria e comportamento indefinido) para a proba
option(BARULLO_SANITIZE "Compilar la prueba con AddressSanitizer y UBSan" OFF)

# Proba sen interface: voz sintética desafinada polo procesador
option(BARULLO_TESTS "Compilar la prueba de render" OFF)
if(BARULLO_TESTS)
  juce_add_console_app(BTRenderTest PRODUCT_NAME "BTRenderTest")
  target_sources(BTRenderTest PRIVATE tools/RenderTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp third_party/mts-esp/libMTSClient.cpp)
  target_include_directories(BTRenderTest PRIVATE Source third_party/mts-esp)
  target_link_libraries(BTRenderTest PRIVATE ${CMAKE_DL_LIBS})
  if(MSVC)
    target_compile_options(BTRenderTest PRIVATE /utf-8 /bigobj)
    # o main da proba ten ducias de procesadores na pila e MSVC non reutiliza o oco
    # entre bloques: coa pila de 1 MB de Windows, cascaba antes de escribir nada
    target_link_options(BTRenderTest PRIVATE /STACK:16777216)
  endif()
  target_compile_definitions(BTRenderTest PRIVATE BARULLO_VERSION="${PROJECT_VERSION}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BARULLO_NO_PLUGIN_ENTRY=1)
  target_link_libraries(BTRenderTest PRIVATE BTFonts juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  escoita_produto(BTRenderTest NOME "Barullo Tune" VERSION ${PROJECT_VERSION} REPO callejodev-dotcom/barullotune PROBA)
  if(BARULLO_SANITIZE AND NOT MSVC)
    target_compile_options(BTRenderTest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(BTRenderTest PRIVATE -fsanitize=address,undefined)
  endif()

  # Proba do detector en condicións difíciles (só o motor, sen JUCE)
  add_executable(BTDetectorTest tools/DetectorTest.cpp)
  target_include_directories(BTDetectorTest PRIVATE Source "${ESCOITA_NUCLEO_DIR}/probas")
  set_target_properties(BTDetectorTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BTDetectorTest_artefacts/$<CONFIG>")
  if(MSVC)
    target_compile_options(BTDetectorTest PRIVATE /utf-8 /O2)
  else()
    target_compile_options(BTDetectorTest PRIVATE -O2 -Wall -Wextra -Wshadow)
  endif()
  if(BARULLO_WERROR)
    target_compile_options(BTDetectorTest PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
  endif()
  if(BARULLO_SANITIZE AND NOT MSVC)
    target_compile_options(BTDetectorTest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(BTDetectorTest PRIVATE -fsanitize=address,undefined)
  endif()
endif()
