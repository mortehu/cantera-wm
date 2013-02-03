AC_INIT(cantera-wm,30)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_CHECK_HEADER([X11/extensions/Xdamage.h], [], [AC_MSG_ERROR([libxdamage required (apt-get install libxdamage-dev)])])
AC_CHECK_HEADER([X11/extensions/Xrender.h], [], [AC_MSG_ERROR([libxrender required (apt-get install libxrender-dev)])])
AC_CHECK_HEADER([X11/extensions/Xinerama.h], [], [AC_MSG_ERROR([libxinerama required (apt-get install libxinerama-dev)])])

AC_OUTPUT(Makefile)
