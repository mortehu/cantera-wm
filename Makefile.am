bin_PROGRAMS = cantera-wm
noinst_PROGRAMS = focus-debug

ACLOCAL_AMFLAGS = -I m4

AM_CXXFLAGS = -Wall -g $(PACKAGES_CFLAGS)

cantera_wm_SOURCES = \
  cantera-wm.h \
  main.cc \
  menu.cc \
  arena.c arena.h \
  io.c io.h \
  tree.c tree.h
cantera_wm_LDADD = $(PACKAGES_LIBS)

focus_debug_SOURCES = focus-debug.c
focus_debug_LDADD = $(PACKAGES_LIBS)
