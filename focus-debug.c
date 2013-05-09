#include <stdio.h>
#include <stdlib.h>

#include <err.h>

#include <X11/Xlib.h>

int
main (int argc, char **argv)
{
  Display *display;
  Window focus;
  int revert_to;

  if (!(display = XOpenDisplay (NULL)))
    errx (EXIT_FAILURE, "XOpenDisplay failed");

  XGetInputFocus (display, &focus, &revert_to);

  fprintf (stderr, "Focus: %lx\n", focus);
  fprintf (stderr, "Revert to: %d\n", revert_to);

  if (!focus)
    {
      fprintf (stderr, "Focus was zero, moving to root window %lx\n",
               DefaultRootWindow (display));

      XSetInputFocus (display, DefaultRootWindow (display),
                      RevertToPointerRoot, CurrentTime);
    }

  XFlush (display);

  return EXIT_SUCCESS;
}
