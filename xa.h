#ifndef XA_H_
#define XA_H_ 1

#include <X11/Xdefs.h>

namespace xa {

extern Atom net_wm_window_type;

extern Atom net_wm_window_type_desktop;
extern Atom net_wm_window_type_dock;
extern Atom net_wm_window_type_toolbar;
extern Atom net_wm_window_type_menu;
extern Atom net_wm_window_type_utility;
extern Atom net_wm_window_type_splash;
extern Atom net_wm_window_type_dialog;
extern Atom net_wm_window_type_normal;

extern Atom wm_protocols;
extern Atom wm_delete_window;

}  // namespace xa

#endif  // !XA_H_
