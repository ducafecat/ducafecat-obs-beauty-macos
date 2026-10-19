# Included by the downloaded official plugin template after its target exists.
enable_language(OBJCXX)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/face-mask.c src/face-tracker.mm)
set_source_files_properties(src/face-tracker.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
foreach(framework Vision Foundation CoreVideo CoreGraphics)
  find_library(BEAUTY_${framework}_FRAMEWORK ${framework} REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${BEAUTY_${framework}_FRAMEWORK})
endforeach()
