#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include <err.h>
#include <signal.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>

#include "cantera-wm.h"
#include "tree.h"

namespace cantera_wm
{
  Display *x_display;
  int x_screen_index;
  Screen* x_screen;
  Visual* x_visual;
  XRenderPictFormat* x_render_visual_format;
  XVisualInfo* x_visual_info;
  int x_visual_info_count;
  Window x_root_window;

  XIM x_im;
  XIC x_ic;
  int x_damage_eventbase;
  int x_damage_errorbase;
}

using namespace cantera_wm;

namespace
{

  int (*x_default_error_handler)(Display *, XErrorEvent *error);

  session current_session;

  struct tree* config;
}

namespace xa
{
  Atom net_wm_window_type;

  Atom net_wm_window_type_desktop;
  Atom net_wm_window_type_dock;
  Atom net_wm_window_type_toolbar;
  Atom net_wm_window_type_menu;
  Atom net_wm_window_type_utility;
  Atom net_wm_window_type_splash;
  Atom net_wm_window_type_dialog;
  Atom net_wm_window_type_normal;
}

static int
x_error_handler (Display *display, XErrorEvent *error)
{
  if (error->error_code == BadAccess && error->request_code == X_ChangeWindowAttributes)
    errx (EXIT_FAILURE, "Another window manager is already running");

  return x_default_error_handler (display, error);
}

static int x_error_discarder (Display *display, XErrorEvent *error)
{
  return 0;
}

static void
x_connect (void)
{
  int dummy, major, minor;
  char *c;

  if (!(x_display = XOpenDisplay (0)))
    errx (EXIT_FAILURE, "Failed to open default X11 display");

  x_default_error_handler = XSetErrorHandler (x_error_handler);

  /*** Global state ***/

  x_screen_index = DefaultScreen (x_display);
  x_screen = DefaultScreenOfDisplay (x_display);
  x_visual = DefaultVisual (x_display, x_screen_index);
  x_render_visual_format = XRenderFindVisualFormat (x_display, x_visual);
  x_visual_info = XGetVisualInfo (x_display, VisualNoMask, NULL, &x_visual_info_count);
  x_root_window = RootWindow (x_display, x_screen_index);

  xa::net_wm_window_type =         XInternAtom (x_display, "_NET_WM_WINDOW_TYPE", False);
  xa::net_wm_window_type_desktop = XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  xa::net_wm_window_type_dock =    XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_DOCK", False);
  xa::net_wm_window_type_toolbar = XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
  xa::net_wm_window_type_menu =    XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_MENU", False);
  xa::net_wm_window_type_utility = XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
  xa::net_wm_window_type_splash =  XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
  xa::net_wm_window_type_dialog =  XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  xa::net_wm_window_type_normal =  XInternAtom (x_display, "_NET_WM_WINDOW_TYPE_NORMAL", False);

  if ((c = XSetLocaleModifiers ("")) && *c)
    x_im = XOpenIM (x_display, 0, 0, 0);

  if (!x_im && (c = XSetLocaleModifiers ("@im=none")) && *c)
    x_im = XOpenIM (x_display, 0, 0, 0);

  if (!x_im)
    errx (EXIT_FAILURE, "Failed to open X Input Method");

  /* XXX: Should we really use root window here? */

  x_ic = XCreateIC (x_im,
                    XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                    XNClientWindow, x_root_window,
                    XNFocusWindow,
                    x_root_window,
                    NULL);

  if (!x_ic)
    errx (EXIT_FAILURE, "Failed to create X Input Context");

  /* Become a window manager */

  XSelectInput (x_display, x_root_window, SubstructureRedirectMask | SubstructureNotifyMask);

  /*** Screen geometry ***/

  if (XineramaQueryExtension (x_display, &dummy, &dummy)
      && XineramaIsActive (x_display))
    {
      XineramaScreenInfo *xinerama_screens;
      int i, screen_count;

      xinerama_screens = XineramaQueryScreens (x_display, &screen_count);

      for (i = 0; i < screen_count; ++i)
        {
          screen new_screen;

          new_screen.geometry = xinerama_screens[i];

          current_session.screens.push_back (new_screen);
        }

      XFree (xinerama_screens);
    }
  else /* Xinerama not active -> assume one big screen */
    {
      XWindowAttributes root_window_attr;
      screen new_screen;

      XGetWindowAttributes (x_display, x_root_window, &root_window_attr);

      new_screen.geometry.x_org = 0;
      new_screen.geometry.y_org = 0;
      new_screen.geometry.width = root_window_attr.width;
      new_screen.geometry.height = root_window_attr.height;

      current_session.screens.push_back (new_screen);
    }

  /*** Compositing ***/

  if (!XCompositeQueryExtension (x_display, &dummy, &dummy))
    errx (EXIT_FAILURE, "Missing XComposite extension");

  if (!XDamageQueryExtension (x_display, &x_damage_eventbase, &x_damage_errorbase))
    errx (EXIT_FAILURE, "Missing XDamage extension");

  XCompositeQueryVersion (x_display, &major, &minor);

  if (!(major > 0 || minor >= 2))
    errx (EXIT_FAILURE, "XComposite version %d.%d is too old.  1.2 or more required", major, minor);

  /* Create one window for each screen in which to composite its contents */

  XSetWindowAttributes window_attr;
  memset (&window_attr, 0, sizeof (window_attr));
  window_attr.colormap = DefaultColormap (x_display, 0);
  window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
  window_attr.override_redirect = True;

  XRenderPictureAttributes pa;
  memset (&pa, 0, sizeof (pa));
  pa.subwindow_mode = IncludeInferiors;

  XCompositeRedirectSubwindows (x_display, x_root_window, CompositeRedirectManual);

  for (auto &screen : current_session.screens)
    {
      Pixmap pixmap;

      screen.x_window
        = XCreateWindow (x_display, x_root_window,
                         screen.geometry.x_org, screen.geometry.y_org,
                         screen.geometry.width, screen.geometry.height,
                         0, /* Border */
                         x_visual_info->depth,
                         InputOutput,
                         x_visual,
                         CWOverrideRedirect | CWColormap | CWEventMask,
                         &window_attr);

      current_session.internal_x_windows.insert (screen.x_window);

      XCompositeUnredirectWindow (x_display, screen.x_window, CompositeRedirectManual);

      XMapWindow (x_display, screen.x_window);

      if (!(screen.x_picture = XRenderCreatePicture (x_display, screen.x_window, x_render_visual_format, CPSubwindowMode, &pa)))
        errx (EXIT_FAILURE, "Failed to create picture for screen window");

      pixmap = XCreatePixmap (x_display, screen.x_window, screen.geometry.width, screen.geometry.height, x_visual_info->depth);

      if (!(screen.x_buffer = XRenderCreatePicture (x_display, pixmap, x_render_visual_format, 0, 0)))
        errx (EXIT_FAILURE, "Failed to create back buffer for screen");

      XFreePixmap (x_display, pixmap);

      fprintf (stderr, "Screen has window %08lx has buffer %08lx\n",
               screen.x_window, screen.x_buffer);
    }

  fprintf (stderr, "Root has window %08lx\n",
           x_root_window);
}

static void
x_process_create_notify (const XCreateWindowEvent &cwe)
{
  window *new_window;

  if (current_session.internal_x_windows.count (cwe.window))
    return;

  if (cwe.override_redirect)
    {
      XCompositeUnredirectWindow (x_display, cwe.window, CompositeRedirectManual);

      return;
    }

  new_window = new window;

  new_window->x_window = cwe.window;
  new_window->position.x = cwe.x;
  new_window->position.y = cwe.y;
  new_window->position.width = cwe.width;
  new_window->position.height = cwe.height;

  current_session.unpositioned_windows.push_back (new_window);
}

void
x_paint_dirty_windows (void)
{
  for (auto &screen : current_session.screens)
    {
      if (!screen.x_damage_region)
        continue;

      XFixesSetPictureClipRegion (x_display, screen.x_buffer, 0, 0, screen.x_damage_region);

      for (auto &workspace : screen.workspaces)
        {
          for (auto &window : workspace)
            {
              if (!window->x_picture)
                continue;

              XRenderComposite (x_display,
                                PictOpSrc,
                                window->x_picture,
                                None,
                                screen.x_buffer,
                                0, 0,
                                0, 0,
                                window->position.x - screen.geometry.x_org,
                                window->position.y - screen.geometry.y_org,
                                window->position.width, window->position.height);
            }
        }

      XRenderComposite (x_display,
                        PictOpSrc,
                        screen.x_buffer,
                        None,
                        screen.x_picture,
                        0, 0,
                        0, 0,
                        0, 0,
                        screen.geometry.width, screen.geometry.height);

      XFixesDestroyRegion (x_display, screen.x_damage_region);
      screen.x_damage_region = 0;
    }
}

static void
x_process_events (void)
{
  XEvent event;

  while (0 == XNextEvent (x_display, &event))
    {
      switch (event.type)
        {
        case KeyPress:

          if (!XFilterEvent (&event, event.xkey.window))
            {
              wchar_t text[32];
              Status status;
              KeySym key_sym;
              int len;

              len = XwcLookupString (x_ic, &event.xkey, text, sizeof (text) / sizeof (text[0]) - 1, &key_sym, &status);
              text[len] = 0;

              switch (key_sym)
                {
                case XK_Home:

                    {
                      pid_t child;

                      if (!(child = fork ()))
                        {
                          char *args[2];

                          args[0] = (char *) "/usr/local/bin/cantera-term";
                          args[1] = NULL;

                          execve (args[0], args, environ);

                          exit (EXIT_FAILURE);
                        }
                    }

                  break;

                case XK_Escape:

                  exit (0);

                  break;
                }
            }

          break;

        case CreateNotify:

          x_process_create_notify (event.xcreatewindow);

          break;

        case DestroyNotify:

          current_session.remove_x_window (event.xdestroywindow.window);

          break;

        case MapRequest:

            {
              screen *scr;
              workspace *ws;
              window *w;

              if (!(w = current_session.find_x_window (event.xmap.window, &ws, &scr)))
                break;

              if (!ws)
                {
                  if (!scr)
                    scr = &current_session.screens[0]; /* XXX: Use the current screen instead */

                  ws = &scr->workspaces[scr->active_workspace];

                  current_session.move_window (w, scr, ws);
                }

                {
                  w->get_hints ();

                  switch (w->type)
                    {
                    case window_type_normal:

                      w->position.x = scr->geometry.x_org;
                      w->position.y = scr->geometry.y_org;
                      w->position.width = scr->geometry.width;
                      w->position.height = scr->geometry.height;

                      break;

                    default:

                      fprintf (stderr, "Unhandled type of window: %d\n", (int) w->type);
                    }
                }

              w->show ();
            }

          break;

        case MapNotify:

            {
              window *w;

              fprintf (stderr, "Window %08lx was mapped\n", event.xmap.window);

              if (!(w = current_session.find_x_window (event.xmap.window)))
                {
                  if (current_session.internal_x_windows.count (event.xmap.window))
                    break;

                  break;
                }

              w->init_composite ();
            }

          break;

        case UnmapNotify:

            {
              XEvent destroy_event;
              screen *scr;
              workspace *ws;
              window *w;

              fprintf (stderr, "Window %08lx was unmapped\n", destroy_event.xdestroywindow.window);

              /* Window is probably destroyed, so we check that first */
              while (XCheckTypedWindowEvent (x_display,
                                             x_root_window,
                                             DestroyNotify,
                                             &destroy_event))
                {
                  current_session.remove_x_window (destroy_event.xdestroywindow.window);
                }

              if (NULL != (w = current_session.find_x_window (event.xmap.window, &ws, &scr)))
                {
                  w->reset_composite ();
                }
            }

          break;

        case ConfigureNotify:

          /* XXX: Do we ever get this event?  Maybe for override-redirect? */
          if (window *w = current_session.find_x_window (event.xconfigure.window))
            {
              w->position.x = event.xconfigure.x;
              w->position.y = event.xconfigure.y;
              w->position.width = event.xconfigure.width;
              w->position.height = event.xconfigure.height;
            }

          break;

        case ConfigureRequest:

            {
              const XConfigureRequestEvent &cre = event.xconfigurerequest;
              window *w;

              if (!(w = current_session.find_x_window (cre.window)))
                break;

              XWindowChanges window_changes;
              int mask;

              memset (&window_changes, 0, sizeof (window_changes));

              mask = cre.value_mask;
              window_changes.sibling = cre.above;
              window_changes.stack_mode = cre.detail;

              if (mask & CWX)
                w->position.x = cre.x;

              if (mask & CWY)
                w->position.y = cre.y;

              if (mask & CWWidth)
                w->position.width = cre.width;

              if (mask & CWHeight)
                w->position.height = cre.height;

              w->constrain_size ();

              window_changes.x = w->position.x;
              window_changes.y = w->position.y;
              window_changes.width = w->position.width;
              window_changes.height = w->position.height;

              XConfigureWindow (x_display,
                                cre.window,
                                mask | CWX | CWY | CWWidth | CWHeight,
                                &window_changes);
            }

          break;

        default:

          if (event.type == x_damage_eventbase + XDamageNotify)
            {
              const XDamageNotifyEvent& dne = *(XDamageNotifyEvent *) &event;
              screen *scr;
              window *w;

              if (NULL != (w = current_session.find_x_window (dne.drawable, NULL, &scr)))
                {
                  if (scr)
                    {
                      if (!scr->x_damage_region)
                        scr->x_damage_region = XFixesCreateRegion (x_display, 0, 0);

                      XDamageSubtract (x_display, dne.damage, None, scr->x_damage_region);
                    }
                }
              else
                {
                  fprintf (stderr, "Drawable %08lx unrecognized!\n", dne.drawable);

                  XDamageSubtract (x_display, dne.damage, None, None);
                }
            }
        }

      if (!XPending (x_display))
        x_paint_dirty_windows ();
    }
}

int
main (int argc, char **argv)
{
  char *home;

  if (!(home = getenv ("HOME")))
    errx (EXIT_FAILURE, "Missing HOME environment variable");

  signal (SIGPIPE, SIG_IGN);
  signal (SIGALRM, SIG_IGN);

  if (-1 == chdir (home))
    err (EXIT_FAILURE, "Unable to chdir to '%s'", home);

  config = tree_load_cfg (".cantera/config");

  x_connect ();

  x_process_events ();
}

window::window ()
{
  memset (this, 0, sizeof (*this));

  type = window_type_unknown;
}

window::~window ()
{
}

void
window::get_hints ()
{
  if (type != window_type_unknown)
    return;

  XSync (x_display, False);
  XSetErrorHandler (x_error_discarder);

  Atom atom_type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned long* prop;

  /* XXX: This code has not been verified.  Also, we should use atom_type for something  */
  if (Success != XGetWindowProperty (x_display, x_window, xa::net_wm_window_type, 0, 1024, False,
                                     XA_ATOM, &atom_type, &format, &nitems,
                                     &bytes_after, (unsigned char**) &prop))
    type = window_type_normal;
  else if (!prop)
    type = window_type_normal;
  else if (*prop == xa::net_wm_window_type_desktop)
    type = window_type_desktop;
  else if (*prop == xa::net_wm_window_type_dock)
    type = window_type_dock;
  else if (*prop == xa::net_wm_window_type_toolbar)
    type = window_type_toolbar;
  else if (*prop == xa::net_wm_window_type_menu)
    type = window_type_menu;
  else if (*prop == xa::net_wm_window_type_utility)
    type = window_type_utility;
  else if (*prop == xa::net_wm_window_type_splash)
    type = window_type_splash;
  else if (*prop == xa::net_wm_window_type_dialog)
    type = window_type_dialog;
  else /* if (*prop == xa::net_wm_window_type_normal) */
    type = window_type_normal;

  XFree (prop);

  XGetTransientForHint (x_display, x_window, &x_transient_for);

  XSync (x_display, False);
  XSetErrorHandler (x_error_handler);

  if (x_transient_for)
    {
      if (type == window_type_normal)
	type = window_type_dialog;
    }
}

void
window::constrain_size ()
{
}

void
window::init_composite ()
{
  XWindowAttributes attr;
  XRenderPictFormat *format;
  XRenderPictureAttributes picture_attributes;

  if (x_picture)
    {
      assert (x_damage);

      return;
    }

  XGetWindowAttributes (x_display, x_window, &attr);

  format = XRenderFindVisualFormat (x_display, attr.visual);

  if (!format)
    errx (EXIT_FAILURE, "Unable to find visual format for window");

  memset (&picture_attributes, 0, sizeof (picture_attributes));
  picture_attributes.subwindow_mode = IncludeInferiors;

  x_picture = XRenderCreatePicture (x_display, x_window, format, CPSubwindowMode, &picture_attributes);
  x_damage = XDamageCreate (x_display, x_window, XDamageReportNonEmpty);

  fprintf (stderr, "Window %08lx has picture %08lx and damage %08lx\n",
           x_window, x_picture, x_damage);
}

void
window::reset_composite ()
{
  if (!x_picture)
    {
      assert (!x_damage);

      return;
    }

  /* It seems these are always already destroyed */

  fprintf (stderr, "Freeing picture %08lx\n", x_picture);
  XDamageDestroy (x_display, x_damage);
  XRenderFreePicture (x_display, x_picture);

  x_damage = 0;
  x_picture = 0;
}

void
window::show ()
{
  XWindowChanges wc;

  wc.x = position.x;
  wc.y = position.y;
  wc.width = position.width;
  wc.height = position.height;

  /* XXX: Quench this if the position is not dirty */
  XConfigureWindow (x_display, x_window,
                    CWX | CWY | CWWidth | CWHeight,
                    &wc);

  XMapWindow (x_display, x_window);
}

screen::screen ()
  : x_damage_region (0),
    active_workspace (0)
{
}

screen *
session::find_screen_for_window (Window x_window)
{
  for (auto &screen : screens)
    {
      if (screen.x_window == x_window)
        return &screen;
    }

  return NULL;
}

window *
session::find_x_window (Window x_window,
                        workspace **workspace_ret,
                        screen **screen_ret)
{
  for (auto &window : unpositioned_windows)
    {
      if (window->x_window == x_window)
        {
          if (workspace_ret)
            *workspace_ret = NULL;

          if (screen_ret)
            *screen_ret = NULL;

          return window;
        }
    }

  for (auto &screen : screens)
    {
      for (auto &workspace : screen.workspaces)
        {
          for (auto &window : workspace)
            {
              if (window->x_window == x_window)
                {
                  if (workspace_ret)
                    *workspace_ret = &workspace;

                  if (screen_ret)
                    *screen_ret = &screen;

                  return window;
                }
            }
        }
    }

  return NULL;
}

void
session::remove_x_window (Window x_window)
{
  fprintf (stderr, "Window %08lx was destroyed\n", x_window);

  auto predicate = [x_window](window *window) -> bool
    {
      return window->x_window == x_window;
    };

  auto i = std::find_if (unpositioned_windows.begin (),
                         unpositioned_windows.end (),
                         predicate);

  if (i != unpositioned_windows.end ())
    {
      fprintf (stderr, " -> st was unpositioned\n");
      delete *i;

      unpositioned_windows.erase (i);

      return;
    }

  for (auto &screen : screens)
    {
      for (auto &workspace : screen.workspaces)
        {
          auto i = std::find_if (workspace.begin (),
                                 workspace.end (),
                                 predicate);

          if (i != workspace.end ())
            {
              fprintf (stderr, " -> It was in a workspace\n");
              delete *i;

              workspace.erase (i);

              return;
            }
        }
    }
}

void
session::move_window (window *w, screen *scre, workspace *ws)
{
  /* XXX: Eliminate code duplication from remove_x_window */

  auto predicate = [w](window *arg) -> bool
    {
      return w == arg;
    };

  auto i = std::find_if (unpositioned_windows.begin (),
                         unpositioned_windows.end (),
                         predicate);

  if (i != unpositioned_windows.end ())
    unpositioned_windows.erase (i);
  else
    {
      for (auto &screen : screens)
        {
          for (auto &workspace : screen.workspaces)
            {
              auto i = std::find_if (workspace.begin (),
                                     workspace.end (),
                                     predicate);

              if (i != workspace.end ())
                {
                  workspace.erase (i);

                  goto found;
                }
            }
        }

found:;
    }

  ws->push_back (w);
}
