AC_INIT(cantera-wm,30)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET

PKG_CHECK_MODULES([PACKAGES], [x11 xcomposite xdamage xfixes xinerama xrender])

AC_SUBST(PACKAGES_CFLAGS)
AC_SUBST(PACKAGES_LIBS)

AC_LANG_PUSH([C++])
AX_CXX_COMPILE_STDCXX_11([noext])
AC_LANG_POP([C++])

AC_OUTPUT(Makefile)
