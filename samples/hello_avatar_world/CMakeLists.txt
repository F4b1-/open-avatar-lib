# The hello_avatar_world sample: your avatar walking, running and jumping. Built by the library's CMakeLists.txt
# (build.sh); a title of your own does the same with add_subdirectory(<the library>) and xsdk_avatar.
add_executable(hello_avatar_world main.c)
target_compile_options(hello_avatar_world PRIVATE -std=gnu17 -O2 -Wall -Wextra -Werror)
target_link_libraries(hello_avatar_world PRIVATE xsdk_avatar m)
xsdk_add_xex(hello_avatar_world)
