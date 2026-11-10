set(PGADM_DESKTOP_ID io.github.hdijkema.AltPgAdmin)
install(FILES "packaging/linux/${PGADM_DESKTOP_ID}.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT Runtime)
install(FILES "packaging/linux/${PGADM_DESKTOP_ID}.svg"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT Runtime)
install(FILES "packaging/linux/${PGADM_DESKTOP_ID}.metainfo.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT Runtime)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/alt-pg-admin
    RENAME copyright COMPONENT Runtime)
install(FILES "${qtkeychain_SOURCE_DIR}/COPYING" DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/alt-pg-admin
    RENAME QtKeychain-COPYING.txt COMPONENT Runtime)
install(FILES "${libxlsxwriter_SOURCE_DIR}/License.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/alt-pg-admin
    RENAME libxlsxwriter-LICENSE.txt COMPONENT Runtime)
install(FILES "${zlib_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/alt-pg-admin
    RENAME zlib-LICENSE.txt COMPONENT Runtime)
install(FILES assets/icons/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/alt-pg-admin
    RENAME Lucide-LICENSE.txt COMPONENT Runtime)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME alt-pg-admin)
set(CPACK_PACKAGE_VENDOR "alt-pg-admin contributors")
set(CPACK_PACKAGE_CONTACT "alt-pg-admin maintainers <hdijkema@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native PostgreSQL administration application")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Manage connection profiles, browse database objects, execute SQL, export results
 to CSV or Excel, and manage roles, privileges, backups and maintenance tasks.
 The interface is available in Dutch, English and German.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hdijkema/alt-pg-admin")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME alt-pg-admin)
set(CPACK_DEBIAN_PACKAGE_SECTION database)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Plugins are loaded dynamically, so dpkg-shlibdeps cannot discover them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpq5 (>= 15), libsecret-1-0, libqt6svg6, qt6-qpa-plugins, qt6-translations-l10n, postgresql-client")
set(PGADM_DEBIAN_VERSION "" CACHE STRING "Debian distribution version for package filename")
if(PGADM_DEBIAN_VERSION)
    if(NOT PGADM_DEBIAN_VERSION MATCHES "^[0-9]+$")
        message(FATAL_ERROR "PGADM_DEBIAN_VERSION must be numeric")
    endif()
    execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE _pgadm_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(CPACK_DEBIAN_RUNTIME_FILE_NAME "alt-pg-admin_${PROJECT_VERSION}-debian${PGADM_DEBIAN_VERSION}_${_pgadm_arch}.deb")
else()
    set(CPACK_DEBIAN_RUNTIME_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
