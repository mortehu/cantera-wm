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
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>

#include "cantera-wm.h"
#include "menu.h"
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

  session current_session;
}

using namespace cantera_wm;

namespace
{
  bool ctrl_pressed;
  bool mod1_pressed;
  bool super_pressed;
  bool shift_pressed;

  int (*x_default_error_handler)(Display *, XErrorEvent *error);

  struct tree* config;

  bool showing_menu;
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
  int result = 0;

  if (error->error_code == BadAccess && error->request_code == X_ChangeWindowAttributes)
    errx (EXIT_FAILURE, "Another window manager is already running");

  if (error->error_code == BadWindow)
    current_session.remove_x_window (error->resourceid);
  else if (error->error_code == BadDamage)
    fprintf (stderr, "BadDamage: %08lx\n", error->resourceid);
  else if (error->error_code == BadMatch)
    fprintf (stderr, "BadMatch: %08lx\n", error->resourceid);
  else
    fprintf (stderr, "Error: %d\n", error->error_code);

  return result;
}

static int
x_error_discarder (Display *display, XErrorEvent *error)
{
  return 0;
}

static void
x_grab_key (KeySym key, unsigned int modifiers)
{
  XGrabKey (x_display, XKeysymToKeycode (x_display, key),
            modifiers, x_root_window, False,
            GrabModeAsync, GrabModeAsync);
}

static void
x_grab_keys (void)
{
  static const int global_modifiers[] = { 0, LockMask, LockMask | Mod2Mask, Mod2Mask };
  size_t i, f, gmod;

  x_grab_key (XK_Alt_L, Mod4Mask);
  x_grab_key (XK_Alt_R, Mod4Mask);

  x_grab_key (XK_Super_L, AnyModifier);
  x_grab_key (XK_Super_R, AnyModifier);

  for (i = 0; i < sizeof (global_modifiers) / sizeof (global_modifiers[0]); ++i)
    {
      gmod = global_modifiers[i];

      for (f = 0; f < current_session.screens.size (); ++f)
        x_grab_key (XK_1 + f, Mod4Mask | gmod);

      x_grab_key (XK_Left,  ControlMask | Mod1Mask | gmod);
      x_grab_key (XK_Right, ControlMask | Mod1Mask | gmod);
      x_grab_key (XK_Up,    ControlMask | Mod1Mask | gmod);
      x_grab_key (XK_Down,  ControlMask | Mod1Mask | gmod);

      for (f = 0; f < 12; ++f)
        {
          x_grab_key (XK_F1 + f, ControlMask | gmod);
          x_grab_key (XK_F1 + f, Mod4Mask | gmod);
        }

      x_grab_key (XK_Escape, ControlMask | Mod1Mask | gmod);
      x_grab_key (XK_F4, Mod1Mask | gmod);
    }
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

          new_screen.geometry.x = xinerama_screens[i].x_org;
          new_screen.geometry.y = xinerama_screens[i].y_org;
          new_screen.geometry.width = xinerama_screens[i].width;
          new_screen.geometry.height = xinerama_screens[i].height;

          current_session.screens.push_back (new_screen);

          if (!i)
            current_session.desktop_geometry = new_screen.geometry;
          else
            current_session.desktop_geometry.union_rect (new_screen.geometry);
        }

      XFree (xinerama_screens);
    }
  else /* Xinerama not active -> assume one big screen */
    {
      XWindowAttributes root_window_attr;
      screen new_screen;

      XGetWindowAttributes (x_display, x_root_window, &root_window_attr);

      new_screen.geometry.x = 0;
      new_screen.geometry.y = 0;
      new_screen.geometry.width = root_window_attr.width;
      new_screen.geometry.height = root_window_attr.height;

      current_session.screens.push_back (new_screen);

      current_session.desktop_geometry = new_screen.geometry;
    }

  /*** Compositing ***/

  if (!XCompositeQueryExtension (x_display, &dummy, &dummy))
    errx (EXIT_FAILURE, "Missing XComposite extension");

  XCompositeQueryVersion (x_display, &major, &minor);

  if (!(major > 0 || minor >= 2))
    errx (EXIT_FAILURE, "XComposite version %d.%d is too old.  1.2 or more required", major, minor);

  if (!XDamageQueryExtension (x_display, &x_damage_eventbase, &x_damage_errorbase))
    errx (EXIT_FAILURE, "Missing XDamage extension");

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
                         screen.geometry.x, screen.geometry.y,
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

      /* Only the first screen window gets key events */
      window_attr.event_mask &= ~(KeyPressMask | KeyReleaseMask);
    }

  x_grab_keys ();

  fprintf (stderr, "Root has window %08lx\n", x_root_window);

  menu_init ();
}

static void
x_process_create_notify (const XCreateWindowEvent &cwe)
{
  window *new_window;

  if (current_session.internal_x_windows.count (cwe.window))
    return;

  new_window = new window;

  new_window->x_window = cwe.window;
  new_window->position.x = cwe.x;
  new_window->position.y = cwe.y;
  new_window->position.width = cwe.width;
  new_window->position.height = cwe.height;

  new_window->real_position = new_window->position;

  if (cwe.override_redirect)
    {
      XCompositeUnredirectWindow (x_display, cwe.window, CompositeRedirectManual);
      new_window->override_redirect = true;
    }

  current_session.unpositioned_windows.push_back (new_window);
}

static pid_t
launch_program (const char* command, Time when)
{
  char *args[4];
  char buf[32];
  pid_t pid;

  sprintf (buf, "%d", current_session.active_screen);
  setenv ("CURRENT_SCREEN", buf, 1);

  args[0] = (char *) "/bin/sh";
  args[1] = (char *) "-c";

  if (-1 == asprintf(&args[2], "exec %s", command))
    return -1;

  args[3] = (char *) NULL;

  /* Can't use vfork() because we want to call setsid */

  if (-1 == (pid = fork ()))
    return -1;

  if (pid)
    return pid;

  setsid ();

#if 0
  sprintf (buf, "%llu", (unsigned long long int) when);
  setenv ("DESKTOP_STARTUP_ID", buf, 1);

  sprintf (buf, ".cantera/bash-history-%02d", current_screen->active_terminal);
  setenv ("HISTFILE", buf, 1);

  sprintf (buf, ".cantera/session-%02d", current_screen->active_terminal);
  setenv ("SESSION_PATH", buf, 1);
#endif

  execve (args[0], args, environ);

  _exit (EXIT_FAILURE);
}

static void
x_paint_dirty_windows (void)
{
  for (auto &screen : current_session.screens)
    {
      XRenderColor black;
      bool draw_menu;

      draw_menu = showing_menu || screen.workspaces[screen.active_workspace].empty ();

      if (draw_menu || current_session.repaint_all)
        XFixesSetPictureClipRegion (x_display, screen.x_buffer, 0, 0, None);
      else
        {
          if (!screen.x_damage_region)
            continue;

#if 0
          /* XXX: Buggy on dual monitors, except for the monitor at offset 0,0 */

          XFixesSetPictureClipRegion (x_display, screen.x_buffer, 0, 0, screen.x_damage_region);
#endif
        }

      black.red = 0x0000;
      black.green = 0x0000;
      black.blue = 0x0000;
      black.alpha = 0xffff;

      XRenderFillRectangle (x_display, PictOpSrc, screen.x_buffer, &black,
                            0, 0, screen.geometry.width, screen.geometry.height);

      for (auto &window : screen.ancillary_windows)
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
                            window->real_position.x - screen.geometry.x,
                            window->real_position.y - screen.geometry.y,
                            window->real_position.width, window->real_position.height);
        }

      for (auto &window : screen.workspaces[screen.active_workspace])
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
                            window->real_position.x - screen.geometry.x,
                            window->real_position.y - screen.geometry.y,
                            window->real_position.width, window->real_position.height);
        }

      if (draw_menu)
        menu_draw (screen);

      XRenderComposite (x_display,
                        PictOpSrc,
                        screen.x_buffer,
                        None,
                        screen.x_picture,
                        0, 0,
                        0, 0,
                        0, 0,
                        screen.geometry.width, screen.geometry.height);

      if (screen.x_damage_region)
        {
          XFixesDestroyRegion (x_display, screen.x_damage_region);
          screen.x_damage_region = 0;
        }
    }

  current_session.repaint_all = false;
}

static void
update_focus (unsigned int screen_index, unsigned int workspace_index, Time x_event_time)
{
  screen *scr;
  bool hide_and_show;

  scr = &current_session.screens[screen_index];

  if ((hide_and_show = (scr->active_workspace != workspace_index))
      || current_session.active_screen != screen_index)
    {
      Window focus_window;

      focus_window = x_root_window;

      for (auto window : scr->workspaces[workspace_index])
        {
          if (hide_and_show)
            window->show ();

          focus_window = window->x_window;
        }

      if (hide_and_show)
        {
          for (auto window : scr->workspaces[scr->active_workspace])
            window->hide ();
        }

      scr->active_workspace = workspace_index;
      current_session.active_screen = screen_index;

      /* XXX: Check with ICCCM what the other parameters should be */
      XSetInputFocus (x_display, focus_window, RevertToPointerRoot, x_event_time);

      /* XXX: Clear navigation stack when implemented */
    }
}

static void
x_process_events (void)
{
  XEvent event;

  current_session.repaint_all = true;

  x_paint_dirty_windows ();

  for (;;)
    {
      XNextEvent (x_display, &event);

      if (XFilterEvent (&event, event.xkey.window))
        continue;

      switch (event.type)
        {
        case KeyPress:

            {
              wchar_t text[32];
              Status status;
              KeySym key_sym;
              int len;

              ctrl_pressed = (event.xkey.state & ControlMask);
              mod1_pressed = (event.xkey.state & Mod1Mask);
              super_pressed = (event.xkey.state & Mod4Mask);
              shift_pressed = (event.xkey.state & ShiftMask);

              len = XwcLookupString (x_ic, &event.xkey, text, sizeof (text) / sizeof (text[0]) - 1, &key_sym, &status);
              text[len] = 0;

              /* From experience, event.xkey.state is not enough.  Why? */
              if (key_sym == XK_Control_L || key_sym == XK_Control_R)
                ctrl_pressed = true;
              else if (key_sym == XK_Super_L || key_sym == XK_Super_R)
                super_pressed = true;
              else if (key_sym == XK_Alt_L || key_sym == XK_Alt_R)
                mod1_pressed = true;

              if ((key_sym == XK_q || key_sym == XK_Q) && (ctrl_pressed && mod1_pressed))
                exit (EXIT_SUCCESS);

              if (key_sym >= 'a' && key_sym <= 'z' && super_pressed)
                {
                  char key[10];
                  const char* command;

                  sprintf (key, "hotkey.%c", (int) key_sym);

                  if (NULL != (command = tree_get_string_default (config, key, NULL)))
                    launch_program (command, event.xkey.time);
                }
              else if ((super_pressed ^ ctrl_pressed) && key_sym >= XK_F1 && key_sym <= XK_F12)
                {
                  unsigned int new_active_workspace;

                  new_active_workspace = key_sym - XK_F1;

                  if (super_pressed)
                    new_active_workspace += 12;

                  update_focus (current_session.active_screen, new_active_workspace, event.xkey.time);
                }
              else if (super_pressed && key_sym >= XK_1 && key_sym < XK_1 + current_session.screens.size ())
                {
                  unsigned int new_screen;

                  new_screen = key_sym - XK_1;

                  update_focus (new_screen, current_session.screens[new_screen].active_workspace, event.xkey.time);
                }
              else if(super_pressed && (mod1_pressed ^ ctrl_pressed))
                {
                  showing_menu = true;
                }
              else
                {
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
                    }
                }
            }

          current_session.repaint_all = true;

          break;

        case KeyRelease:

            {
              KeySym key_sym;

              key_sym = XLookupKeysym (&event.xkey, 0);

              ctrl_pressed = (event.xkey.state & ControlMask);
              mod1_pressed = (event.xkey.state & Mod1Mask);
              super_pressed = (event.xkey.state & Mod4Mask);
              shift_pressed = (event.xkey.state & ShiftMask);

              if (key_sym == XK_Control_L || key_sym == XK_Control_R)
                ctrl_pressed = false;
              else if (key_sym == XK_Super_L || key_sym == XK_Super_R)
                super_pressed = false;
              else if (key_sym == XK_Alt_L || key_sym == XK_Alt_R)
                mod1_pressed = false;

              if(!super_pressed || !(mod1_pressed ^ ctrl_pressed))
                showing_menu = false;
            }

          current_session.repaint_all = true;

          break;

        case CreateNotify:

          fprintf (stderr, "Window created (%d, %d, %d, %d), parent %08lx (root = %08lx)\n",
                   event.xcreatewindow.x, event.xcreatewindow.y,
                   event.xcreatewindow.width, event.xcreatewindow.height,
                   event.xcreatewindow.parent,
                   x_root_window);

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

              bool give_focus = true;

              if (!(w = current_session.find_x_window (event.xmap.window, &ws, &scr)))
                break;

              w->get_hints ();

              if (!scr)
                scr = &current_session.screens[current_session.active_screen];

              if (w->type == window_type_desktop)
                {
                  current_session.move_window (w, scr, NULL);

                  w->position = scr->geometry;

                  give_focus = false;
                }
              else
                {
                  if (!ws)
                    {
                      unsigned int workspace;

                      if (w->type == window_type_normal)
                        {
                          for (workspace = scr->active_workspace; workspace < 24; ++workspace)
                            {
                              if (scr->workspaces[workspace].empty ())
                                break;
                            }

                          /* All workspaces are full; do not map window */
                          if (workspace == 24)
                            break;

                          scr->active_workspace = workspace;
                        }
                      else
                        workspace = scr->active_workspace;

                      ws = &scr->workspaces[workspace];

                      current_session.move_window (w, scr, ws);
                    }

                  switch (w->type)
                    {
                    case window_type_normal:

                      w->position = scr->geometry;

                      break;

                    default:

                      fprintf (stderr, "Unhandled type of window: %d\n", (int) w->type);
                    }
                }

              w->show ();

              XMapWindow (x_display, w->x_window);

              if (give_focus)
                {
                  /* XXX: Check with ICCCM what the other parameters should be */
                  XSetInputFocus (x_display, w->x_window, RevertToPointerRoot, CurrentTime);
                }
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
              current_session.repaint_all = true;

              /* Window is probably destroyed, so we check that first */
              while (XCheckTypedWindowEvent (x_display,
                                             x_root_window,
                                             DestroyNotify,
                                             &destroy_event))
                {
                  current_session.remove_x_window (destroy_event.xdestroywindow.window);
                }

              if (NULL != (w = current_session.find_x_window (event.xmap.window, &ws, &scr)))
                w->reset_composite ();
            }

          break;

        case ConfigureNotify:

          current_session.repaint_all = true;

          if (window *w = current_session.find_x_window (event.xconfigure.window))
            {
              w->real_position.x = event.xconfigure.x;
              w->real_position.y = event.xconfigure.y;
              w->real_position.width = event.xconfigure.width;
              w->real_position.height = event.xconfigure.height;
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

                      if (w->real_position.x || w->real_position.y)
                        {
                          XserverRegion tmp_region;

                          tmp_region = XFixesCreateRegion(x_display, 0, 0);

                          XDamageSubtract (x_display, dne.damage, None, tmp_region);

                          XFixesTranslateRegion (x_display, tmp_region, w->real_position.x, w->real_position.y);

                          XFixesUnionRegion (x_display, scr->x_damage_region,
                                             scr->x_damage_region, tmp_region);

                          XFixesDestroyRegion (x_display, tmp_region);
                        }
                      else
                        XDamageSubtract (x_display, dne.damage, None, scr->x_damage_region);
                    }
                }
              else
                XDamageSubtract (x_display, dne.damage, None, None);
            }
        }

      if (XPending (x_display))
        continue;

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

void
rect::union_rect (struct rect &other)
{
  if (x > other.x)
    x = other.x;

  if (y > other.y)
    y = other.y;

  if (x + width < other.x + other.width)
    width = (other.x + other.width) - x;

  if (y + height < other.y + other.width)
    height = (other.y + other.width) - y;
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
  if (x_picture)
    {
      assert (x_damage);

      return;
    }

  if (!override_redirect)
    {
      XWindowAttributes attr;
      XRenderPictFormat *format;
      XRenderPictureAttributes picture_attributes;

      XGetWindowAttributes (x_display, x_window, &attr);

      format = XRenderFindVisualFormat (x_display, attr.visual);

      if (!format)
        errx (EXIT_FAILURE, "Unable to find visual format for window");

      memset (&picture_attributes, 0, sizeof (picture_attributes));
      picture_attributes.subwindow_mode = IncludeInferiors;

      x_picture = XRenderCreatePicture (x_display, x_window, format, CPSubwindowMode, &picture_attributes);
    }

  x_damage = XDamageCreate (x_display, x_window, XDamageReportNonEmpty);

  fprintf (stderr, "Window %08lx has picture %08lx and damage %08lx\n",
           x_window, x_picture, x_damage);
}

void
window::reset_composite ()
{
  /* XXX: It seems these are always already destroyed? */

  if (x_damage)
    {
      XDamageDestroy (x_display, x_damage);

      x_damage = 0;
    }

  if (x_picture)
    {
      XRenderFreePicture (x_display, x_picture);

      x_picture = 0;
    }
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
}

void
window::hide ()
{
  XWindowChanges wc;

  wc.x = current_session.desktop_geometry.x + current_session.desktop_geometry.width;

  XConfigureWindow (x_display, x_window, CWX, &wc);
}

screen::screen ()
  : x_damage_region (0),
    active_workspace (0)
{
}

session::session ()
  : active_screen (0)
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
      for (auto &window : screen.ancillary_windows)
        {
          if (window->x_window == x_window)
            {
              if (workspace_ret)
                *workspace_ret = NULL;

              if (screen_ret)
                *screen_ret = &screen;

              return window;
            }
        }

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

  current_session.repaint_all = true;

  auto predicate = [x_window](window *window) -> bool
    {
      return window->x_window == x_window;
    };

  auto i = std::find_if (unpositioned_windows.begin (),
                         unpositioned_windows.end (),
                         predicate);

  if (i != unpositioned_windows.end ())
    {
      fprintf (stderr, " -> It was unpositioned\n");
      delete *i;

      unpositioned_windows.erase (i);

      return;
    }

  for (auto &screen : screens)
    {
      auto i = std::find_if (screen.ancillary_windows.begin (),
                             screen.ancillary_windows.end (),
                             predicate);

      if (i != screen.ancillary_windows.end ())
        {
          fprintf (stderr, " -> It was an ancillary window\n");
          delete *i;

          screen.ancillary_windows.erase (i);

          return;
        }

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
session::move_window (window *w, screen *scr, workspace *ws)
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
    {
      unpositioned_windows.erase (i);

      goto found;
    }

  for (auto &screen : screens)
    {
      auto i = std::find_if (screen.ancillary_windows.begin (),
                             screen.ancillary_windows.end (),
                             predicate);

      if (i != screen.ancillary_windows.end ())
        {
          screen.ancillary_windows.erase (i);

          goto found;
        }

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

found:

  if (ws)
    ws->push_back (w);
  else
    scr->ancillary_windows.push_back (w);
}
