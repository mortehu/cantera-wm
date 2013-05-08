#ifndef CANTERA_WM_H_
#define CANTERA_WM_H_ 1

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include <X11/extensions/Xdamage.h>

#include <memory>
#include <set>
#include <vector>

namespace cantera_wm {

extern Display *x_display;
extern int x_screen_index;
extern Screen* x_screen;
extern Visual* x_visual;
extern XRenderPictFormat* x_render_visual_format;
extern XVisualInfo* x_visual_info;
extern int x_visual_info_count;
extern Window x_root_window;

extern XIM x_im;
extern XIC x_ic;
extern int x_damage_eventbase;
extern int x_damage_errorbase;

struct session;

extern session current_session;

enum window_type
{
  window_type_unknown,
  window_type_desktop,
  window_type_dock,
  window_type_toolbar,
  window_type_menu,
  window_type_utility,
  window_type_splash,
  window_type_dialog,
  window_type_normal
};

struct rect : XRectangle
{
  void union_rect (struct rect &other);
};

struct window
{
  window ();
  ~window ();

  window_type type;

  Window x_window;
  Picture x_picture;
  Damage x_damage;

  Window x_transient_for;

  struct rect position;
  struct rect real_position;

  void get_hints ();
  void constrain_size ();

  void init_composite ();
  void reset_composite ();

  void show ();
  void hide ();

  bool override_redirect;

private:
  window (const window &rhs);
  window& operator=(const window &rhs);
};

typedef std::vector<window *> workspace;

struct screen
{
  screen ();

  void paint ();

  std::vector<window *> ancillary_windows;

  Window x_window;
  Picture x_picture;
  Picture x_buffer;

  XserverRegion x_damage_region;

  struct rect geometry;

  workspace workspaces[24];
  unsigned int active_workspace;

  std::vector<Picture> resize_buffers;
  XTransform initial_transform;

  std::vector<unsigned int> navigation_stack;
};

struct session
{
  session();

  struct rect desktop_geometry;

  std::vector<screen> screens;
  unsigned int active_screen;

  std::vector<window *> unpositioned_windows;

  std::set<Window> internal_x_windows;

  bool repaint_all;

  screen *find_screen_for_window (Window x_window);

  window *find_x_window (Window x_window,
                         workspace **workspace_ret = NULL,
                         screen **screen_ret = NULL);
  void remove_x_window (Window x_window);
  void move_window (window *, screen *scre, workspace *ws);
};

} /* namespace cantera_wm */

#endif /* !CANTERA_WM_H_ */
