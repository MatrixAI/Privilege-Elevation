AC_INIT(file-descriptor-example, 0.0.1)
AC_CONFIG_SRCDIR(src/file-descriptor-client.c)
AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_LANG(C)
AC_CHECK_HEADER([sys/un.h], [], [AC_MSG_ERROR([<sys/un.h> is required.])])
AC_CHECK_HEADER([ftw.h],    [], [AC_MSG_ERROR([<ftw.h> is required.])])

AC_PROG_INSTALL

AC_PATH_PROG([PKEXEC_PATH], [pkexec])
if test -z "$PKEXEC_PATH"; then
    AC_MSG_ERROR([pkexec(1) is required.])
fi
AC_DEFINE_UNQUOTED([PKEXEC_PATH], ["$PKEXEC_PATH"])

dnl Alright.. so, I can't seem to get pkglibexecdir inside AC_SUBST
dnl Where is the variable...?
dnl the bottom doesn't work atm...

dnl AC_SUBST([MECHANISM_PATH], ["$pkglibexecdir/file-descriptor-mechanism"])
dnl AC_DEFINE([MECHANISM_PATH], ["$(pkglibexecdir)/file-descriptor-mechanism"])

AC_CONFIG_FILES([Makefile src/Makefile policy/Makefile policy/ai.matrix.pkexec.file-descriptor-example.policy])

AC_OUTPUT