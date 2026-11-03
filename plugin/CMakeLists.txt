cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin/analysis_worker.cpp
    src/plugin/mask_filter.cpp
    src/plugin/module_paths.cpp
    src/plugin/obs_frame.cpp
    src/plugin/plugin_main.cpp
    src/plugin/status_text.cpp
)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ---- 相手名マスクの中核（OBS に依存しない。方式・構成設計書 §5） ----
add_library(omask_core STATIC)
target_sources(
  omask_core
  PRIVATE
    src/core/analyzer.cpp
    src/core/color.cpp
    src/core/delay.cpp
    src/core/draw.cpp
    src/core/fixed_policy.cpp
    src/core/frame.cpp
    src/core/geometry.cpp
    src/core/mask_planner.cpp
    src/core/preprocess.cpp
    src/core/result_band.cpp
    src/core/result_store.cpp
    src/core/screen_judge.cpp
    src/core/settings.cpp
    src/core/status.cpp
    src/core/tag_decode.cpp
    src/core/tag_policy.cpp
    src/core/text.cpp
    src/core/timing.cpp
)
target_include_directories(omask_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(omask_core PRIVATE /W4)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE omask_core)

# ---- 推論（ONNX Runtime DirectML 版を明示的に読み込む。方式・構成設計書 §5） ----
include(cmake/omask_onnxruntime.cmake)
add_library(omask_infer STATIC)
target_sources(omask_infer PRIVATE src/infer/ort_engine.cpp)
target_include_directories(omask_infer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(omask_infer SYSTEM PRIVATE "${OMASK_ORT_INCLUDE_DIR}")
target_link_libraries(omask_infer PUBLIC omask_core)
target_compile_options(omask_infer PRIVATE /W4)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE omask_infer)

# onnxruntime.dll はプラグインの DLL と同じフォルダに置く（自分のフォルダから明示的に読み込む。PRC-08）。
# DirectML.dll は OS のものを使うので置かない（方式・構成設計書 付録 B）
install(FILES "${OMASK_ORT_DLL}" DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
# ライセンス（本体は GPL-2.0、同梱の ONNX Runtime は MIT と第三者の利用条件。非機能・運用設計書 §5.3）
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION "${CMAKE_PROJECT_NAME}" RENAME LICENSE.txt)
install(
  FILES "${OMASK_ORT_LICENSE}" "${OMASK_ORT_NOTICES}"
  DESTINATION "${CMAKE_PROJECT_NAME}/licenses/onnxruntime"
)
add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OMASK_ORT_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  VERBATIM
)

# ---- 検証用 CLI omask-eval（開発用で、公開するソースと配布物には入れない。手元にあるときだけ作る） ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/eval/main.cpp")
  add_executable(omask-eval tools/eval/main.cpp)
  target_link_libraries(omask-eval PRIVATE omask_infer)
  target_compile_definitions(omask-eval PRIVATE OMASK_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/models")
  target_compile_options(omask-eval PRIVATE /W4)
  add_custom_command(
    TARGET omask-eval
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OMASK_ORT_DLL}" "$<TARGET_FILE_DIR:omask-eval>"
    VERBATIM
  )
endif()

# ---- 単体テスト（doctest ＋ CTest。テスト設計書 §4.1） ----
option(OMASK_BUILD_TESTS "Build the unit tests (doctest + CTest)" ON)
if(OMASK_BUILD_TESTS)
  enable_testing()
  add_executable(omask_tests)
  target_sources(
    omask_tests
    PRIVATE
      tests/bench_core.cpp
      tests/test_analyzer.cpp
      tests/main.cpp
      tests/test_delay.cpp
      tests/test_frame.cpp
      tests/test_draw.cpp
      tests/test_fixed_policy.cpp
      tests/test_geometry.cpp
      tests/test_mask_planner.cpp
      tests/test_preprocess.cpp
      tests/test_result_band.cpp
      tests/test_screen_judge.cpp
      tests/test_settings.cpp
      tests/test_status.cpp
      tests/test_tag_decode.cpp
      tests/test_tag_adjust.cpp
      tests/test_tag_policy.cpp
      tests/test_text.cpp
      tests/test_timing.cpp
  )
  target_include_directories(omask_tests PRIVATE tests tests/third_party)
  target_link_libraries(omask_tests PRIVATE omask_core)
  target_compile_definitions(omask_tests PRIVATE OMASK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  target_compile_options(omask_tests PRIVATE /W4)
  add_test(NAME omask_tests COMMAND omask_tests)

  # 推論の結合テスト（テスト設計書 §4.2。onnxruntime.dll を実行ファイルの隣に写す）
  add_executable(omask_infer_tests)
  target_sources(omask_infer_tests PRIVATE tests/main.cpp tests/infer/test_ort_engine.cpp)
  target_include_directories(omask_infer_tests PRIVATE tests tests/third_party)
  target_link_libraries(omask_infer_tests PRIVATE omask_infer)
  target_compile_definitions(
    omask_infer_tests
    PRIVATE
      OMASK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
      OMASK_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/models"
  )
  target_compile_options(omask_infer_tests PRIVATE /W4)
  add_custom_command(
    TARGET omask_infer_tests
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OMASK_ORT_DLL}" "$<TARGET_FILE_DIR:omask_infer_tests>"
    VERBATIM
  )
  add_test(NAME omask_infer_tests COMMAND omask_infer_tests)
  # 推論ライブラリを一度も読み込めなかったプロセスでも、後片付けで落ちない（ほかのテストより先に、新しいプロセスで）
  add_test(NAME omask_infer_tests_no_runtime COMMAND omask_infer_tests "--test-case=TC-16 (b)*")
endif()

# ---- 整形の検査（clang-format --dry-run --Werror。方式・構成設計書 §4.3） ----
find_program(
  OMASK_CLANG_FORMAT
  NAMES clang-format
  PATHS
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/bin"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/bin"
)
if(OMASK_CLANG_FORMAT)
  file(
    GLOB_RECURSE _omask_format_files
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.[ch]pp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/infer/*.[ch]pp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.[ch]pp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/eval/*.[ch]pp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.[ch]pp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/infer/*.[ch]pp"
  )
  add_custom_target(
    omask_format_check
    COMMAND "${OMASK_CLANG_FORMAT}" --dry-run --Werror ${_omask_format_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "clang-format の検査"
    VERBATIM
  )
endif()

# ---- 命名の検査（clang-tidy の readability-identifier-naming。設定は .clang-tidy） ----
find_program(
  OMASK_CLANG_TIDY
  NAMES clang-tidy
  PATHS
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/bin"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/bin"
)
if(OMASK_CLANG_TIDY)
  file(
    GLOB_RECURSE _omask_tidy_files
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/infer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/eval/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/infer/*.cpp"
  )
  get_target_property(_omask_obs_includes OBS::libobs INTERFACE_INCLUDE_DIRECTORIES)
  set(_omask_obs_include_flags "")
  foreach(_dir IN LISTS _omask_obs_includes)
    list(APPEND _omask_obs_include_flags "-imsvc${_dir}")
  endforeach()
  add_custom_target(
    omask_naming_check
    COMMAND
      "${OMASK_CLANG_TIDY}" --quiet ${_omask_tidy_files} -- --driver-mode=cl -std:c++17 -EHsc
      "-I${CMAKE_CURRENT_SOURCE_DIR}/src" "-I${CMAKE_CURRENT_SOURCE_DIR}/tests"
      "-I${CMAKE_CURRENT_SOURCE_DIR}/tests/third_party" "-imsvc${OMASK_ORT_INCLUDE_DIR}"
      ${_omask_obs_include_flags} "-DOMASK_TEST_DATA_DIR=\"\"" "-DOMASK_MODELS_DIR=\"\""
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "clang-tidy の命名の検査"
    VERBATIM
  )
endif()
