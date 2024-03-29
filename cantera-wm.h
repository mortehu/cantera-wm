#ifndef CANTERA_WM_H_
#define CANTERA_WM_H_ 1

#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#undef ScreenCount

#include <memory>
#include <set>
#include <string>
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

class Rectangle : public XRectangle {
 public:
  Rectangle() {
    x = 0;
    y = 0;
    width = 0;
    height = 0;
  }

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
  enum WindowType {
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

  static const char* StringFromType(WindowType type);

  Window();
  ~Window();

  Window(const Window& rhs) = delete;
  Window& operator=(const Window& rhs) = delete;

  void GetWMHints();
  void GetHints();
  void ReadProperties();
  void constrain_size();

  void init_composite();
  void reset_composite();

  void show();
  void hide();

  void GetName();

  bool AcceptsInput() const { return accepts_input_; }

  WindowType Type() const { return type; }

  const std::vector<Atom> Properties() const { return properties_; }

  std::string Description() const {
    std::string result;

    if (!name_.empty()) {
      result += "'";
      result += name_;
      result += "' ";
    }

    char tmp[32];
    sprintf(tmp, "(0x%lx)", x_window);
    result += tmp;

    return result;
  }

  // TODO(mortehu): Make these private

  bool override_redirect = false;

  WindowType type = window_type_unknown;

  ::Window x_window = 0;
  Picture x_picture = 0;
  Damage x_damage = 0;

  ::Window x_transient_for = 0;

  struct Rectangle position;
  struct Rectangle real_position;

 private:
  std::vector<Atom> properties_;

  std::string name_;

  bool accepts_input_ = true;
};

typedef std::vector<Window*> workspace;

class Screen {
 public:
  Screen() : x_damage_region(0), active_workspace(0) {}

  void paint();

  void UpdateFocus(unsigned int workspace_index, Time x_event_time);

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
  void ProcessXCreateWindowEvent(const XCreateWindowEvent& cwe);

  void SetDirty() { repaint_all_ = true; }
  void SetDamaged() { repaint_some_ = true; }
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

  void ShowMenu() {
    showing_menu_ = true;
    SetDirty();
  }
  void HideMenu() {
    showing_menu_ = false;
    SetDirty();
  }

  int Top() { return desktop_geometry_.y; }
  int Right() { return desktop_geometry_.x + desktop_geometry_.width; }
  int Down() { return desktop_geometry_.y + desktop_geometry_.height; }
  int Left() { return desktop_geometry_.x; }

  bool Dirty() const { return repaint_all_ || repaint_some_; }

 private:
  Rectangle desktop_geometry_;

  std::vector<Screen> screens_;
  unsigned int active_screen_ = 0;

  std::vector<Window*> unpositioned_windows_;

  std::set< ::Window> internal_x_windows_;

  bool showing_menu_ = false;
  bool repaint_all_ = true;
  bool repaint_some_ = false;
};

} /* namespace cantera_wm */

#endif /* !CANTERA_WM_H_ */
