# The hello sample: one avatar, in C++ on the C API, built by the library's CMakeLists.txt (build.sh).
add_executable(hello main.cpp)
target_compile_options(hello PRIVATE -std=c++23 -O2 -Wall -Wextra -Werror)
target_link_libraries(hello PRIVATE xsdk_avatar)
xsdk_add_xex(hello)
