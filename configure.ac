AC_INIT(privilege-elevation, 0.0.1)
AC_CONFIG_SRCDIR(src/privilege-elevation.c)
AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_LANG(C)
AC_CHECK_HEADER([sys/un.h], [], [AC_MSG_ERROR([<sys/un.h> is required.])])
AC_CHECK_HEADER([ftw.h],    [], [AC_MSG_ERROR([<ftw.h> is required.])])

AC_PROG_INSTALL

dnl Substitute the PKEXEC_PATH
AC_PATH_PROG([PKEXEC_PATH], [pkexec])
if test -z "$PKEXEC_PATH"; then
    AC_MSG_ERROR([pkexec(1) is required.])
fi

AC_CONFIG_FILES([Makefile src/Makefile policy/Makefile argparse/GNUMakefile])

AC_OUTPUT