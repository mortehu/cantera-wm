bin_PROGRAMS = cantera-wm

AM_CXXFLAGS = -std=c++11 -Wall -g $(PACKAGES_CFLAGS)

cantera_wm_SOURCES = \
  cantera-wm.h \
  main.cc \
  menu.cc \
  arena.c arena.h \
  io.c io.h \
  tree.c tree.h
cantera_wm_LDADD = $(PACKAGES_LIBS)
