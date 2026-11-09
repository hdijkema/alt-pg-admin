cmake_minimum_required(VERSION 3.16)

project(pgadmin_qt VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Concurrent)
# QXlsx uses Qt's ZIP support. Declare the private target in the parent scope too.
if(QT_VERSION_MAJOR EQUAL 6 AND Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
set(PGADM_POSTGRESQL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/postgresql18/pgsql" CACHE PATH "PostgreSQL 18 SDK and runtime")
if(WIN32)
    if(NOT EXISTS "${PGADM_POSTGRESQL_ROOT}/include/libpq-fe.h")
        message(FATAL_ERROR "Download the PostgreSQL 18 libraries first: py -3 tools/fetch-postgresql.py")
    endif()
    set(PostgreSQL_INCLUDE_DIR "${PGADM_POSTGRESQL_ROOT}/include" CACHE PATH "PostgreSQL headers" FORCE)
    set(PostgreSQL_LIBRARY_RELEASE "${PGADM_POSTGRESQL_ROOT}/lib/libpq.lib" CACHE FILEPATH "PostgreSQL import library" FORCE)
endif()
set(PostgreSQL_ADDITIONAL_VERSIONS "18")
find_package(PostgreSQL 18 REQUIRED)

include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "Build dependency translations" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "Build dependency test application" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable upstream dependency tests; use PGADM_BUILD_TESTS" FORCE)
set(BUILD_WITH_QT5 OFF CACHE BOOL "Use Qt 6 for QtKeychain" FORCE)
FetchContent_Declare(qtkeychain
    URL https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.17.0.tar.gz
    URL_HASH SHA256=3b85c3929034b0a99da777130c34d99f006fcd3a9d56564159399a33fee0e504
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(qtkeychain)
FetchContent_Declare(qxlsx
    URL https://github.com/QtExcel/QXlsx/archive/refs/tags/v1.5.1.1.zip
    URL_HASH SHA256=5358ec4605e0084ab99e79c993e83364c94cf42cce079db579ac29a6e5e07d5f
    SOURCE_SUBDIR QXlsx
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(qxlsx)

set(PROJECT_SOURCES
        main.cpp
        pgadmmain.cpp
        pgadmmain.h
        profiledialog.cpp
        profiledialog.h
        pgtools.cpp
        pgtools.h
        databasejob.cpp
        databasejob.h
        databasetaskdialog.cpp
        databasetaskdialog.h
        rolesql.cpp
        rolesql.h
        rolemanager.cpp
        rolemanager.h
        pgadmmain.ui
        querytab.cpp
        querytab.h
        resultexporter.cpp
        resultexporter.h
        sqlhighlighter.cpp
        sqlhighlighter.h
        querylibrary.cpp
        querylibrary.h
        savedqueriespanel.cpp
        savedqueriespanel.h
        pgsession.cpp
        pgsession.h
        queryresult.h
        sqlscript.cpp
        sqlscript.h
        resultmodel.cpp
        resultmodel.h
        cellviewer.cpp
        cellviewer.h
        passwordstore.cpp
        passwordstore.h
        catalogservice.cpp
        catalogservice.h
        catalogtree.cpp
        catalogtree.h
        objectdetails.cpp
        objectdetails.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(pgadmin_qt
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET pgadmin_qt APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(pgadmin_qt SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(pgadmin_qt
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(pgadmin_qt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
target_link_libraries(pgadmin_qt PRIVATE PostgreSQL::PostgreSQL)
target_link_libraries(pgadmin_qt PRIVATE qt6keychain)
target_link_libraries(pgadmin_qt PRIVATE QXlsx::QXlsx Qt${QT_VERSION_MAJOR}::Concurrent)
add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${qxlsx_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:pgadmin_qt>/QXlsx-LICENSE.txt")
add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${qtkeychain_SOURCE_DIR}/COPYING" "$<TARGET_FILE_DIR:pgadmin_qt>/QtKeychain-COPYING.txt")
# Keep runtime dependencies next to both Qt Creator and standalone builds.
if(WIN32)
    get_filename_component(POSTGRESQL_ROOT "${PostgreSQL_INCLUDE_DIR}" DIRECTORY)
    foreach(runtime libpq.dll libssl-3-x64.dll libcrypto-3-x64.dll libintl-9.dll libiconv-2.dll libwinpthread-1.dll liblz4.dll libzstd.dll)
        if(EXISTS "${POSTGRESQL_ROOT}/bin/${runtime}")
            add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${POSTGRESQL_ROOT}/bin/${runtime}" "$<TARGET_FILE_DIR:pgadmin_qt>")
        endif()
    endforeach()
    foreach(tool pg_dump pg_restore psql createdb dropdb)
        add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${POSTGRESQL_ROOT}/bin/${tool}.exe" "$<TARGET_FILE_DIR:pgadmin_qt>")
    endforeach()
    foreach(notice server_license.txt commandlinetools_3rd_party_licenses.txt)
        if(EXISTS "${POSTGRESQL_ROOT}/${notice}")
            add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${POSTGRESQL_ROOT}/${notice}" "$<TARGET_FILE_DIR:pgadmin_qt>")
        endif()
    endforeach()
endif()
if(MSVC)
    target_compile_options(pgadmin_qt PRIVATE /utf-8 /W4)
endif()

option(PGADM_BUILD_TESTS "Build PostgreSQL connection tests" OFF)
if(PGADM_BUILD_TESTS)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Network)
    add_executable(tst_pgsession tests/tst_pgsession.cpp pgsession.cpp pgsession.h sqlscript.cpp resultmodel.cpp)
    target_include_directories(tst_pgsession PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_pgsession PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Widgets PostgreSQL::PostgreSQL)
    if(MSVC)
        target_compile_options(tst_pgsession PRIVATE /utf-8 /W4)
    endif()
    if(WIN32)
        add_custom_command(TARGET tst_pgsession POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>" "$<TARGET_FILE_DIR:tst_pgsession>")
    endif()
    add_test(NAME pgsession COMMAND tst_pgsession)
    add_executable(tst_queries tests/tst_queries.cpp pgsession.cpp pgsession.h sqlscript.cpp resultmodel.cpp resultmodel.h querytab.cpp querytab.h cellviewer.cpp cellviewer.h sqlhighlighter.cpp sqlhighlighter.h resultexporter.cpp resultexporter.h)
    target_include_directories(tst_queries PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_queries PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Widgets PostgreSQL::PostgreSQL)
    target_link_libraries(tst_queries PRIVATE QXlsx::QXlsx Qt${QT_VERSION_MAJOR}::Concurrent)
    if(MSVC)
        target_compile_options(tst_queries PRIVATE /utf-8 /W4)
    endif()
    if(WIN32)
        get_target_property(QT_CORE_DLL Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
        get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
        add_custom_command(TARGET tst_queries POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tst_queries>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll" "$<TARGET_FILE_DIR:tst_queries>/platforms")
    endif()
    add_test(NAME queries COMMAND tst_queries)
    set_tests_properties(queries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set(BROWSER_TEST_SOURCES ${PROJECT_SOURCES})
    list(REMOVE_ITEM BROWSER_TEST_SOURCES main.cpp pgadmmain.ui)
    add_executable(tst_browser tests/tst_browser.cpp ${BROWSER_TEST_SOURCES})
    target_include_directories(tst_browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_browser PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network PostgreSQL::PostgreSQL qt6keychain)
    target_link_libraries(tst_browser PRIVATE QXlsx::QXlsx Qt${QT_VERSION_MAJOR}::Concurrent)
    if(MSVC)
        target_compile_options(tst_browser PRIVATE /utf-8 /W4)
    endif()
    add_dependencies(tst_browser tst_queries)
    if(WIN32 AND EXISTS "${POSTGRESQL_ROOT}/bin/pg_dump.exe")
        foreach(runtime pg_dump.exe pg_restore.exe psql.exe createdb.exe dropdb.exe liblz4.dll libzstd.dll)
            add_custom_command(TARGET tst_browser POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${POSTGRESQL_ROOT}/bin/${runtime}" "$<TARGET_FILE_DIR:tst_browser>")
        endforeach()
    endif()
    add_test(NAME browser COMMAND tst_browser)
    set_tests_properties(browser PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.pgadmin_qt)
endif()
set_target_properties(pgadmin_qt PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS pgadmin_qt
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(pgadmin_qt)
endif()
