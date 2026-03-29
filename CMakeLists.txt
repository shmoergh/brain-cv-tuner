cmake_minimum_required(VERSION 3.22)

# Default board/platform. Can be overridden at configure time with:
#   -DPICO_BOARD=<board>
#   -DPICO_PLATFORM=<platform>
if(NOT DEFINED PICO_BOARD)
	set(PICO_BOARD pico2)
endif()

if(NOT DEFINED PICO_PLATFORM)
	set(PICO_PLATFORM rp2350-arm-s)
endif()

# Include Pico SDK (from brain-sdk subdirectory)
include(brain-sdk/pico_sdk_import.cmake)

project(brain-cv-tuner C CXX ASM)

# Reserve top-of-flash sectors for SDK-managed persistent storage when supported
# by the checked-out SDK revision.
set(BRAIN_SDK_STORAGE_RESERVE_CMAKE
	"${CMAKE_CURRENT_LIST_DIR}/brain-sdk/cmake/brain-storage-reserve-flash.cmake")
if(EXISTS "${BRAIN_SDK_STORAGE_RESERVE_CMAKE}")
	include("${BRAIN_SDK_STORAGE_RESERVE_CMAKE}")
	brain_storage_configure_flash_reservation()
else()
	message(WARNING "brain-storage flash reservation helper not found in current SDK revision; building without reserved storage sectors.")
endif()

# Initialize the Pico SDK
pico_sdk_init()

# Add brain-sdk which includes brain libraries
add_subdirectory(brain-sdk)

add_executable(brain-cv-tuner main.cpp)

target_link_libraries(brain-cv-tuner
	pico_stdlib
	brain-common
	brain-ui
	brain-io
  brain-utils)

pico_enable_stdio_usb(brain-cv-tuner 1)
pico_enable_stdio_uart(brain-cv-tuner 1)
pico_add_extra_outputs(brain-cv-tuner)
