################################
## App CMake File            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 11 Oct 2026          ##
## Last Modified: 15 Oct 2026 ##
################################

add_executable(mts_app main.cc)
target_compile_options(mts_app PRIVATE ${MTS_WARNING_FLAGS})
target_link_libraries(mts_app PRIVATE Threads::Threads)

