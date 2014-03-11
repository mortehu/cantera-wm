#include "cantera-wm.h"

#include <algorithm>
#include <cstdio>

#include <X11/extensions/Xcomposite.h>

#include "menu.h"

namespace cantera_wm {

Session::Session()
    : active_screen_(0), showing_menu_(false), repaint_all_(true) {}

void Session::ProcessXCreateWindowEvent(const XCreateWindowEvent& cwe) {
  if (WindowIsInternal(cwe.window)) return;

  cantera_wm::Window* new_window = new cantera_wm::Window;

  new_window->x_window = cwe.window;
  new_window->position.x = cwe.x;
  new_window->position.y = cwe.y;
  new_window->position.width = cwe.width;
  new_window->position.height = cwe.height;

  new_window->real_position = new_window->position;

  if (cwe.override_redirect) {
    XCompositeUnredirectWindow(x_display, cwe.window, CompositeRedirectManual);
    new_window->override_redirect = true;
  }

  unpositioned_windows_.push_back(new_window);
}

void Session::Paint() {
  for (cantera_wm::Screen& screen : current_session.screens_) {
    XRenderColor black;
    bool draw_menu;

    draw_menu =
        showing_menu_ || screen.workspaces[screen.active_workspace].empty();

    if (draw_menu || current_session.repaint_all_)
      XFixesSetPictureClipRegion(x_display, screen.x_buffer, 0, 0, None);
    else {
      if (!screen.x_damage_region) continue;

#if 0
      /* XXX: Buggy on dual monitors, except for the monitor at offset 0,0 */

      XFixesSetPictureClipRegion(x_display, screen.x_buffer, 0, 0,
                                 screen.x_damage_region);
#endif
    }

    black.red = 0x0000;
    black.green = 0x0000;
    black.blue = 0x0000;
    black.alpha = 0xffff;

    XRenderFillRectangle(x_display, PictOpSrc, screen.x_buffer, &black, 0, 0,
                         screen.geometry.width, screen.geometry.height);

    for (auto& window : screen.ancillary_windows) {
      if (!window->x_picture) continue;

      XRenderComposite(
          x_display, PictOpSrc, window->x_picture, None, screen.x_buffer, 0, 0,
          0, 0, window->real_position.x - screen.geometry.x,
          window->real_position.y - screen.geometry.y,
          window->real_position.width, window->real_position.height);
    }

    for (auto& window : screen.workspaces[screen.active_workspace]) {
      if (!window->x_picture) continue;

      XRenderComposite(
          x_display, PictOpSrc, window->x_picture, None, screen.x_buffer, 0, 0,
          0, 0, window->real_position.x - screen.geometry.x,
          window->real_position.y - screen.geometry.y,
          window->real_position.width, window->real_position.height);
    }

    if (draw_menu) menu_draw(screen);

    XRenderComposite(x_display, PictOpSrc, screen.x_buffer, None,
                     screen.x_picture, 0, 0, 0, 0, 0, 0, screen.geometry.width,
                     screen.geometry.height);

    if (screen.x_damage_region) {
      XFixesDestroyRegion(x_display, screen.x_damage_region);
      screen.x_damage_region = 0;
    }
  }

  current_session.repaint_all_ = false;
}

cantera_wm::Screen* Session::find_screen_for_window(::Window x_window) {
  for (auto& screen : screens_) {
    if (screen.x_window == x_window) return &screen;
  }

  return NULL;
}

cantera_wm::Window* Session::find_x_window(::Window x_window,
                                           workspace** workspace_ret,
                                           cantera_wm::Screen** screen_ret) {
  for (auto& window : unpositioned_windows_) {
    if (window->x_window == x_window) {
      if (workspace_ret) *workspace_ret = NULL;

      if (screen_ret) *screen_ret = NULL;

      return window;
    }
  }

  for (auto& screen : screens_) {
    for (auto& window : screen.ancillary_windows) {
      if (window->x_window == x_window) {
        if (workspace_ret) *workspace_ret = NULL;

        if (screen_ret) *screen_ret = &screen;

        return window;
      }
    }

    for (auto& workspace : screen.workspaces) {
      for (auto& window : workspace) {
        if (window->x_window == x_window) {
          if (workspace_ret) *workspace_ret = &workspace;

          if (screen_ret) *screen_ret = &screen;

          return window;
        }
      }
    }
  }

  return NULL;
}

void Session::remove_x_window(::Window x_window) {
  fprintf(stderr, "Window %08lx was destroyed\n", x_window);

  current_session.repaint_all_ = true;

  auto predicate = [x_window](cantera_wm::Window* window)->bool {
    return window->x_window == x_window;
  };

  auto i = std::find_if(unpositioned_windows_.begin(),
                        unpositioned_windows_.end(), predicate);

  if (i != unpositioned_windows_.end()) {
    fprintf(stderr, " -> It was unpositioned\n");
    delete* i;

    unpositioned_windows_.erase(i);

    return;
  }

  unsigned int screen_index = 0;

  for (auto& screen : screens_) {
    auto i = std::find_if(screen.ancillary_windows.begin(),
                          screen.ancillary_windows.end(), predicate);

    if (i != screen.ancillary_windows.end()) {
      fprintf(stderr, " -> It was an ancillary window\n");
      delete* i;

      screen.ancillary_windows.erase(i);

      return;
    }

    unsigned int workspace_index = 0;

    for (auto& workspace : screen.workspaces) {
      auto i = std::find_if(workspace.begin(), workspace.end(), predicate);

      if (i != workspace.end()) {
        fprintf(stderr, " -> It was in a workspace\n");
        delete* i;

        workspace.erase(i);

        if (workspace.empty()) {
          auto& navstack = screen.navigation_stack;

          navstack.erase(
              std::remove(navstack.begin(), navstack.end(), workspace_index),
              navstack.end());

          if (screen.active_workspace == workspace_index && !navstack.empty()) {
            current_session.screens_[screen_index]
                .update_focus(navstack.back(), CurrentTime);
          }
        }

        return;
      }

      ++workspace_index;
    }

    ++screen_index;
  }
}

void Session::move_window(cantera_wm::Window* w, cantera_wm::Screen* scr,
                          workspace* ws) {
  /* XXX: Eliminate code duplication from remove_x_window */

  auto predicate = [w](cantera_wm::Window* arg)->bool { return w == arg; };

  auto i = std::find_if(unpositioned_windows_.begin(),
                        unpositioned_windows_.end(), predicate);

  if (i != unpositioned_windows_.end()) {
    unpositioned_windows_.erase(i);

    goto found;
  }

  for (auto& screen : screens_) {
    auto i = std::find_if(screen.ancillary_windows.begin(),
                          screen.ancillary_windows.end(), predicate);

    if (i != screen.ancillary_windows.end()) {
      screen.ancillary_windows.erase(i);

      goto found;
    }

    for (auto& workspace : screen.workspaces) {
      auto i = std::find_if(workspace.begin(), workspace.end(), predicate);

      if (i != workspace.end()) {
        workspace.erase(i);

        goto found;
      }
    }
  }

found:

  if (ws)
    ws->push_back(w);
  else
    scr->ancillary_windows.push_back(w);
}

}  // namespace cantera_wm
