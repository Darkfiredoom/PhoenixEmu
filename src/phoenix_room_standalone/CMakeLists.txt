# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(phoenix_room_standalone phoenix_room_standalone.cpp)

set_target_properties(phoenix_room_standalone PROPERTIES OUTPUT_NAME "eden-room")

target_link_libraries(phoenix_room_standalone PRIVATE phoenix-room)

if(UNIX AND NOT APPLE)
    install(TARGETS phoenix_room_standalone RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if (PHOENIX_STATIC_ROOM)
    target_link_options(phoenix_room_standalone PRIVATE "-static")
endif()

create_target_directory_groups(phoenix_room_standalone)
