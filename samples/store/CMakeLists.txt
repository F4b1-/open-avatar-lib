# The store sample: the Avatar Thrift Store, its NPCs and the emote wheel. Built by the library's CMakeLists.txt (build.sh); its own shaders
# come from store_shaders.py, with the library's generator.
set(store_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
	OUTPUT "${store_generated}/store_shaders.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${store_generated}"
	COMMAND sh -c "python3 '${CMAKE_CURRENT_SOURCE_DIR}/store_shaders.py' '${XSDK_ROOT}' \
'${CMAKE_CURRENT_BINARY_DIR}/shader_work' > '${store_generated}/store_shaders.h.new'"
	COMMAND ${CMAKE_COMMAND} -E rename "${store_generated}/store_shaders.h.new" "${store_generated}/store_shaders.h"
	DEPENDS store_shaders.py "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_shaders.py"
	VERBATIM)
add_executable(store main.c locomotion.c follow_camera.c bands.c emotes.c mesh.c font.c scene.c npcs.c wheel.c
	../viewer/screenshot.c "${store_generated}/store_shaders.h")
target_include_directories(store PRIVATE ../viewer "${store_generated}")
target_compile_options(store PRIVATE -std=gnu17 -O2 -Wall -Wextra -Werror)
target_link_libraries(store PRIVATE xsdk_avatar m)
xsdk_add_xex(store)
