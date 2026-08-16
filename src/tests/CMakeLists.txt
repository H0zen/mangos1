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
    ClientParserTest.cpp
    TerrainModelTest.cpp
    TileSerializerTest.cpp
    ModelMapTest.cpp
    DynamicCollisionTest.cpp
    PlacementTest.cpp
    GeometryMathTest.cpp
    DataIntegrityTest.cpp
    CombatCoreTest.cpp
    CombatReactionTest.cpp
    ProcPointsTest.cpp
    # A unit's stats, assembled without a unit. StatBlock depends on nothing,
    # which is what lets the formula every stat in the game goes through be
    # asserted with written-down numbers.
    StatBlockTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Object/StatBlock.cpp
    # The same bargain for the other two components held by value on Unit: the
    # resource tick and the movement rates depend on nothing either, so both
    # come in as sources rather than through game.lib.
    PowerPoolTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Object/PowerPool.cpp
    SpeedSetTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Object/SpeedSet.cpp
    # The first aura rule with a test. Linked from aura_pure rather than
    # compiled in, like combat_pure, because it is a library of its own.
    AuraSlotTest.cpp
    # Compiled in, not linked from `game`: game.lib pulls the whole server, down to the
    # database globals that only mangosd defines. These three know nothing of it.
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers/DynamicCollision.cpp
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers/GameObjectModel.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Server/SessionMailbox.cpp
    # Same bargain for the reaction queue: it names an ObjectGuid on each end
    # and nothing else from the server, so it comes in directly rather than
    # dragging game.lib behind it.
    #
    # ObjectGuid.cpp is deliberately NOT here. Everything the queue and these
    # tests use -- the constructors and the comparisons -- is inline in the
    # header, and the translation unit reaches Object.h and from there the
    # whole world. Compiling it in for two operators would undo the point.
    ${CMAKE_SOURCE_DIR}/src/game/combat/ReactionQueue.cpp
    ByteBufferStressTest.cpp
    CryptoStressTest.cpp
    # Re-keying, kept out of CryptoStressTest so that file stays the untouched proof
    # that the in-tree RC4 reproduces the published vector.
    Arc4RekeyTest.cpp
    # AuthCryptTest is deliberately absent. 2.4.3's packet crypt is a
    # different scheme -- a fixed-length header crypt, four bytes out and six
    # in, with no HMAC-seeded RC4 pair and no continuous keystream -- so the
    # round-trip and keystream assertions describe 3.3.5a and later, not this
    # wire format. CryptoStressTest holds what every expansion shares.
    CodecStressTest.cpp
    PacketCodecTest.cpp
    # MAI's lowering, run over a real world's db_scripts export. Compiled in
    # rather than linked from `game` for the same reason the three above are:
    # game.lib pulls the whole server down to globals only mangosd defines,
    # and this file needs none of it.
    MaiLoweringTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Scripting/mai/MaiLowering.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Scripting/mai/MaiParse.cpp
    # The block compiler. Here for the same reason and with the same
    # property: it turns `if`/`repeat`/`while` rows into jumps knowing
    # nothing about a world, so what it decides can be asserted without one.
    ${CMAKE_SOURCE_DIR}/src/game/Scripting/mai/MaiCompile.cpp
    # The movement plan's arithmetic. Compiled in rather than linked from `game` for
    # the same reason as the three above: a Course is a value over points and a clock,
    # so it owes the server nothing, and the whole point of extracting it was that its
    # timing could be asserted against the retail captures without standing a world up.
    CourseTest.cpp
    RosterTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Course.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/ClientClock.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/CourseSync.cpp
    # The four values a movement is made of -- where, how it bends, how fast, and where
    # WHEN. None of them refers to a Unit, a Map, a packet or a running clock, so all
    # four compile into a test binary with nothing else present. That is the property
    # they were designed for: the client's duration accumulator is four lines of
    # arithmetic that decides where every mover in the world is, and until it could be
    # built out of plain numbers nobody had ever asserted on it.
    MotionLayerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Path.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Pace.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Curve.cpp
    ${CMAKE_SOURCE_DIR}/src/game/motion/Motion.cpp
)

# The navigation: the grid's binning, a baked tile's round trip through its file, the
# joining of two tiles, and the routing decisions over them. These link `nav` and
# nothing of the server -- that is the whole reason the profile is a value and the
# router takes a store rather than a Map.
list(APPEND SRC_GRP_TESTS RouteTest.cpp NavGridTest.cpp NavTileTest.cpp
                          RouterLinkTest.cpp NavBuilderTest.cpp
                          NavPolygonTest.cpp NavMeshTest.cpp PolyanyaTest.cpp
                          MedialAxisTest.cpp ReebGraphTest.cpp)

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
        ${CMAKE_SOURCE_DIR}/src/game/Server
        ${CMAKE_SOURCE_DIR}/src/game/Object
        ${CMAKE_SOURCE_DIR}/src/game/Scripting)

target_link_libraries(mangos_tests
    PRIVATE
        proto
        shared
        combat_pure
        aura_pure
        nav
        dataintegrity
        extractor_client
        extractor_data
        Threads::Threads
        mangos_openssl_strict
)

add_test(NAME mangos_tests COMMAND mangos_tests)

# The fixtures a test reads are found by this rather than by guessing at the
# working directory ctest happens to use.
# APPEND, not PROPERTIES: the Windows block below sets ENVIRONMENT too, and a
# plain set would have silently thrown one of the two away.
set_property(TEST mangos_tests APPEND PROPERTY
    ENVIRONMENT "MANGOS_TEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data")

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

add_test(NAME pure_boundary
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckPureBoundary.cmake)

add_test(NAME script_events
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckScriptEvents.cmake)

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
    # No ossl-modules here, and no OPENSSL_MODULES to point at one. RC4 was the only
    # thing this tree ever asked the legacy provider for, and it is in the tree now, so
    # the tests need libcrypto and nothing beside it.
endif()
