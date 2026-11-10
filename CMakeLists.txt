cmake_minimum_required(VERSION 3.24)

set(PGADM_RELEASE_VERSION "" CACHE STRING "Release version (major.minor.patch); empty uses the development version")
set(PGADM_VERSION "0.1.0")
if(NOT PGADM_RELEASE_VERSION STREQUAL "")
    if(NOT PGADM_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
        message(FATAL_ERROR "PGADM_RELEASE_VERSION must have the form major.minor.patch")
    endif()
    set(PGADM_VERSION "${PGADM_RELEASE_VERSION}")
endif()
project(pgadmin_qt VERSION ${PGADM_VERSION} LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Concurrent)
set(PGADM_POSTGRESQL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/postgresql18/pgsql" CACHE PATH "PostgreSQL 18 SDK and runtime")
if(WIN32)
    if(NOT EXISTS "${PGADM_POSTGRESQL_ROOT}/include/libpq-fe.h")
        message(FATAL_ERROR "Download the PostgreSQL 18 libraries first: py -3 tools/fetch-postgresql.py")
    endif()
    set(PostgreSQL_INCLUDE_DIR "${PGADM_POSTGRESQL_ROOT}/include" CACHE PATH "PostgreSQL headers" FORCE)
    set(PostgreSQL_LIBRARY_RELEASE "${PGADM_POSTGRESQL_ROOT}/lib/libpq.lib" CACHE FILEPATH "PostgreSQL import library" FORCE)
endif()
set(PostgreSQL_ADDITIONAL_VERSIONS "18")
find_package(PostgreSQL 15 REQUIRED)

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
# libxlsxwriter and its sole external dependency are pinned and built statically.
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ZLIB
    URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
    URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(ZLIB)
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_NO_MD5 ON CACHE BOOL "" FORCE)
set(USE_OPENSSL_MD5 OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_MINIZIP OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libxlsxwriter
    URL https://github.com/jmcnamara/libxlsxwriter/archive/refs/tags/v1.2.4.tar.gz
    URL_HASH SHA256=e0db59fc248a5ffa465a05ea83a9d466d4bca0e53ab42771515d4ebb467a41c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libxlsxwriter)
add_library(qtxlsxwriter STATIC src/results/qtxlsxwriter.cpp src/results/qtxlsxwriter.h)
target_include_directories(qtxlsxwriter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qtxlsxwriter PUBLIC Qt${QT_VERSION_MAJOR}::Core PRIVATE xlsxwriter)
if(MSVC)
    target_compile_options(qtxlsxwriter PRIVATE /utf-8 /W4)
endif()

include(cmake/Translations.cmake)

set(PROJECT_SOURCES
        ${PGADM_TRANSLATION_RESOURCE}
        assets/icons.qrc
        src/app/main.cpp
        src/app/pgadmmain.cpp
        src/app/pgadmmain.h
        src/connections/profiledialog.cpp
        src/connections/profiledialog.h
        src/database/pgtools.cpp
        src/database/pgtools.h
        src/administration/databasejob.cpp
        src/administration/databasejob.h
        src/administration/databasetaskdialog.cpp
        src/administration/databasetaskdialog.h
        src/administration/rolesql.cpp
        src/administration/rolesql.h
        src/administration/rolemanager.cpp
        src/administration/rolemanager.h
        src/app/pgadmmain.ui
        src/query/querytab.cpp
        src/app/editorstate.cpp
        src/query/querytab.h
        src/results/resultexporter.cpp
        src/results/resultexporter.h
        src/query/sqlhighlighter.cpp
        src/query/sqlhighlighter.h
        src/query/sqleditor.cpp
        src/query/sqleditor.h
        src/query/querylibrary.cpp
        src/query/querylibrary.h
        src/query/savedqueriespanel.cpp
        src/query/savedqueriespanel.h
        src/results/fullresultexport.cpp
        src/results/fullresultexport.h
        src/database/pgsession.cpp
        src/database/pgsession.h
        src/results/queryresult.h
        src/database/sqlscript.cpp
        src/database/sqlscript.h
        src/results/resultmodel.cpp
        src/results/resultmodel.h
        src/results/cellviewer.cpp
        src/results/cellviewer.h
        src/connections/passwordstore.cpp
        src/connections/passwordstore.h
        src/database/catalogservice.cpp
        src/database/catalogservice.h
        src/browser/catalogtree.cpp
        src/browser/catalogtree.h
        src/browser/objectdetails.cpp
        src/browser/objectdetails.h
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
target_include_directories(pgadmin_qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(pgadmin_qt PRIVATE PGADM_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(pgadmin_qt PRIVATE PostgreSQL::PostgreSQL)
target_link_libraries(pgadmin_qt PRIVATE qt6keychain)
target_link_libraries(pgadmin_qt PRIVATE qtxlsxwriter Qt${QT_VERSION_MAJOR}::Concurrent)
add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${libxlsxwriter_SOURCE_DIR}/License.txt" "$<TARGET_FILE_DIR:pgadmin_qt>/libxlsxwriter-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${zlib_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:pgadmin_qt>/zlib-LICENSE.txt")
add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${qtkeychain_SOURCE_DIR}/COPYING" "$<TARGET_FILE_DIR:pgadmin_qt>/QtKeychain-COPYING.txt")
add_custom_command(TARGET pgadmin_qt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/LICENSE.txt" "$<TARGET_FILE_DIR:pgadmin_qt>/Lucide-LICENSE.txt")
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
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Network)
    add_executable(tst_pgsession tests/tst_pgsession.cpp src/results/fullresultexport.cpp src/database/pgsession.cpp src/database/pgsession.h src/database/sqlscript.cpp src/results/resultmodel.cpp)
    target_include_directories(tst_pgsession PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_pgsession PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Widgets PostgreSQL::PostgreSQL)
    target_link_libraries(tst_pgsession PRIVATE qtxlsxwriter Qt${QT_VERSION_MAJOR}::Concurrent)
    if(MSVC)
        target_compile_options(tst_pgsession PRIVATE /utf-8 /W4)
    endif()
    if(WIN32)
        add_custom_command(TARGET tst_pgsession POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>" "$<TARGET_FILE_DIR:tst_pgsession>")
    endif()
    add_test(NAME pgsession COMMAND tst_pgsession)
    add_executable(tst_queries tests/tst_queries.cpp src/results/fullresultexport.cpp src/database/pgsession.cpp src/database/pgsession.h src/database/sqlscript.cpp src/results/resultmodel.cpp src/results/resultmodel.h src/query/querytab.cpp src/query/querytab.h src/results/cellviewer.cpp src/results/cellviewer.h src/query/sqlhighlighter.cpp src/query/sqlhighlighter.h src/query/sqleditor.cpp src/query/sqleditor.h src/results/resultexporter.cpp src/results/resultexporter.h)
    target_compile_definitions(tst_queries PRIVATE PGADM_TEST_PYTHON="${Python3_EXECUTABLE}")
    target_include_directories(tst_queries PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_queries PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Widgets PostgreSQL::PostgreSQL)
    target_link_libraries(tst_queries PRIVATE qtxlsxwriter Qt${QT_VERSION_MAJOR}::Concurrent)
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
    list(REMOVE_ITEM BROWSER_TEST_SOURCES src/app/main.cpp src/app/pgadmmain.ui)
    add_executable(tst_browser tests/tst_browser.cpp ${BROWSER_TEST_SOURCES})
    target_include_directories(tst_browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_browser PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network PostgreSQL::PostgreSQL qt6keychain)
    target_link_libraries(tst_browser PRIVATE qtxlsxwriter Qt${QT_VERSION_MAJOR}::Concurrent)
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
    add_test(NAME translations_ui COMMAND tst_browser translations)
    set_tests_properties(translations_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME translations_catalog COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-translations.py")
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
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
if(WIN32)
    enable_language(RC)
    configure_file(assets/app-icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/alt-pg-admin.ico")
    target_sources(pgadmin_qt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
elseif(APPLE)
    target_link_options(pgadmin_qt PRIVATE "-Wl,-headerpad_max_install_names")
    target_sources(pgadmin_qt PRIVATE assets/icons/alt-pg-admin.icns)
    set_source_files_properties(assets/icons/alt-pg-admin.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(pgadmin_qt PROPERTIES
        MACOSX_BUNDLE_ICON_FILE alt-pg-admin.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.hdijkema.AltPgAdmin
        MACOSX_BUNDLE_BUNDLE_NAME alt-pg-admin)
endif()
install(TARGETS pgadmin_qt
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(pgadmin_qt)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/LinuxPackaging.cmake)
endif()
