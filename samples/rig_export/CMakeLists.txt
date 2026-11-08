# The rig export: rigs to animate, written beside the .xex from the console's own files, its progress on screen.
# Built by the library's CMakeLists.txt (build.sh).
add_executable(rig_export main.c screen.c)
target_compile_options(rig_export PRIVATE -std=gnu17 -O2 -Wall -Wextra -Werror)
target_link_libraries(rig_export PRIVATE xsdk_avatar)
xsdk_add_xex(rig_export)
