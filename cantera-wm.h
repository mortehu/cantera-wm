#ifndef CANTERA_WM_H_
#define CANTERA_WM_H_ 1

#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#undef ScreenCount

#include <memory>
#include <set>
#include <vector>

namespace cantera_wm {

extern const size_t kWorkspaceCount;

extern Display* x_display;
extern int x_screen_index;
extern ::Screen* x_screen;
extern Visual* x_visual;
extern XRenderPictFormat* x_render_visual_format;
extern XVisualInfo* x_visual_info;
extern int x_visual_info_count;
extern ::Window x_root_window;

extern XIM x_im;
extern XIC x_ic;
extern int x_damage_eventbase;
extern int x_damage_errorbase;

class Session;

extern Session current_session;

enum window_type {
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

class Rectangle : public XRectangle {
 public:
  void union_rect(const Rectangle& other) {
    if (x > other.x) x = other.x;
    if (y > other.y) y = other.y;
    if (x + width < other.x + other.width) width = (other.x + other.width) - x;
    if (y + height < other.y + other.width)
      height = (other.y + other.width) - y;
  }
};

class Window {
 public:
  Window();
  ~Window();

  window_type type;

  ::Window x_window;
  Picture x_picture;
  Damage x_damage;

  ::Window x_transient_for;

  struct Rectangle position;
  struct Rectangle real_position;

  void get_hints();
  void constrain_size();

  void init_composite();
  void reset_composite();

  void show();
  void hide();

  bool override_redirect;

 private:
  Window(const Window& rhs);
  Window& operator=(const Window& rhs);
};

typedef std::vector<Window*> workspace;

class Screen {
 public:
  Screen() : x_damage_region(0), active_workspace(0) {}

  void paint();

  void update_focus(unsigned int workspace_index, Time x_event_time);

  std::vector<Window*> ancillary_windows;

  ::Window x_window;
  Picture x_picture;
  Picture x_buffer;

  XserverRegion x_damage_region;

  Rectangle geometry;

  workspace workspaces[24];
  unsigned int active_workspace;

  std::vector<Picture> resize_buffers;
  XTransform initial_transform;

  std::vector<unsigned int> navigation_stack;
};

class Session {
 public:
  Session();

  void ProcessXCreateWindowEvent(const XCreateWindowEvent& cwe);

  void SetDirty() { repaint_all_ = true; }
  void Paint();

  Screen* find_screen_for_window(::Window x_window);

  Window* find_x_window(::Window x_window, workspace** workspace_ret = NULL,
                        Screen* *screen_ret = NULL);
  void remove_x_window(::Window x_window);
  void move_window(Window*, Screen* scre, workspace* ws);

  void SetDesktopGeometry(const Rectangle& geometry) {
    desktop_geometry_ = geometry;
  }

  void AddScreen(const Screen& new_screen) {
    if (screens_.empty())
      desktop_geometry_ = new_screen.geometry;
    else
      desktop_geometry_.union_rect(new_screen.geometry);

    screens_.push_back(new_screen);
  }

  size_t ScreenCount() { return screens_.size(); }
  Screen* GetScreen(size_t i) { return &screens_[i]; }
  Screen* ActiveScreen() { return &screens_[active_screen_]; }
  size_t ActiveScreenIndex() const { return active_screen_; }
  void SetActiveScreen(size_t i) { active_screen_ = i; }

  void AddInternalXWindow(::Window window) {
    internal_x_windows_.insert(window);
  }

  bool WindowIsInternal(::Window window) {
    return internal_x_windows_.count(window) > 0;
  }

  void ShowMenu() { showing_menu_ = true; }
  void HideMenu() { showing_menu_ = false; }

  int Top() { return desktop_geometry_.y; }
  int Right() { return desktop_geometry_.x + desktop_geometry_.width; }
  int Down() { return desktop_geometry_.y + desktop_geometry_.height; }
  int Left() { return desktop_geometry_.x; }

 private:
  Rectangle desktop_geometry_;

  std::vector<Screen> screens_;
  unsigned int active_screen_;

  std::vector<Window*> unpositioned_windows_;

  std::set< ::Window> internal_x_windows_;

  bool showing_menu_;
  bool repaint_all_;
};

} /* namespace cantera_wm */

#endif /* !CANTERA_WM_H_ */
