AC_INIT([6fs],[0.0],[marlam@marlam.de],[6fs],[https://marlam.de/6fs])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CANONICAL_TARGET
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AM_INIT_AUTOMAKE([1.16.4 silent-rules no-texinfo.tex -Wall])
AM_SILENT_RULES([yes])
AC_PROG_CXX
AC_PROG_INSTALL
AC_LANG([C++])

PKG_PROG_PKG_CONFIG([])
if test -z "$PKG_CONFIG"; then
    AC_MSG_ERROR([pkg-config not found])
fi
PKG_CHECK_MODULES([fuse3], [fuse3 >= 3.14], [HAVE_FUSE3=1], [HAVE_FUSE3=0])
if test "$HAVE_FUSE3" != "1"; then
    AC_MSG_ERROR([libfuse3 >= 3.14 not found])
fi
PKG_CHECK_MODULES([libsodium], [libsodium >= 1.0], [HAVE_LIBSODIUM=1], [HAVE_LIBSODIUM=0])
if test "$HAVE_LIBSODIUM" != "1"; then
    AC_MSG_ERROR([libsodium >= 1.0 not found])
fi

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
