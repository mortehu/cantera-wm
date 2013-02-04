#ifndef CANTERA_WM_H_
#define CANTERA_WM_H_ 1

#include <memory>
#include <set>
#include <vector>


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

struct rect
{
  int x, y;
  unsigned int width, height;
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

  void get_hints ();
  void constrain_size ();

  void init_composite ();
  void reset_composite ();

  void show ();

private:
  window (const window &rhs);
  window& operator=(const window &rhs);
};

typedef std::vector<window *> workspace;

struct screen
{
  screen ();

  void paint ();

  Window x_window;
  Picture x_picture;
  Picture x_buffer;

  XserverRegion x_damage_region;

  XineramaScreenInfo geometry;
  workspace workspaces[24];

  unsigned int active_workspace;
};

struct session
{
  std::vector<screen> screens;
  std::vector<window *> unpositioned_windows;

  std::set<Window> internal_x_windows;

  screen *find_screen_for_window (Window x_window);

  window *find_x_window (Window x_window,
                         workspace **workspace_ret = NULL,
                         screen **screen_ret = NULL);
  void remove_x_window (Window x_window);
  void move_window (window *, screen *scre, workspace *ws);
};

#endif /* !CANTERA_WM_H_ */
