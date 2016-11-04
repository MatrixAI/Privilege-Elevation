AC_INIT(file-descriptor-example, 0.0.1)
AC_CONFIG_SRCDIR(src/file-descriptor-client.c)
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

dnl Substitute the MECHANISM_NAME (which will be combined with `$(pkglibexecdir)` in the Makefile)
AC_SUBST([MECHANISM_NAME], ["file-descriptor-mechanism"])

AC_CONFIG_FILES([Makefile src/Makefile policy/Makefile])

AC_OUTPUT