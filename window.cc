#include "cantera-wm.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <err.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xatom.h>

#include "xa.h"

namespace {

int x_error_discarder(Display* display, XErrorEvent* error) { return 0; }

}  // namespace

namespace cantera_wm {

const char* Window::StringFromType(WindowType type) {
  switch (type) {
    case window_type_desktop:
      return "DESKTOP";
    case window_type_dock:
      return "DOCK";
    case window_type_toolbar:
      return "TOOLBAR";
    case window_type_menu:
      return "MENU";
    case window_type_utility:
      return "UTILITY";
    case window_type_splash:
      return "SPLASH";
    case window_type_dialog:
      return "DIALOG";
    case window_type_normal:
      return "NORMAL";
    default:
    case window_type_unknown:
      return "UNKNOWN";
  }
}

Window::Window() { type = window_type_unknown; }

Window::~Window() {}

void Window::GetName() {
  name_.clear();

  XTextProperty text_prop;
  auto status = XGetWMName(x_display, x_window, &text_prop);
  if (!status || !text_prop.value || text_prop.nitems < 1) return;

  char** list;
  int num;
  status = Xutf8TextPropertyToTextList(x_display, &text_prop, &list, &num);
  if (status < 0 || num < 1 || !*list) return;

  name_ = list[0];

  XFree(text_prop.value);
  XFreeStringList(list);
}

void Window::GetWMHints() {
  if (auto wm_hints = XGetWMHints(x_display, x_window)) {
    if (wm_hints->flags & InputHint) accepts_input_ = wm_hints->input;

    XFree(wm_hints);
  }
}

void Window::GetHints() {
  if (type != window_type_unknown) return;

  XSync(x_display, False);
  auto old_error_handler = XSetErrorHandler(x_error_discarder);

  Atom atom_type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned long* prop;

  /* XXX: This code has not been verified.  Also, we should use atom_type for
   * something  */
  if (Success !=
      XGetWindowProperty(x_display, x_window, xa::net_wm_window_type, 0, 1024,
                         False, XA_ATOM, &atom_type, &format, &nitems,
                         &bytes_after, (unsigned char**)&prop))
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

  XFree(prop);

  XGetTransientForHint(x_display, x_window, &x_transient_for);

  XSync(x_display, False);
  XSetErrorHandler(old_error_handler);

  if (x_transient_for && type == window_type_normal) type = window_type_dialog;
}

void Window::ReadProperties() {
  int num_properties = 0;
  std::unique_ptr<Atom[], decltype(&XFree)> properties(
      XListProperties(x_display, x_window, &num_properties), XFree);
  properties_.assign(properties.get(), properties.get() + num_properties);
}

void Window::constrain_size() {}

void Window::init_composite() {
  if (x_picture) {
    assert(x_damage);

    return;
  }

  if (!override_redirect) {
    XWindowAttributes attr;
    XRenderPictFormat* format;
    XRenderPictureAttributes picture_attributes;

    XGetWindowAttributes(x_display, x_window, &attr);

    format = XRenderFindVisualFormat(x_display, attr.visual);

    if (!format) errx(EXIT_FAILURE, "Unable to find visual format for window");

    memset(&picture_attributes, 0, sizeof(picture_attributes));
    picture_attributes.subwindow_mode = IncludeInferiors;

    x_picture = XRenderCreatePicture(x_display, x_window, format,
                                     CPSubwindowMode, &picture_attributes);

    XRenderColor black;
    black.red = 0x0000;
    black.green = 0x0000;
    black.blue = 0x0000;
    black.alpha = 0xffff;

    XRenderFillRectangle(x_display, PictOpSrc, x_picture, &black, 0, 0,
                         position.width, position.height);
  }

  x_damage = XDamageCreate(x_display, x_window, XDamageReportNonEmpty);

  fprintf(stderr, "Window %08lx has picture %08lx and damage %08lx\n", x_window,
          x_picture, x_damage);
}

void Window::reset_composite() {
  /* XXX: It seems these are always already destroyed? */

  if (x_damage) {
    XDamageDestroy(x_display, x_damage);
    x_damage = 0;
  }

  if (x_picture) {
    XRenderFreePicture(x_display, x_picture);
    x_picture = 0;
  }
}

void Window::show() {
  /* Restore the normal input region first … */
  XFixesSetWindowShapeRegion(x_display, x_window,
                             ShapeInput, 0, 0, None);

  /* … then put it back where it belongs. */
  XMoveResizeWindow(x_display, x_window,
                    position.x, position.y,
                    position.width, position.height);
}

void Window::hide() {
  /* Make the window non‑interactive but keep it mapped so the
     compositor still receives damaged pixels for the thumbnail.      */
  XserverRegion empty = XFixesCreateRegion(x_display, nullptr, 0);
  XFixesSetWindowShapeRegion(x_display, x_window,
                             ShapeInput, 0, 0, empty);
  XFixesDestroyRegion(x_display, empty);

  /* Move it off‑screen as before (purely cosmetic now).              */
  XMoveWindow(x_display, x_window, current_session.Right(), position.y);
}

}  // namespace cantera_wm
