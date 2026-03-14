cmake_minimum_required(VERSION 3.25)
project(AetherChrome VERSION 0.1.0 LANGUAGES CXX OBJCXX)

# ── Standards ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)

# ── Linker Warnings ─────────────────────────────────────────
if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_warn_duplicate_libraries")
endif()

# ── Homebrew prefix (Apple Silicon) ────────────────────────
set(HOMEBREW_PREFIX "/opt/homebrew")

# ── Build type default ─────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Options ────────────────────────────────────────────────
option(AETHER_BUILD_TESTS  "Build Catch2 test suite"   ON)
option(AETHER_BUILD_APP    "Build Qt6 application"     ON)
option(AETHER_METAL_STUB   "Use Metal CPU stub"        OFF)
option(AETHER_AI_ENABLED   "Enable SAM2/ONNX masking"  ON)

# ── Warnings & Optimization ──────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-function)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options($<$<CONFIG:Debug>:-O0>)
    add_compile_options($<$<CONFIG:Debug>:-g>)
    add_compile_options($<$<CONFIG:Release>:-O3>)
endif()

add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

# ── Find Dependencies ───────────────────────────────────────
find_package(PkgConfig REQUIRED)

# LibRaw
pkg_check_modules(LIBRAW REQUIRED libraw)
find_library(LIBRAW_LIB_PATH
    NAMES raw raw_r libraw libraw_r
    PATHS
        ${LIBRAW_LIBRARY_DIRS}
        ${HOMEBREW_PREFIX}/opt/libraw/lib
        ${HOMEBREW_PREFIX}/lib
    REQUIRED)
message(STATUS "LibRaw found at: ${LIBRAW_LIB_PATH}")

# LittleCMS2
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)

# ONNX Runtime
if(AETHER_AI_ENABLED)
  pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET onnxruntime)
  if(NOT ONNXRUNTIME_FOUND)
    message(STATUS "onnxruntime not found via pkg-config, using Homebrew fallbacks")
    set(ONNXRUNTIME_INCLUDE_DIRS
        ${HOMEBREW_PREFIX}/include
        ${HOMEBREW_PREFIX}/include/onnxruntime)
    set(ONNXRUNTIME_LIBRARIES
        ${HOMEBREW_PREFIX}/lib/libonnxruntime.dylib)
  endif()
endif()

# Qt6
if(AETHER_BUILD_APP)
  # Qt6 via cmake config (pas pkg-config)
  set(CMAKE_PREFIX_PATH
      "/opt/homebrew/opt/qt"
      ${CMAKE_PREFIX_PATH})

  find_package(Qt6 REQUIRED COMPONENTS
      Core Gui Qml Quick QuickControls2 Concurrent)

  # MOC automatique — obligatoire pour Q_OBJECT
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOUIC OFF)  # on utilise QML, pas .ui

  qt_standard_project_setup()
endif()

# Catch2
if(AETHER_BUILD_TESTS)
  find_package(Catch2 3 QUIET)
  
  if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found locally — fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4)
    FetchContent_MakeAvailable(Catch2)
  endif()
endif()

# ── Target 1 : aether_core (static library) ───────────────
# Engine core - no Qt dependencies

set(CORE_SOURCES
    core/PipelineEngine.cpp
    core/pipeline/RawInputStage.cpp
    core/pipeline/ColorMatrixStage.cpp
    core/pipeline/DCPStage.cpp
    core/dcp/DCPProfile.cpp
    core/pipeline/ExposureStage.cpp
    core/pipeline/ToneMappingStage.cpp
    core/pipeline/OutputTransformStage.cpp
    core/pipeline/MaskCompositeStage.cpp
    core/mask/ParametricMaskBuilder.cpp
    core/gpu/MetalContext.mm
)

if(AETHER_AI_ENABLED)
  list(APPEND CORE_SOURCES
      core/ai/SAMEncoder.cpp
      core/ai/SAMDecoder.cpp
      core/ai/AIMaskController.cpp)
endif()

add_library(aether_core STATIC ${CORE_SOURCES})

target_include_directories(aether_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${LIBRAW_INCLUDE_DIRS}
)

if(AETHER_AI_ENABLED)
  target_compile_definitions(aether_core PUBLIC
      AETHER_AI_ENABLED=1)
endif()

if(AETHER_METAL_STUB)
  target_compile_definitions(aether_core PUBLIC
      AETHER_METAL_STUB=1)
endif()

target_link_libraries(aether_core PUBLIC
    ${LIBRAW_LIB_PATH}
    PkgConfig::LCMS2
    "-framework Metal"
    "-framework Foundation"
    "-framework CoreGraphics"
)

if(AETHER_AI_ENABLED)
  if(TARGET PkgConfig::ONNXRUNTIME)
    target_link_libraries(aether_core PUBLIC PkgConfig::ONNXRUNTIME)
  else()
    target_include_directories(aether_core PUBLIC ${ONNXRUNTIME_INCLUDE_DIRS})
    target_link_libraries(aether_core PUBLIC ${ONNXRUNTIME_LIBRARIES})
  endif()
endif()

# ── Metal shaders ───────────────────────────────────────────

if(NOT AETHER_METAL_STUB)
  set(METAL_SOURCES
      core/gpu/shaders/ColorMatrix.metal
      core/gpu/shaders/ToneMap.metal
      core/gpu/shaders/OutputTransform.metal)

  set(AIR_FILES "")
  foreach(METAL_SRC ${METAL_SOURCES})
      get_filename_component(STEM ${METAL_SRC} NAME_WE)
      set(AIR_FILE ${CMAKE_BINARY_DIR}/${STEM}.air)
      add_custom_command(
          OUTPUT  ${AIR_FILE}
          COMMAND xcrun -sdk macosx metal
                  -c ${CMAKE_SOURCE_DIR}/${METAL_SRC}
                  -o ${AIR_FILE}
          DEPENDS ${CMAKE_SOURCE_DIR}/${METAL_SRC}
          COMMENT "Compiling ${STEM}.metal")
      list(APPEND AIR_FILES ${AIR_FILE})
  endforeach()

  set(METALLIB ${CMAKE_BINARY_DIR}/AetherChrome.metallib)
  add_custom_command(
      OUTPUT  ${METALLIB}
      COMMAND xcrun -sdk macosx metallib
              ${AIR_FILES} -o ${METALLIB}
      DEPENDS ${AIR_FILES}
      COMMENT "Linking AetherChrome.metallib")

  add_custom_target(aether_shaders ALL DEPENDS ${METALLIB})
  add_dependencies(aether_core aether_shaders)

  target_compile_definitions(aether_core PUBLIC
      AETHER_METALLIB_PATH="${METALLIB}")
endif()

# ── Target 2 : AetherChrome (Qt App) ────────────────────────

if(AETHER_BUILD_APP)

    set(APP_SOURCES
        app/main.cpp
        app/bridge/PipelineBridge.cpp
        app/bridge/PreviewProvider.cpp)

    qt_add_executable(AetherChrome MACOSX_BUNDLE
        ${APP_SOURCES})

    # Module QML — chemin URI doit correspondre
    # à l'import dans les fichiers QML
    qt_add_qml_module(AetherChrome
        URI AetherChrome
        VERSION 1.0
        QML_FILES
            app/ui/Main.qml
            app/ui/Viewer.qml
            app/ui/ControlPanel.qml
            app/ui/MaskOverlay.qml
            app/ui/FilmStrip.qml
            app/ui/components/ASlider.qml
            app/ui/components/ALabel.qml
    )

    target_include_directories(AetherChrome PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/app)

    target_link_libraries(AetherChrome PRIVATE
        aether_core
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent)

    set_target_properties(AetherChrome PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER  "com.aetherchrome.app"
        MACOSX_BUNDLE_BUNDLE_NAME     "AetherChrome"
        MACOSX_BUNDLE_BUNDLE_VERSION  "0.1.0"
        MACOSX_BUNDLE_COPYRIGHT       "Antoine Bedos"
        MACOSX_BUNDLE_INFO_PLIST
            ${CMAKE_SOURCE_DIR}/app/Info.plist.in)

    if(AETHER_AI_ENABLED)
      set(MODEL_SOURCES 
          "${CMAKE_CURRENT_SOURCE_DIR}/models/mobile_sam_image_encoder.onnx"
          "${CMAKE_CURRENT_SOURCE_DIR}/models/sam_mask_decoder_single.onnx"
      )

      foreach(MODEL_FILE ${MODEL_SOURCES})
          get_filename_component(MODEL_NAME ${MODEL_FILE} NAME)
          add_custom_command(
              TARGET AetherChrome POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AetherChrome>/models"
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_FILE}" "$<TARGET_FILE_DIR:AetherChrome>/models/${MODEL_NAME}"
              COMMENT "Copying AI model ${MODEL_NAME} to app bundle"
          )
      endforeach()
    endif()

endif()

# ── Target 3 : tests ────────────────────────────────────────

if(AETHER_BUILD_TESTS)

  set(TEST_FILES
      tests/test_RawInputStage.cpp
      tests/test_ColorMatrixStage.cpp
      tests/test_ExposureAndToneMapping.cpp
      tests/test_OutputTransform.cpp
      tests/test_PipelineEngine.cpp
      tests/test_MaskComposite.cpp
      tests/test_MetalPipeline.cpp
      tests/test_AI.cpp
      tests/run_integration_log.cpp)

  foreach(TEST_FILE ${TEST_FILES})
      get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
      add_executable(${TEST_NAME} ${TEST_FILE})
      target_include_directories(${TEST_NAME} PRIVATE
          ${CMAKE_SOURCE_DIR})
      target_link_libraries(${TEST_NAME} PRIVATE
          aether_core
          Catch2::Catch2WithMain)
      add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  endforeach()

  enable_testing()

endif()
