AC_INIT(privilege-elevation, 0.0.1)
AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_LANG(C)
AC_CHECK_HEADER([sys/un.h], [], [AC_MSG_ERROR([<sys/un.h> is required.])])
AC_CHECK_HEADER([ftw.h],    [], [AC_MSG_ERROR([<ftw.h> is required.])])
AC_CHECK_HEADER([math.h],    [], [AC_MSG_ERROR([<math.h> is required.])])

AC_PROG_INSTALL
AC_PROG_RANLIB
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

dnl Substitute the PKEXEC_PATH
AC_PATH_PROG([PKEXEC_PATH], [pkexec])
if test -z "$PKEXEC_PATH"; then
    AC_MSG_ERROR([pkexec(1) is required.])
fi

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
