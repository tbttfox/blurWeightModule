from Qt import QtCore, QtGui

_default_hotkeys = {
    "smooth_key": QtCore.Qt.Key_Shift,
    "remove_key": QtCore.Qt.Key_Control,
    "exit_key": QtCore.Qt.Key_Escape,
    "solo_key": QtCore.Qt.Key_S,
    "mirror_key": QtCore.Qt.Key_M,
    "set_orbit_pos_key": QtCore.Qt.Key_F,
    "solo_opaque_key": QtCore.Qt.Key_A,
    # +Alt: pick MAX influence
    "pick_influence_key": QtCore.Qt.Key_D,
    # ALT KEYS
    "toggle_wireframe_key": QtCore.Qt.Key_W,
    "toggle_xray_key": QtCore.Qt.Key_X,
    # Handled specially
    "marking_menu_key": QtCore.Qt.Key_U,
}


class HOTKEY_CLASS:
    def __init__(self, hotkeyDict):
        self.updateHotkeys(hotkeyDict)

    def _key_to_string(self, key):
        if key == QtCore.Qt.Key_Control:
            return "Ctrl"
        elif key == QtCore.Qt.Key_Shift:
            return "Shift"
        return QtGui.QKeySequence(key).toString()

    def updateHotkeys(self, hotkeyDict):
        dhCopy = dict(_default_hotkeys)
        dhCopy.update(hotkeyDict)

        self.SMOOTH_KEY = dhCopy["smooth_key"]
        self.REMOVE_KEY = dhCopy["remove_key"]

        # Regular keys
        self.EXIT_KEY = dhCopy["exit_key"]
        self.SOLO_KEY = dhCopy["solo_key"]
        self.MIRROR_KEY = dhCopy["mirror_key"]
        self.SET_ORBIT_POS_KEY = dhCopy["set_orbit_pos_key"]
        self.SOLO_OPAQUE_KEY = dhCopy["solo_opaque_key"]

        # +Alt = pick MAX influence
        self.PICK_INFLUENCE_KEY = dhCopy["pick_influence_key"]

        # ALT KEYS
        self.TOGGLE_WIREFRAME_KEY = dhCopy["toggle_wireframe_key"]
        self.TOGGLE_XRAY_KEY = dhCopy["toggle_xray_key"]

        # Handled specially
        self.MARKING_MENU_KEY = dhCopy["marking_menu_key"]

    def buildHotkeyList(self):
        return [
            ("Remove", self._key_to_string(self.REMOVE_KEY) + " LMB"),
            ("Smooth", self._key_to_string(self.SMOOTH_KEY) + " LMB"),
            ("Sharpen", "Ctrl + Shift + LMB"),
            ("Size", "MMB left right"),
            ("Strength", "MMB up down"),
            ("Fine Strength Size", "Ctrl + MMB"),
            ("Marking Menu ", self._key_to_string(self.MARKING_MENU_KEY)),
            ("Pick Influence", self._key_to_string(self.PICK_INFLUENCE_KEY)),
            ("Pick Vertex ", "ALT + " + self._key_to_string(self.PICK_INFLUENCE_KEY)),
            ("Toggle Mirror Mode", "ALT + " + self._key_to_string(self.MIRROR_KEY)),
            ("Toggle Solo Mode", "ALT + " + self._key_to_string(self.SOLO_KEY)),
            ("Toggle Solo Opaque", "ALT + " + self._key_to_string(self.SOLO_OPAQUE_KEY)),
            ("Toggle Wireframe", "ALT + " + self._key_to_string(self.TOGGLE_WIREFRAME_KEY)),
            ("Toggle Xray", "ALT + " + self._key_to_string(self.TOGGLE_XRAY_KEY)),
            ("Orbit Center To", self._key_to_string(self.SET_ORBIT_POS_KEY)),
            ("Undo", "CTRL + Z"),
            ("Quit", self._key_to_string(self.EXIT_KEY)),
        ]


HOTKEYS = HOTKEY_CLASS({})
