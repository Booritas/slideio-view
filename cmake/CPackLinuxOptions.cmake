# CPACK_PACKAGING_INSTALL_PREFIX is global, but the two Linux generators need
# different prefixes: the .deb places its payload at an absolute system path,
# while the .tar.gz has to stay relocatable. CPack re-evaluates this file once
# per generator, which is the mechanism provided for exactly that.

if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/slideio-viewer")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "slideio-viewer")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Booritas/slideio")

    # Off deliberately. With it on, dpkg scans the bundled Qt libraries and adds
    # dependencies on the distribution's own Qt packages -- the opposite of
    # bundling, and a package that then pulls in a second, conflicting Qt.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

    # What Qt still needs from the system even with Qt bundled: the xcb platform
    # plugin's own dependencies. The authoritative list is derived by running ldd
    # over the packaged libqxcb.so on the runner -- see the release workflow's
    # container step, which prints it. This is the starting point, not a guess to
    # be trusted; the smoke test is what certifies it.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgl1, libglx-mesa0, libxkbcommon0, libxkbcommon-x11-0, \
libfontconfig1, libfreetype6, libdbus-1-3, libxcb1, libxcb-cursor0, libxcb-icccm4, \
libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, \
libxcb-sync1, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libx11-xcb1")

    # The /usr/bin symlink, the desktop entry and the icon. The payload itself
    # stays in /opt so the bundled libraries and plugins travel with it; the
    # maintainer scripts link the three paths that have to appear in /usr.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_LIST_DIR}/debian/postinst;${CMAKE_CURRENT_LIST_DIR}/debian/prerm")
    # Without this, the control scripts enter the archive with whatever mode the
    # checkout gave them. That mode was already wrong once, and dpkg only
    # notices at configure time -- long after the package looks fine.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
else()
    # Relocatable: bin/, lib/ and plugins/ at the root of the tarball.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
endif()
