add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/scene_viewer)
add_executable(test_main test_main.cpp)
target_link_libraries(test_main glm::glm)