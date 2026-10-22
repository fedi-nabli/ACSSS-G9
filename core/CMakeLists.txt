################################
## core                       ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 21 Oct 2026 ##
################################

# Pure algorithms and plain data: no threads, no I/O
add_subdirectory(src/types)

# Seam modules (interfaces fixed, implementations to come)
add_subdirectory(src/calibration)
add_subdirectory(src/fusion)
add_subdirectory(src/separation)
add_subdirectory(src/detect)
add_subdirectory(src/predict)


add_subdirectory(src/kalman)

mts_add_umbrella(core)

