#include "cantera-wm.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <err.h>

#include "xa.h"

namespace {

int x_error_discarder(Display *display, XErrorEvent *error) { return 0; }

}  // namespace

namespace cantera_wm {

window::window() {
  memset(this, 0, sizeof(*this));

  type = window_type_unknown;
}

window::~window() {}

void window::get_hints() {
  if (type != window_type_unknown) return;

  XSync(x_display, False);
  auto old_error_handler = XSetErrorHandler(x_error_discarder);

  Atom atom_type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned long *prop;

  /* XXX: This code has not been verified.  Also, we should use atom_type for
   * something  */
  if (Success !=
      XGetWindowProperty(x_display, x_window, xa::net_wm_window_type, 0, 1024,
                         False, XA_ATOM, &atom_type, &format, &nitems,
                         &bytes_after, (unsigned char **)&prop))
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

  if (x_transient_for && type == window_type_normal)
    type = window_type_dialog;
}

void window::constrain_size() {}

void window::init_composite() {
  if (x_picture) {
    assert(x_damage);

    return;
  }

  if (!override_redirect) {
    XWindowAttributes attr;
    XRenderPictFormat *format;
    XRenderPictureAttributes picture_attributes;

    XGetWindowAttributes(x_display, x_window, &attr);

    format = XRenderFindVisualFormat(x_display, attr.visual);

    if (!format) errx(EXIT_FAILURE, "Unable to find visual format for window");

    memset(&picture_attributes, 0, sizeof(picture_attributes));
    picture_attributes.subwindow_mode = IncludeInferiors;

    x_picture = XRenderCreatePicture(x_display, x_window, format,
                                     CPSubwindowMode, &picture_attributes);
  }

  x_damage = XDamageCreate(x_display, x_window, XDamageReportNonEmpty);

  fprintf(stderr, "Window %08lx has picture %08lx and damage %08lx\n", x_window,
          x_picture, x_damage);
}

void window::reset_composite() {
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

void window::show() {
  XWindowChanges wc;

  wc.x = position.x;
  wc.y = position.y;
  wc.width = position.width;
  wc.height = position.height;

  /* XXX: Quench this if the position is not dirty */
  XConfigureWindow(x_display, x_window, CWX | CWY | CWWidth | CWHeight, &wc);
}

void window::hide() {
  XWindowChanges wc;

  wc.x = current_session.desktop_geometry.x +
         current_session.desktop_geometry.width;

  XConfigureWindow(x_display, x_window, CWX, &wc);
}

}
