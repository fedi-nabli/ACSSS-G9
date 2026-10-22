################################
## App CMake File            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 11 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Threads, resources, wiring. Modules first, then the executable
add_subdirectory(src/link)
add_subdirectory(src/hat)
add_subdirectory(src/ws)
add_subdirectory(src/protocol)
add_subdirectory(src/server)
add_subdirectory(src/processing)
add_subdirectory(src/orchestrator)

add_executable(mts_app main.cc)
target_compile_options(mts_app PRIVATE ${MTS_WARNING_FLAGS})
target_link_libraries(mts_app PRIVATE mts::app::orchestrator mts::utils::logger mts::utils::time Threads::Threads)

