# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# =============================================================================
# Unit tests. -DWITH_TESTS=1 builds them; -DWITH_NET_TESTS=1 adds the socket tests.
#
# Scope is deliberately narrow: the pieces that are pure functions over bytes and
# whose failure modes are invisible in ordinary use -- packet reassembly under
# fragmentation, and a zero-padding bug that shows about one login in 256.
#
# THE TWO SWITCHES ARE DISJOINT. The socket tests bind real listeners and cost more
# wall-clock than the whole rest of the suite, so WITH_TESTS never implies them. They
# are not run in CI. Ask for them by name when a change touches the reactor.
#
# No test framework is vendored; see TestHarness.h.
# =============================================================================

set(SRC_GRP_TESTS
    main.cpp
    TestHarness.h
    BigNumberTest.cpp
    Utf8Test.cpp
    ConfigTest.cpp
    ByteBufferTest.cpp
    SessionMailboxTest.cpp
    SessionProtocolPolicyTest.cpp
    DatabaseConcurrencyTest.cpp
    OpenSSLProviderTest.cpp
    ClientParserTest.cpp
    TerrainModelTest.cpp
    TileSerializerTest.cpp
    ModelMapTest.cpp
    NavBinningTest.cpp
    DynamicCollisionTest.cpp
    PlacementTest.cpp
    GeometryMathTest.cpp
    DataIntegrityTest.cpp
    # Compiled in, not linked from `game`: game.lib pulls the whole server, down to the
    # database globals that only mangosd defines. These three know nothing of it.
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers/DynamicCollision.cpp
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers/GameObjectModel.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Server/SessionMailbox.cpp
    ByteBufferStressTest.cpp
    CryptoStressTest.cpp
    # AuthCryptTest is deliberately absent. 2.4.3's packet crypt is a
    # different scheme -- a fixed-length header crypt, four bytes out and six
    # in, with no HMAC-seeded RC4 pair and no continuous keystream -- so the
    # round-trip and keystream assertions describe 3.3.5a and later, not this
    # wire format. CryptoStressTest holds what every expansion shares.
    CodecStressTest.cpp
    PacketCodecTest.cpp
    # The movement plan's arithmetic. Compiled in rather than linked from `game` for
    # the same reason as the three above: a Course is a value over points and a clock,
    # so it owes the server nothing, and the whole point of extracting it was that its
    # timing could be asserted against the retail captures without standing a world up.
    CourseTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Course.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/ClientClock.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/CourseSync.cpp
)

# The routing decisions -- Route's states, MoveProfile's permissions, the baked tile
# header. They need Detour's headers and, above all, its DT_POLYREF64 definition: the
# header assertion is that the tile records the polyref width, and it means nothing
# unless the test compiles against the same width the server does.
#
# Detour is only configured when the server or the tools are being built (see
# dep/CMakeLists.txt), so this follows the target rather than forcing it. A tests-only
# tree still configures; it just does not carry these cases, and every CI job builds
# with BUILD_MANGOSD=1, so they run where it matters.
if(TARGET RecastNavigation::Detour)
    list(APPEND SRC_GRP_TESTS RouteTest.cpp)
endif()

# The socket tests. Off unless asked for -- see the note above.
if(WITH_NET_TESTS)
    list(APPEND SRC_GRP_TESTS GracefulCloseTest.cpp)
    list(APPEND SRC_GRP_TESTS NetworkStressTest.cpp)
endif()

source_group("tests" FILES ${SRC_GRP_TESTS})

add_executable(mangos_tests ${SRC_GRP_TESTS})

target_include_directories(mangos_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
        ${CMAKE_SOURCE_DIR}/src/game/MotionGenerators
        ${CMAKE_SOURCE_DIR}/src/game/motion
        ${CMAKE_SOURCE_DIR}/src/game/Server)

target_link_libraries(mangos_tests
    PRIVATE
        proto
        shared
        dataintegrity
        extractor_client
        extractor_data
        Threads::Threads
        mangos_openssl_strict
)

# Headers and DT_POLYREF64, not code. Linking the target rather than reaching for its
# include directory is the point: the definition is PUBLIC on it, so this is what keeps
# the test's idea of sizeof(dtPolyRef) identical to the server's. Reaching for the
# directory alone would let the test agree with a server it in fact disagrees with.
if(TARGET RecastNavigation::Detour)
    target_link_libraries(mangos_tests PRIVATE RecastNavigation::Detour)
endif()

add_test(NAME mangos_tests COMMAND mangos_tests)

add_test(NAME proto_boundary
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckProtoBoundary.cmake)

add_test(NAME build_policy
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckBuildPolicy.cmake)

add_test(NAME spatial_boundary
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckSpatialBoundary.cmake)

add_test(NAME movement_state_seed
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckMovementStateSeed.cmake)

# Put the runtime DLLs beside the test binary so it can be run from the build tree.
# A convenience, never a gate: the search is derived from the library that was actually
# found, and a miss warns rather than failing the configure.
if(WIN32)
    get_filename_component(MANGOS_SSL_LIB_DIR "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
    get_filename_component(MANGOS_SSL_ROOT "${MANGOS_SSL_LIB_DIR}" DIRECTORY)

    find_file(MANGOS_TEST_OPENSSL_CRYPTO_DLL
        NAMES libcrypto-3-x64.dll libcrypto-3.dll libcrypto.dll
        HINTS "${MANGOS_SSL_ROOT}/bin" "${MANGOS_SSL_LIB_DIR}" "${OPENSSL_ROOT_DIR}/bin")

    if(MANGOS_TEST_OPENSSL_CRYPTO_DLL)
        add_custom_command(TARGET mangos_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MANGOS_TEST_OPENSSL_CRYPTO_DLL}"
                "$<TARGET_FILE_DIR:mangos_tests>")
    else()
        message(WARNING
            "OpenSSL runtime DLL not found; mangos_tests may need it on PATH to run.")
    endif()

    get_filename_component(
        MANGOS_TEST_MYSQL_DLL_DIR "${MySQL_LIBRARY}" DIRECTORY)
    get_filename_component(
        MANGOS_TEST_MYSQL_DLL_NAME "${MySQL_LIBRARY}" NAME)
    string(REPLACE ".lib" ".dll" MANGOS_TEST_MYSQL_DLL_NAME
        "${MANGOS_TEST_MYSQL_DLL_NAME}")
    if(EXISTS "${MANGOS_TEST_MYSQL_DLL_DIR}/${MANGOS_TEST_MYSQL_DLL_NAME}")
        add_custom_command(TARGET mangos_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MANGOS_TEST_MYSQL_DLL_DIR}/${MANGOS_TEST_MYSQL_DLL_NAME}"
                "$<TARGET_FILE_DIR:mangos_tests>")
    endif()
    set_tests_properties(mangos_tests PROPERTIES
        ENVIRONMENT "OPENSSL_MODULES=")

    # Match the release layout and leave OPENSSL_MODULES empty: provider discovery
    # must work from ossl-modules beside the executable, not from the build host.
    find_file(MANGOS_TEST_OPENSSL_LEGACY_DLL
        NAMES legacy.dll
        HINTS "${MANGOS_SSL_ROOT}" "${OPENSSL_ROOT_DIR}"
        PATH_SUFFIXES bin/ossl-modules bin ossl-modules lib/ossl-modules ""
        NO_DEFAULT_PATH)

    if(MANGOS_TEST_OPENSSL_LEGACY_DLL)
        add_custom_command(TARGET mangos_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:mangos_tests>/ossl-modules"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MANGOS_TEST_OPENSSL_LEGACY_DLL}"
                "$<TARGET_FILE_DIR:mangos_tests>/ossl-modules/legacy.dll")
    else()
        message(WARNING
            "OpenSSL legacy provider (legacy.dll) not found; mangos_tests may fail its RC4 cases.")
    endif()
endif()
