#include "xa.h"

#include <memory>
#include <unordered_map>

#include <X11/Xlib.h>

#include "cantera-wm.h"

namespace xa {

Atom net_wm_window_type;

Atom net_wm_window_type_desktop;
Atom net_wm_window_type_dock;
Atom net_wm_window_type_toolbar;
Atom net_wm_window_type_menu;
Atom net_wm_window_type_utility;
Atom net_wm_window_type_splash;
Atom net_wm_window_type_dialog;
Atom net_wm_window_type_normal;

Atom wm_delete_window;
Atom wm_protocols;
Atom wm_state;

}  // namespace xa

namespace cantera_wm {

namespace {

std::unordered_map<Atom, std::string> atom_names;

}  // namespace

std::string GetAtomName(Atom atom) {
  auto i = atom_names.find(atom);
  if (i != atom_names.end()) return i->second;

  std::unique_ptr<char[], decltype(&XFree)> str(XGetAtomName(x_display, atom),
                                                XFree);

  if (!str) return std::string();

  auto& result = atom_names[atom];
  result = str.get();

  return result;
}

}  // namespace cantera_wm
