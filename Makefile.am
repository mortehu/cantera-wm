bin_PROGRAMS = cantera-wm
noinst_PROGRAMS = focus-debug

ACLOCAL_AMFLAGS = -I m4

AM_CXXFLAGS = -Wall -g $(PACKAGES_CFLAGS)

cantera_wm_SOURCES = \
  arena.c arena.h \
  cantera-wm.h \
  main.cc \
  menu.cc \
  io.c io.h \
  session.cc \
  tree.c tree.h \
  window.cc \
  xa.cc xa.h
cantera_wm_LDADD = $(PACKAGES_LIBS)

focus_debug_SOURCES = focus-debug.c
focus_debug_LDADD = $(PACKAGES_LIBS)
