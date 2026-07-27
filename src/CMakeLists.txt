# 查找必要的依赖包

# 设置编译选项
set(PRJ_COMPILE_OPTIONS "")

set(MIC_NUM 6)

# 收集源文件
file(GLOB_RECURSE SRC ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.c)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SRC})

# 设置 RPATH，运行时查找 lib/arm 下的共享库
set_target_properties(${PROJECT_NAME} PROPERTIES
  BUILD_RPATH "${CMAKE_SOURCE_DIR}/lib/arm"
  INSTALL_RPATH "$ORIGIN/../lib/arm"
)

# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../include
  ${CMAKE_CURRENT_LIST_DIR}/utils/jsoncpp
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_SOURCE_DIR}/sherpa-onnx/install/include
)

# 设置编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
  "-DMIC_NUM=${MIC_NUM}"
)

target_link_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/lib/arm
  ${CMAKE_SOURCE_DIR}/sherpa-onnx/install/lib
)

# 链接库
target_link_libraries(${PROJECT_NAME}
  udev
  portaudio
  asound
  curl
  avvtn_mic${MIC_NUM}
  sherpa-onnx-c-api
  sherpa-onnx-cxx-api
  sherpa-onnx-core
  piper_phonemize
  espeak-ng
  ucd
  sherpa-onnx-fst
  sherpa-onnx-fstfar
  sherpa-onnx-kaldifst-core
  kaldi-decoder-core
  kaldi-native-fbank-core
  kissfft-float
  ssentencepiece_core
  onnxruntime
  pthread
  dl
)

# 添加：在构建完成后复制可执行文件到根目录的 bin 目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}/bin/
    COMMENT "Copying executable to ${CMAKE_SOURCE_DIR}/bin"
)