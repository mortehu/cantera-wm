#ifndef XA_H_
#define XA_H_ 1

#include <string>

#include <X11/Xdefs.h>

namespace xa {

extern Atom net_active_window;

extern Atom net_wm_window_type;

extern Atom net_wm_window_type_desktop;
extern Atom net_wm_window_type_dock;
extern Atom net_wm_window_type_toolbar;
extern Atom net_wm_window_type_menu;
extern Atom net_wm_window_type_utility;
extern Atom net_wm_window_type_splash;
extern Atom net_wm_window_type_dialog;
extern Atom net_wm_window_type_normal;

extern Atom wm_delete_window;
extern Atom wm_protocols;
extern Atom wm_state;

}  // namespace xa

namespace cantera_wm {

std::string GetAtomName(Atom atom);

}  // namespace cantera_wm

#endif  // !XA_H_
