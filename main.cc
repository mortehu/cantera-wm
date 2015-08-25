#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <set>

#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
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
#include "xa.h"

namespace cantera_wm {

const size_t kWorkspaceCount = 24;

Display* x_display;
int x_screen_index;
::Screen* x_screen;
Visual* x_visual;
XRenderPictFormat* x_render_visual_format;
XVisualInfo* x_visual_info;
int x_visual_info_count;
::Window x_root_window;

XIM x_im;
XIC x_ic;
int x_damage_eventbase;
int x_damage_errorbase;

Session current_session;

std::set<pid_t> children;

void Screen::UpdateFocus(unsigned int workspace_index, Time x_event_time) {
  ::Window focus_window;
  bool hide_and_show;

  hide_and_show = (active_workspace != workspace_index);

  focus_window = x_root_window;

  std::vector<cantera_wm::Window*> focus_candidates;

  for (auto window : workspaces[workspace_index]) {
    if (hide_and_show) window->show();

    if (window->AcceptsInput()) focus_candidates.emplace_back(window);
  }

  if (focus_candidates.empty()) {
    fprintf(stderr, "No focus candidate windows in workspace %u\n",
            workspace_index);

    for (auto window : workspaces[workspace_index]) {
      fprintf(stderr, "  %s:", window->Description().c_str());

      if (!window->AcceptsInput()) fprintf(stderr, " (doesn't accept input)");

      fprintf(stderr, "\n");
    }
  } else {
    if (focus_candidates.size() > 1) {
      fprintf(stderr, "%zu focus candidate windows in workspace %u\n",
              focus_candidates.size(), workspace_index);

      for (const auto window : focus_candidates)
        fprintf(stderr, "  %s: Type: %d\n", window->Description().c_str(),
                static_cast<int>(window->Type()));
    }

    focus_window = focus_candidates.back()->x_window;
  }

  if (hide_and_show) {
    for (auto window : workspaces[active_workspace]) window->hide();
  }

  active_workspace = workspace_index;

  XSetInputFocus(x_display, focus_window, RevertToParent, x_event_time);
}

}  // namespace cantera_wm

using namespace cantera_wm;

namespace {

enum Option { kOptionEventLog = 'l' };

int print_version;
int print_help;
std::unique_ptr<FILE, decltype(&fclose)> event_log(nullptr, fclose);

struct option kLongOptions[] = {
    {"event-log", required_argument, nullptr, kOptionEventLog},
    {"version", no_argument, &print_version, 1},
    {"help", no_argument, &print_help, 1},
    {nullptr, 0, nullptr, 0}};

bool ctrl_pressed;
bool mod1_pressed;
bool super_pressed;
bool shift_pressed;

int (*x_default_error_handler)(Display*, XErrorEvent* error);

struct tree* config;

int x_error_handler(Display* display, XErrorEvent* error) {
  int result = 0;

  if (error->error_code == BadAccess &&
      error->request_code == X_ChangeWindowAttributes)
    errx(EXIT_FAILURE, "Another window manager is already running");

  if (error->error_code == BadWindow) {
    fprintf(stderr, "Got BadWindow error for window 0x%lx\n",
            error->resourceid);
    current_session.remove_x_window(error->resourceid);
  } else if (error->error_code == x_damage_errorbase + BadDamage) {
    fprintf(stderr, "BadDamage: 0x%lx\n", error->resourceid);
  } else if (error->error_code == BadMatch) {
    fprintf(stderr, "BadMatch: 0x%lx\n", error->resourceid);
  } else {
    fprintf(stderr, "Error: %d\n", error->error_code);
  }

  return result;
}

void x_grab_key(KeySym key, unsigned int modifiers) {
  XGrabKey(x_display, XKeysymToKeycode(x_display, key), modifiers,
           x_root_window, False, GrabModeAsync, GrabModeAsync);
}

void x_grab_keys() {
  static const int global_modifiers[] = {0, LockMask, LockMask | Mod2Mask,
                                         Mod2Mask};
  size_t i, f, gmod;

  x_grab_key(XK_Alt_L, Mod4Mask);
  x_grab_key(XK_Alt_R, Mod4Mask);

  x_grab_key(XK_Super_L, AnyModifier);
  x_grab_key(XK_Super_R, AnyModifier);

  for (i = 0; i < sizeof(global_modifiers) / sizeof(global_modifiers[0]); ++i) {
    gmod = global_modifiers[i];

    for (f = 0; f < current_session.ScreenCount(); ++f)
      x_grab_key(XK_1 + f, Mod4Mask | gmod);

    x_grab_key(XK_Left, ControlMask | Mod1Mask | gmod);
    x_grab_key(XK_Right, ControlMask | Mod1Mask | gmod);
    x_grab_key(XK_Up, ControlMask | Mod1Mask | gmod);
    x_grab_key(XK_Down, ControlMask | Mod1Mask | gmod);

    for (f = 0; f < 12; ++f) {
      x_grab_key(XK_F1 + f, ControlMask | gmod);
      x_grab_key(XK_F1 + f, Mod4Mask | gmod);
    }

    x_grab_key(XK_Escape, ControlMask | Mod1Mask | gmod);
    x_grab_key(XK_F4, Mod1Mask | gmod);
  }
}

void x_connect() {
  int dummy, major, minor;
  char* c;

  if (!(x_display = XOpenDisplay(0)))
    errx(EXIT_FAILURE, "Failed to open default X11 display");

  x_default_error_handler = XSetErrorHandler(x_error_handler);

  /*** Global state ***/

  x_screen_index = DefaultScreen(x_display);
  x_screen = DefaultScreenOfDisplay(x_display);
  x_visual = DefaultVisual(x_display, x_screen_index);
  x_render_visual_format = XRenderFindVisualFormat(x_display, x_visual);
  x_visual_info =
      XGetVisualInfo(x_display, VisualNoMask, NULL, &x_visual_info_count);
  x_root_window = RootWindow(x_display, x_screen_index);

  xa::net_wm_window_type = XInternAtom(x_display, "_NET_WM_WINDOW_TYPE", False);
  xa::net_wm_window_type_desktop =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  xa::net_wm_window_type_dock =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_DOCK", False);
  xa::net_wm_window_type_toolbar =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
  xa::net_wm_window_type_menu =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_MENU", False);
  xa::net_wm_window_type_utility =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
  xa::net_wm_window_type_splash =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
  xa::net_wm_window_type_dialog =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  xa::net_wm_window_type_normal =
      XInternAtom(x_display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
  xa::wm_delete_window = XInternAtom(x_display, "WM_DELETE_WINDOW", False);
  xa::wm_state = XInternAtom(x_display, "WM_STATE", False);
  xa::wm_protocols = XInternAtom(x_display, "WM_PROTOCOLS", False);

  if ((c = XSetLocaleModifiers("")) && *c) x_im = XOpenIM(x_display, 0, 0, 0);

  if (!x_im && (c = XSetLocaleModifiers("@im=none")) && *c)
    x_im = XOpenIM(x_display, 0, 0, 0);

  if (!x_im) errx(EXIT_FAILURE, "Failed to open X Input Method");

  /* XXX: Should we really use root window here? */

  x_ic = XCreateIC(x_im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                   XNClientWindow, x_root_window, XNFocusWindow, x_root_window,
                   NULL);

  if (!x_ic) errx(EXIT_FAILURE, "Failed to create X Input Context");

  /* Become a window manager */

  XSelectInput(x_display, x_root_window,
               SubstructureRedirectMask | SubstructureNotifyMask);

  /*** Screen geometry ***/

  if (XineramaQueryExtension(x_display, &dummy, &dummy) &&
      XineramaIsActive(x_display)) {
    XineramaScreenInfo* xinerama_screens;
    int i, screen_count;

    xinerama_screens = XineramaQueryScreens(x_display, &screen_count);
    assert(screen_count >= 1);

    for (i = 0; i < screen_count; ++i) {
      cantera_wm::Screen new_screen;
      new_screen.geometry.x = xinerama_screens[i].x_org;
      new_screen.geometry.y = xinerama_screens[i].y_org;
      new_screen.geometry.width = xinerama_screens[i].width;
      new_screen.geometry.height = xinerama_screens[i].height;

      current_session.AddScreen(new_screen);
    }

    XFree(xinerama_screens);
  } else /* Xinerama not active -> assume one big screen */
  {
    XWindowAttributes root_window_attr;
    XGetWindowAttributes(x_display, x_root_window, &root_window_attr);

    cantera_wm::Screen new_screen;
    new_screen.geometry.x = 0;
    new_screen.geometry.y = 0;
    new_screen.geometry.width = root_window_attr.width;
    new_screen.geometry.height = root_window_attr.height;

    current_session.AddScreen(new_screen);
  }

  /*** Compositing ***/

  if (!XCompositeQueryExtension(x_display, &dummy, &dummy))
    errx(EXIT_FAILURE, "Missing XComposite extension");

  XCompositeQueryVersion(x_display, &major, &minor);

  if (!(major > 0 || minor >= 2))
    errx(EXIT_FAILURE,
         "XComposite version %d.%d is too old.  1.2 or more required", major,
         minor);

  if (!XDamageQueryExtension(x_display, &x_damage_eventbase,
                             &x_damage_errorbase))
    errx(EXIT_FAILURE, "Missing XDamage extension");

  /* Create one window for each screen in which to composite its contents */

  XSetWindowAttributes window_attr;
  memset(&window_attr, 0, sizeof(window_attr));
  window_attr.colormap = DefaultColormap(x_display, 0);
  window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask |
                           ButtonReleaseMask | PointerMotionMask | ExposureMask;
  window_attr.override_redirect = True;

  XRenderPictureAttributes pa;
  memset(&pa, 0, sizeof(pa));
  pa.subwindow_mode = IncludeInferiors;

  XCompositeRedirectSubwindows(x_display, x_root_window,
                               CompositeRedirectManual);

  for (size_t i = 0; i < current_session.ScreenCount(); ++i) {
    cantera_wm::Screen* screen = current_session.GetScreen(i);

    screen->x_window = XCreateWindow(
        x_display, x_root_window, screen->geometry.x, screen->geometry.y,
        screen->geometry.width, screen->geometry.height, 0, /* Border */
        x_visual_info->depth, InputOutput, x_visual,
        CWOverrideRedirect | CWColormap | CWEventMask, &window_attr);

    current_session.AddInternalXWindow(screen->x_window);

    XCompositeUnredirectWindow(x_display, screen->x_window,
                               CompositeRedirectManual);

    XMapWindow(x_display, screen->x_window);

    if (!(screen->x_picture = XRenderCreatePicture(x_display, screen->x_window,
                                                   x_render_visual_format,
                                                   CPSubwindowMode, &pa)))
      errx(EXIT_FAILURE, "Failed to create picture for screen window");

    Pixmap pixmap;
    pixmap = XCreatePixmap(x_display, screen->x_window, screen->geometry.width,
                           screen->geometry.height, x_visual_info->depth);

    if (!(screen->x_buffer = XRenderCreatePicture(
              x_display, pixmap, x_render_visual_format, 0, 0)))
      errx(EXIT_FAILURE, "Failed to create back buffer for screen");

    XFreePixmap(x_display, pixmap);

    fprintf(stderr, "Screen has window 0x%lx has buffer 0x%lx\n",
            screen->x_window, screen->x_buffer);

    /* Only the first screen window gets key events */
    window_attr.event_mask &= ~(KeyPressMask | KeyReleaseMask);
  }

  x_grab_keys();

  fprintf(stderr, "Root has window %08lx\n", x_root_window);

  menu_init();
}

pid_t launch_program(const char* command, Time when) {
  char* args[4];
  char buf[32];
  pid_t pid;

  sprintf(buf, "%zu", current_session.ActiveScreenIndex());
  setenv("CURRENT_SCREEN", buf, 1);

  args[0] = const_cast<char*>("/bin/sh");
  args[1] = const_cast<char*>("-c");

  if (-1 == asprintf(&args[2], "exec %s", command)) return -1;

  args[3] = nullptr;

  /* Can't use vfork() because we want to call setsid */

  if (-1 == (pid = fork())) return -1;

  if (pid) {
    children.insert(pid);

    return pid;
  }

  setsid();

  execve(args[0], args, environ);

  _exit(EXIT_FAILURE);
}

void wait_for_dead_children() {
  int status;
  pid_t child;

  while (!children.empty() && (0 < (child = waitpid(-1, &status, WNOHANG))))
    children.erase(child);
}

void HandleMapRequest(const XMapRequestEvent& xmaprequest) {
  cantera_wm::Screen* scr;
  workspace* ws;
  cantera_wm::Window* w;

  if (!(w = current_session.find_x_window(xmaprequest.window, &ws, &scr))) {
    fprintf(stderr, "MapRequest received for unknown window %08lx\n",
            xmaprequest.window);
    return;
  }

  w->GetHints();
  w->ReadProperties();

  fprintf(stderr, "Map window %08lx of type %s\n", xmaprequest.window,
          cantera_wm::Window::StringFromType(w->Type()));

  if (!scr) scr = current_session.ActiveScreen();

  bool give_focus = true;

  switch (w->Type()) {
    case cantera_wm::Window::window_type_desktop:
      current_session.move_window(w, scr, NULL);

      w->position = scr->geometry;

      give_focus = false;
      break;

    default: {
      if (ws) break;
      unsigned int workspace;

      if (w->Type() == cantera_wm::Window::window_type_normal) {
        // First, try remaining slots in current workspace.
        for (workspace = scr->active_workspace; workspace < kWorkspaceCount;
             ++workspace) {
          if (scr->workspaces[workspace].empty()) break;
        }

        if (workspace == kWorkspaceCount) {
          // Next, try preceding slots in current workspace.
          for (workspace = 0; workspace < scr->active_workspace; ++workspace) {
            if (scr->workspaces[workspace].empty()) break;
          }

          if (workspace == scr->active_workspace) {
            fprintf(stderr,
                    "All workspaces on current screen are in use.  Cannot map "
                    "window\n");
            return;
          }
        }

        scr->active_workspace = workspace;

        auto& navstack = scr->navigation_stack;

        navstack.erase(std::remove(navstack.begin(), navstack.end(), workspace),
                       navstack.end());
        navstack.push_back(workspace);
      } else {
        workspace = scr->active_workspace;
      }

      ws = &scr->workspaces[workspace];

      current_session.move_window(w, scr, ws);
    }
  }

  switch (w->Type()) {
    case cantera_wm::Window::window_type_normal:
      w->position = scr->geometry;
      break;

    default:
      fprintf(stderr, "Unhandled type of window: %s\n",
              cantera_wm::Window::StringFromType(w->Type()));
  }

  w->show();

  static unsigned long kMappedState[2] = {NormalState, None};
  XChangeProperty(x_display, w->x_window, xa::wm_state, xa::wm_state, 32,
                  PropModeReplace,
                  reinterpret_cast<unsigned char*>(kMappedState), 2);

  XMapWindow(x_display, w->x_window);

  if (give_focus) {
    /* XXX: Check with ICCCM what the other parameters should be */
    XSetInputFocus(x_display, w->x_window, RevertToParent, CurrentTime);
  }
}

void ProcessEvent(XEvent& event) {
  switch (event.type) {
    case PropertyNotify: {
      auto w = current_session.find_x_window(event.xmap.window);
      if (!w) break;

      switch (event.xproperty.atom) {
        case XA_WM_NAME: {
          const auto old_name = w->Description();
          w->GetName();
          fprintf(stderr, "New name for %s: %s\n", old_name.c_str(),
                  w->Description().c_str());
        } break;

        case XA_WM_HINTS:
          fprintf(stderr, "New WM hints for %s\n", w->Description().c_str());
          w->GetWMHints();
          break;
      }
    } break;

    case KeyPress: {
      wchar_t text[32];
      Status status;
      KeySym key_sym;
      int len;

      ctrl_pressed = (event.xkey.state & ControlMask);
      mod1_pressed = (event.xkey.state & Mod1Mask);
      super_pressed = (event.xkey.state & Mod4Mask);
      shift_pressed = (event.xkey.state & ShiftMask);

      len = XwcLookupString(x_ic, &event.xkey, text,
                            sizeof(text) / sizeof(text[0]) - 1, &key_sym,
                            &status);
      text[len] = 0;

      /* From experience, event.xkey.state is not enough.  Why? */
      if (key_sym == XK_Control_L || key_sym == XK_Control_R)
        ctrl_pressed = true;
      else if (key_sym == XK_Super_L || key_sym == XK_Super_R)
        super_pressed = true;
      else if (key_sym == XK_Alt_L || key_sym == XK_Alt_R)
        mod1_pressed = true;

      if (key_sym >= 'a' && key_sym <= 'z' && super_pressed) {
        char key[10];
        const char* command;

        sprintf(key, "hotkey.%c", (int)key_sym);

        if (NULL != (command = tree_get_string_default(config, key, NULL)))
          launch_program(command, event.xkey.time);
      } else if ((super_pressed ^ ctrl_pressed) && key_sym >= XK_F1 &&
                 key_sym <= XK_F12) {
        unsigned int new_active_workspace;
        cantera_wm::Screen* scr;

        new_active_workspace = key_sym - XK_F1;

        if (super_pressed) new_active_workspace += 12;

        current_session.ActiveScreen()->UpdateFocus(new_active_workspace,
                                                    event.xkey.time);

        scr = current_session.ActiveScreen();
        scr->navigation_stack.clear();
        scr->navigation_stack.push_back(new_active_workspace);

        current_session.SetDirty();
      } else if (super_pressed && key_sym >= XK_1 &&
                 key_sym < XK_1 + current_session.ScreenCount()) {
        unsigned int new_screen;

        new_screen = key_sym - XK_1;

        current_session.SetActiveScreen(new_screen);
        current_session.ActiveScreen()->UpdateFocus(
            current_session.ActiveScreen()->active_workspace, event.xkey.time);

        current_session.SetDirty();
      } else if (super_pressed && (mod1_pressed ^ ctrl_pressed)) {
        int direction = 0;
        current_session.ShowMenu();
        switch (key_sym) {
          case XK_Up:
            direction = -12;
            break;
          case XK_Down:
            direction = 12;
            break;
          case XK_Left:
            direction = -1;
            break;
          case XK_Right:
            direction = 1;
            break;
        }
        if (direction) {
          cantera_wm::Screen* scr = current_session.ActiveScreen();
          unsigned int new_workspace =
              (kWorkspaceCount + scr->active_workspace + direction) %
              kWorkspaceCount;

          if (ctrl_pressed) {
            scr->workspaces[scr->active_workspace].swap(
                scr->workspaces[new_workspace]);
            if (!scr->navigation_stack.empty())
              scr->navigation_stack.back() = new_workspace;
            scr->active_workspace = new_workspace;
          } else {
            current_session.ActiveScreen()->UpdateFocus(new_workspace,
                                                        event.xkey.time);

            scr->navigation_stack.clear();
            scr->navigation_stack.push_back(new_workspace);
          }
        }

        current_session.SetDirty();
      } else if (mod1_pressed && key_sym == XK_F4) {
        XClientMessageEvent cme;
        cantera_wm::Screen* scr;
        cantera_wm::Window* w;

        scr = current_session.ActiveScreen();

        if (scr->workspaces[scr->active_workspace].empty()) break;

        w = scr->workspaces[scr->active_workspace].front();

        cme.type = ClientMessage;
        cme.send_event = True;
        cme.display = x_display;
        cme.window = w->x_window;
        cme.message_type = xa::wm_protocols;
        cme.format = 32;
        cme.data.l[0] = xa::wm_delete_window;
        cme.data.l[1] = event.xkey.time;

        XSendEvent(x_display, w->x_window, False, 0, (XEvent*)&cme);
      }
    } break;

    case KeyRelease: {
      KeySym key_sym;

      key_sym = XLookupKeysym(&event.xkey, 0);

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

      if (!super_pressed || !(mod1_pressed ^ ctrl_pressed))
        current_session.HideMenu();
    } break;

    case CreateNotify: {
      fprintf(stderr,
              "Window created (%d, %d, %d, %d), parent 0x%lx (root = 0x%lx)\n",
              event.xcreatewindow.x, event.xcreatewindow.y,
              event.xcreatewindow.width, event.xcreatewindow.height,
              event.xcreatewindow.parent, x_root_window);

      current_session.ProcessXCreateWindowEvent(event.xcreatewindow);
    } break;

    case DestroyNotify: {
      current_session.remove_x_window(event.xdestroywindow.window);
    } break;

    case MapRequest:
      HandleMapRequest(event.xmaprequest);
      break;

    case MapNotify: {
      auto w = current_session.find_x_window(event.xmap.window);
      if (!w) break;

      w->init_composite();
    } break;

    case UnmapNotify: {
      XEvent destroy_event;
      cantera_wm::Screen* scr;
      workspace* ws;
      cantera_wm::Window* w;

      fprintf(stderr, "Window 0x%lx was unmapped\n",
              destroy_event.xdestroywindow.window);

      /* Window is probably destroyed, so we check that first */
      while (XCheckTypedWindowEvent(x_display, x_root_window, DestroyNotify,
                                    &destroy_event)) {
        current_session.remove_x_window(destroy_event.xdestroywindow.window);
      }

      if (NULL !=
          (w = current_session.find_x_window(event.xunmap.window, &ws, &scr))) {
        w->reset_composite();
        current_session.SetDirty();
      }
    } break;

    case ConfigureNotify: {
      if (auto w = current_session.find_x_window(event.xconfigure.window)) {
        w->real_position.x = event.xconfigure.x;
        w->real_position.y = event.xconfigure.y;
        w->real_position.width = event.xconfigure.width;
        w->real_position.height = event.xconfigure.height;
      }

      current_session.SetDirty();
    } break;

    case ConfigureRequest: {
      const XConfigureRequestEvent& cre = event.xconfigurerequest;
      cantera_wm::Window* w;

      if (!(w = current_session.find_x_window(cre.window))) break;

      XWindowChanges window_changes;
      int mask;

      memset(&window_changes, 0, sizeof(window_changes));

      mask = cre.value_mask;
      window_changes.sibling = cre.above;
      window_changes.stack_mode = cre.detail;

      if (mask & CWX) w->position.x = cre.x;
      if (mask & CWY) w->position.y = cre.y;
      if (mask & CWWidth) w->position.width = cre.width;
      if (mask & CWHeight) w->position.height = cre.height;

      w->constrain_size();

      window_changes.x = w->position.x;
      window_changes.y = w->position.y;
      window_changes.width = w->position.width;
      window_changes.height = w->position.height;

      XConfigureWindow(x_display, cre.window,
                       mask | CWX | CWY | CWWidth | CWHeight, &window_changes);
    } break;

    default: {
      if (event.type == x_damage_eventbase + XDamageNotify) {
        const auto& dne = *reinterpret_cast<XDamageNotifyEvent*>(&event);

        cantera_wm::Screen* scr;
        auto w = current_session.find_x_window(dne.drawable, NULL, &scr);

        if (!w) {
          XDamageSubtract(x_display, dne.damage, None, None);
          break;
        }

        if (!scr) break;

        current_session.SetDamaged();

        if (!scr->x_damage_region)
          scr->x_damage_region = XFixesCreateRegion(x_display, 0, 0);

        if (w->real_position.x || w->real_position.y) {
          XserverRegion tmp_region;

          tmp_region = XFixesCreateRegion(x_display, 0, 0);

          XDamageSubtract(x_display, dne.damage, None, tmp_region);

          XFixesTranslateRegion(x_display, tmp_region, w->real_position.x,
                                w->real_position.y);

          XFixesUnionRegion(x_display, scr->x_damage_region,
                            scr->x_damage_region, tmp_region);

          XFixesDestroyRegion(x_display, tmp_region);
        } else {
          XDamageSubtract(x_display, dne.damage, None, scr->x_damage_region);
        }
      }
    }
  }
}

void x_process_events() {
  current_session.SetDirty();

  for (;;) {
    wait_for_dead_children();

    while (!current_session.Dirty() || XPending(x_display)) {
      XEvent event;
      XNextEvent(x_display, &event);

      if (event_log) {
        auto event_uc = reinterpret_cast<unsigned char*>(&event);
        for (size_t i = 0; i < sizeof(event); ++i) {
          if (i) fputc(' ', event_log.get());
          fprintf(event_log.get(), "%02x", event_uc[i]);
        }
        fprintf(event_log.get(), "\n");
        fflush(event_log.get());
      }

      if (!XFilterEvent(&event, event.xkey.window)) ProcessEvent(event);
    }

    current_session.Paint();
  }
}

void reload_config() {
  if (config) tree_destroy(config);

  config = tree_load_cfg(".cantera/config");
}

void sighandler(int signal) {
  switch (signal) {
    case SIGUSR1:
      reload_config();
      break;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int i;
  while ((i = getopt_long(argc, argv, "f:", kLongOptions, 0)) != -1) {
    if (!i) continue;
    if (i == '?')
      errx(EX_USAGE, "Try '%s --help' for more information.", argv[0]);

    switch (static_cast<Option>(i)) {
      case kOptionEventLog:
        event_log.reset(fopen(optarg, "w"));
        break;
    }
  }

  if (print_help) {
    printf(
        "Usage: %s [OPTION]... [FILE]...\n"
        "\n"
        "      --event-log=PATH            write X11 events to PATH\n"
        "      --help     display this help and exit\n"
        "      --version  display version information and exit\n"
        "\n"
        "With no FILE, or when FILE is -, read standard input.\n"
        "\n"
        "Report bugs to <morten.hustveit@gmail.com>\n",
        argv[0]);

    return EXIT_SUCCESS;
  }

  if (print_version) {
    puts(PACKAGE_STRING);

    return EXIT_SUCCESS;
  }

  signal(SIGUSR1, sighandler);

  char* home;
  if (!(home = getenv("HOME")))
    errx(EXIT_FAILURE, "Missing HOME environment variable");

  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  if (-1 == chdir(home)) err(EXIT_FAILURE, "Unable to chdir to '%s'", home);

  reload_config();

  x_connect();

  x_process_events();
}
