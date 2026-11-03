# ONNX Runtime（DirectML 版）の取得（方式・構成設計書 §6、非機能・運用設計書 §2.5）。
# 公式の配布元（NuGet）から版とハッシュを固定して取り、plugin/.deps に展開する。DirectML.dll は同梱せず OS のものを使う。

include_guard(GLOBAL)

set(OMASK_ORT_VERSION "1.24.4")
set(OMASK_ORT_SHA256 "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")
set(
  OMASK_ORT_URL
  "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/${OMASK_ORT_VERSION}/microsoft.ml.onnxruntime.directml.${OMASK_ORT_VERSION}.nupkg"
)
set(OMASK_ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime-directml-${OMASK_ORT_VERSION}")

if(NOT EXISTS "${OMASK_ORT_DIR}/build/native/include/onnxruntime_c_api.h")
  message(STATUS "Download ONNX Runtime DirectML ${OMASK_ORT_VERSION}")
  set(_omask_ort_pkg "${OMASK_ORT_DIR}.nupkg")
  file(DOWNLOAD "${OMASK_ORT_URL}" "${_omask_ort_pkg}" EXPECTED_HASH SHA256=${OMASK_ORT_SHA256} TLS_VERIFY ON)
  file(ARCHIVE_EXTRACT INPUT "${_omask_ort_pkg}" DESTINATION "${OMASK_ORT_DIR}")
  file(REMOVE "${_omask_ort_pkg}")
endif()

set(OMASK_ORT_INCLUDE_DIR "${OMASK_ORT_DIR}/build/native/include")
set(OMASK_ORT_DLL "${OMASK_ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll")
set(OMASK_ORT_LICENSE "${OMASK_ORT_DIR}/LICENSE")
set(OMASK_ORT_NOTICES "${OMASK_ORT_DIR}/ThirdPartyNotices.txt")
