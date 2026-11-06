# The viewer sample: several avatars in one scene, built by the library's CMakeLists.txt (build.sh).
add_executable(viewer main.c screenshot.c)
target_compile_options(viewer PRIVATE -std=gnu17 -O2 -Wall -Wextra -Werror)
target_link_libraries(viewer PRIVATE xsdk_avatar m)
xsdk_add_xex(viewer)
