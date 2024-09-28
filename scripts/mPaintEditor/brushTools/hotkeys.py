from Qt import QtCore


class HOTKEYS:
    """A quick-n-dirty class to hold the hotkeys needed for this tool
    And eventually, we could update this with json
    """

    SMOOTH_KEY = QtCore.Qt.Key_Shift
    REMOVE_KEY = QtCore.Qt.Key_Control

    EXIT_KEY = QtCore.Qt.Key_Escape
    SOLO_KEY = QtCore.Qt.Key_S
    MIRROR_KEY = QtCore.Qt.Key_M
    SET_ORBIT_POS_KEY = QtCore.Qt.Key_F
    SOLO_OPAQUE_KEY = QtCore.Qt.Key_A

    # +Alt = pick MAX influence
    PICK_INFLUENCE_KEY = QtCore.Qt.Key_D

    # ALT KEYS
    TOGGLE_WIREFRAME_KEY = QtCore.Qt.Key_W
    TOGGLE_XRAY_KEY = QtCore.Qt.Key_X

    # Handled specially
    MARKING_MENU_KEY = QtCore.Qt.Key_U

    @classmethod
    def loadDict(cls, hotkeyDict):
        # Modifier keys
        cls.SMOOTH_KEY = hotkeyDict.get("smooth_key", QtCore.Qt.Key_Shift)
        cls.REMOVE_KEY = hotkeyDict.get("remove_key", QtCore.Qt.Key_Control)

        # Regular keys
        cls.EXIT_KEY = hotkeyDict.get("exit_key", QtCore.Qt.Key_Escape)
        cls.SOLO_KEY = hotkeyDict.get("solo_key", QtCore.Qt.Key_S)
        cls.MIRROR_KEY = hotkeyDict.get("mirror_key", QtCore.Qt.Key_M)
        cls.SET_ORBIT_POS_KEY = hotkeyDict.get("set_orbit_pos_key", QtCore.Qt.Key_F)
        cls.SOLO_OPAQUE_KEY = hotkeyDict.get("solo_opaque_key", QtCore.Qt.Key_A)

        # +Alt = pick MAX influence
        cls.PICK_INFLUENCE_KEY = hotkeyDict("pick_influence_key", QtCore.Qt.Key_D)

        # ALT KEYS
        cls.TOGGLE_WIREFRAME_KEY = hotkeyDict("toggle_wireframe_key", QtCore.Qt.Key_W)
        cls.TOGGLE_XRAY_KEY = hotkeyDict("toggle_xray_key", QtCore.Qt.Key_X)

        # Handled specially
        cls.MARKING_MENU_KEY = hotkeyDict("marking_menu_key", QtCore.Qt.Key_U)
