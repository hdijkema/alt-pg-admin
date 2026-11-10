# Compile and embed catalogs so installed and portable builds need no loose QM files.
get_target_property(_pgadm_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(_pgadm_qt_bin "${_pgadm_qmake}" DIRECTORY)
find_program(PGADM_LRELEASE NAMES lrelease-qt6 lrelease6 lrelease
    HINTS "${_pgadm_qt_bin}" /usr/lib/qt6/bin REQUIRED)
set(PGADM_TRANSLATION_FILES)
foreach(language en de)
    set(catalog "${CMAKE_CURRENT_BINARY_DIR}/translations/pgadmin_${language}.qm")
    add_custom_command(OUTPUT "${catalog}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND "${PGADM_LRELEASE}" -nounfinished
            "${CMAKE_CURRENT_SOURCE_DIR}/translations/pgadmin_${language}.ts" -qm "${catalog}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/pgadmin_${language}.ts"
        VERBATIM)
    list(APPEND PGADM_TRANSLATION_FILES "${catalog}")
endforeach()
set(_pgadm_translation_qrc "<RCC><qresource prefix=\"/i18n\">")
foreach(catalog IN LISTS PGADM_TRANSLATION_FILES)
    get_filename_component(name "${catalog}" NAME)
    string(APPEND _pgadm_translation_qrc "<file alias=\"${name}\">${catalog}</file>")
endforeach()
string(APPEND _pgadm_translation_qrc "</qresource></RCC>")
set(PGADM_TRANSLATION_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
file(CONFIGURE OUTPUT "${PGADM_TRANSLATION_RESOURCE}" CONTENT "${_pgadm_translation_qrc}" @ONLY)
