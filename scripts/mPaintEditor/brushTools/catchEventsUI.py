from __future__ import print_function
from __future__ import absolute_import

from Qt import QtGui, QtCore, QtWidgets, QtCompat

from maya import OpenMayaUI, cmds, mel
import time
from .brushPythonFunctions import (
    callPaintEditorFunction,
    escapePressed,
    toggleSoloMode,
    disableUndoContext,
)
from . import meshFnIntersection
from past.builtins import long

EVENTCATCHER = None
ROOTWINDOW = None

MM_NAME = "skinBrush_MM"


def callMarkingMenu():
    if cmds.popupMenu(MM_NAME, exists=True):
        cmds.deleteUI(MM_NAME)

    res = mel.eval("findPanelPopupParent")
    cmds.popupMenu(
        MM_NAME,
        button=1,
        ctrlModifier=False,
        altModifier=False,
        allowOptionBoxes=True,
        parent=res,
        markingMenu=True,
    )

    kwArgs = {
        "label": "add",
        "divider": False,
        "subMenu": False,
        "tearOff": False,
        "optionBox": False,
        "enable": True,
        "data": 0,
        "allowOptionBoxes": True,
        "postMenuCommandOnce": False,
        "enableCommandRepeat": True,
        "echoCommand": False,
        "italicized": False,
        "boldFont": True,
        "sourceType": "mel",
        "longDivider": True,
    }
    # 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7 UnLockVertices

    lstCommands = [
        ("add", "N", "add", 0),
        ("remove", "S", "rmv", 1),
        ("addPercent", "NW", "addPerc", 2),
        ("sharpen", "SW", "sharpen", 5),
        ("absolute", "NE", "abs", 3),
        ("smooth", "W", "smooth", 4),
        ("locks Verts", "E", "locks", 6),
        ("Unlocks Verts", "SE", "unLocks", 7),
    ]

    for ind, (txt, posi, btn, cmdInd) in enumerate(lstCommands):
        kwArgs["radialPosition"] = posi
        kwArgs["label"] = txt
        kwArgs["command"] = """\
            brSkinBrushContext -edit -commandIndex {0} `currentCtx`;
            python("import mPaintEditor;mPaintEditor.PAINT_EDITOR.{1}_btn.click()");
            """.format(cmdInd, btn)
        cmds.menuItem("menuEditorMenuItem{0}".format(ind + 1), **kwArgs)
    kwArgs.pop("radialPosition", None)
    kwArgs["label"] = "solo color"
    kwArgs["subMenu"] = True

    cmds.menuItem("menuEditorMenuItem{0}".format(len(lstCommands) + 1), **kwArgs)
    kwArgs["subMenu"] = False
    for ind, colType in enumerate(["white", "lava", "influence"]):
        kwArgs["label"] = colType
        kwArgs["command"] = """\
            python("import mPaintEditor;mPaintEditor.PAINT_EDITOR.updateSoloColor({0})");
            brSkinBrushContext -edit -soloColorType {0} `currentCtx`;
            """.format(ind)

        cmds.menuItem("menuEditorMenuItemCol{0}".format(ind + 1), **kwArgs)

    mel.eval("setParent -menu ..;")
    mel.eval("setParent -menu ..;")


SMOOTH_KEY = QtCore.Qt.Key_Control
REMOVE_KEY = QtCore.Qt.Key_Shift

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


class CatchEventsWidget(QtCore.QObject):
    # Custom event filter to catch rightclicks
    verbose = False
    filterInstalled = False
    displayLabel = None
    eventFilterWidgetReceiver = None
    lstButtons = [
        "brSkinBrushAddRb",
        "brSkinBrushRemoveRb",
        "brSkinBrushAddPercentRb",
        "brSkinBrushAbsoluteRb",
        "brSkinBrushSmoothRb",
        "brSkinBrushSharpenRb",
        "brSkinBrushLockVerticesRb",
        "brSkinBrushUnLockVerticesRb",
    ]

    def __init__(self):
        super(CatchEventsWidget, self).__init__(ROOTWINDOW)
        self.QApplicationInstance = QtWidgets.QApplication.instance()

        self.setMask(QtGui.QRegion(0, 0, 1, 1))

        self.markingMenuKeyPressed = False
        self.markingMenuShown = False
        self.closingNextPressMarkingMenu = False
        self.removeKeyPressed = False
        self.smoothKeyPressed = False
        self.testWireFrame = True

        self.rootWin = ROOTWINDOW

        self.prevQtButton = "add"

        self.orbit = meshFnIntersection.Orbit()
        self.timeStampRunning = time.time()

        self.searchInfluencesPaintEditor = callPaintEditorFunction("searchInfluences_le")

    # ---------- GAMMA --------------------------------------
    restorePanels = []

    def setPanelsDisplayOn(self):
        self.restorePanels = []
        dicPanel = {"edit": True, "displayLights": "flat", "useDefaultMaterial": False}
        wireframeCB = callPaintEditorFunction("wireframe_cb")
        listModelEditorKeys = [
            "displayLights",
            "cmEnabled",
            "selectionHiliteDisplay",
            "wireframeOnShaded",
            "useDefaultMaterial",
        ]
        if not self.testWireFrame:
            if wireframeCB and wireframeCB.isChecked():
                dicPanel["wireframeOnShaded"] = False
        else:
            listModelEditorKeys.remove("wireframeOnShaded")

        for panel in cmds.getPanel(vis=True):
            if cmds.getPanel(to=panel) == "modelPanel":
                valDic = {}
                for key in listModelEditorKeys:
                    dic = {"query": True, key: True}
                    valDic[key] = cmds.modelEditor(panel, **dic)
                self.restorePanels.append((panel, valDic))
                cmds.modelEditor(panel, **dicPanel)
                # GAMMA ENABLED
                cmds.modelEditor(panel, edit=True, cmEnabled=False)

    def setPanelsDisplayOff(self):
        for panel, valDic in self.restorePanels:
            cmds.modelEditor(panel, edit=True, **valDic)

    # ---------- end GAMMA --------------------------------------

    def open(self):
        with disableUndoContext():
            if not self.filterInstalled:
                self.installFilters()
            self.setPanelsDisplayOn()

    def installFilters(self):
        self.eventFilterWidgetReceiver = [
            QtCompat.wrapInstance(
                long(OpenMayaUI.MQtUtil.findControl(el)),
                QtWidgets.QWidget,
            )
            for el in cmds.getPanel(type="modelPanel")
            if OpenMayaUI.MQtUtil.findControl(el) is not None
        ]

        self.filterInstalled = True
        self.QApplicationInstance.installEventFilter(self)

    def removeFilters(self):
        self.filterInstalled = False
        self.QApplicationInstance.removeEventFilter(self)

    def highlightBtns(self):
        btnQtToSelect = ""
        btnMayaToSelect = ""
        if self.smoothKeyPressed and self.removeKeyPressed:
            btnQtToSelect = "sharpen"
            btnMayaToSelect = "brSkinBrushSharpenRb"
        elif self.smoothKeyPressed:
            if self.prevButton == "brSkinBrushAddRb":
                btnMayaToSelect = "brSkinBrushSmoothRb"
            elif self.prevButton == "brSkinBrushLockVerticesRb":
                btnMayaToSelect = "brSkinBrushUnLockVerticesRb"
            else:
                btnMayaToSelect = self.prevButton
            if self.prevQtButton:
                if self.prevQtButton == "add":
                    btnQtToSelect = "smooth"
                elif self.prevQtButton == "locks":
                    btnQtToSelect = "unLocks"
                else:
                    btnQtToSelect = self.prevQtButton
        elif self.removeKeyPressed:
            btnQtToSelect = "rmv"
            btnMayaToSelect = "brSkinBrushRemoveRb"
        else:
            btnQtToSelect = self.prevQtButton
            btnMayaToSelect = self.prevButton
        callPaintEditorFunction("highlightBtn", btnQtToSelect)
        if cmds.radioButton(btnMayaToSelect, ex=True):
            cmds.radioButton(btnMayaToSelect, edit=True, select=True)

        if self.removeKeyPressed:
            value = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, smoothStrength=True)
        else:
            value = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, strength=True)

        callPaintEditorFunction("updateStrengthVal", value)
        try:
            cmds.floatSliderGrp("brSkinBrushStrength", edit=True, value=value)
        except Exception:
            pass

    def testRunOnce(self):
        currentStampTime = time.time()
        correctTime = (currentStampTime - self.timeStampRunning) > 0.5
        if correctTime:
            self.timeStampRunning = currentStampTime
        return correctTime

    def eventFilter(self, obj, event):
        """process is stopped when returning True
        keeps when returning False
        """
        # only for the marking menu always checked
        if self.markingMenuKeyPressed or self.markingMenuShown or self.closingNextPressMarkingMenu:
            if (
                event.type() in [QtCore.QEvent.MouseButtonPress, QtCore.QEvent.MouseButtonRelease]
                and event.modifiers() != QtCore.Qt.AltModifier
            ):
                if event.modifiers() == QtCore.Qt.NoModifier:  # regular click
                    if event.type() == QtCore.QEvent.MouseButtonPress:  # click
                        with disableUndoContext():
                            if self.markingMenuKeyPressed:
                                if not self.markingMenuShown:
                                    callMarkingMenu()
                                    self.markingMenuShown = True
                                    self.closingNextPressMarkingMenu = False
                                    # print "-- callMarkingMenu --"
                            elif self.closingNextPressMarkingMenu:
                                if cmds.popupMenu(MM_NAME, exists=True):
                                    cmds.deleteUI(MM_NAME)
                                self.markingMenuShown = False
                                self.markingMenuKeyPressed = False
                                self.closingNextPressMarkingMenu = False
                    elif event.type() == QtCore.QEvent.MouseButtonRelease:  # click release
                        if self.markingMenuShown:
                            self.closingNextPressMarkingMenu = True
                    return False
                return False

        if obj in self.eventFilterWidgetReceiver:
            # action on Release
            if event.type() == QtCore.QEvent.KeyRelease:
                if event.key() == REMOVE_KEY:
                    self.removeKeyPressed = False
                    with disableUndoContext():
                        self.highlightBtns()
                    return False
                elif event.key() == SMOOTH_KEY:
                    self.smoothKeyPressed = False
                    with disableUndoContext():
                        self.highlightBtns()
                    return False
                elif event.key() == MARKING_MENU_KEY:
                    if self.markingMenuKeyPressed:
                        self.markingMenuKeyPressed = False
                    return True

            # action on Press
            if event.type() == QtCore.QEvent.KeyPress:
                if event.key() == REMOVE_KEY:
                    if self.removeKeyPressed:  # already pressed
                        return False
                    if QtWidgets.QApplication.mouseButtons() == QtCore.Qt.NoButton:
                        self.removeKeyPressed = True
                        with disableUndoContext():
                            if not self.smoothKeyPressed:
                                self.prevButton = self.lstButtons[
                                    cmds.brSkinBrushContext(
                                        "brSkinBrushContext1",
                                        query=True,
                                        commandIndex=True,
                                    )
                                ]
                                self.prevQtButton = callPaintEditorFunction("getEnabledButton")
                            self.highlightBtns()
                        return False

                elif event.key() == SMOOTH_KEY:
                    if self.smoothKeyPressed:  # already pressed
                        return False
                    if QtWidgets.QApplication.mouseButtons() == QtCore.Qt.NoButton:
                        self.smoothKeyPressed = True
                        # print "custom SHIFT pressed"
                        with disableUndoContext():
                            if not self.removeKeyPressed:
                                self.prevButton = self.lstButtons[
                                    cmds.brSkinBrushContext(
                                        cmds.currentCtx(),
                                        query=True,
                                        commandIndex=True,
                                    )
                                ]
                                self.prevQtButton = callPaintEditorFunction("getEnabledButton")
                            self.highlightBtns()
                        return False

                elif event.key() == MARKING_MENU_KEY:
                    self.markingMenuKeyPressed = True
                    return True

                elif event.key() == QtCore.Qt.Key_Escape:
                    with disableUndoContext():
                        escapePressed()
                        mel.eval("setToolTo $gMove;")
                    return True

                elif event.key() == PICK_INFLUENCE_KEY:
                    with disableUndoContext():
                        if self.testRunOnce():
                            if event.modifiers() == QtCore.Qt.AltModifier:
                                cmds.brSkinBrushContext(
                                    cmds.currentCtx(), edit=True, pickMaxInfluence=1
                                )
                            else:
                                cmds.brSkinBrushContext(
                                    cmds.currentCtx(), edit=True, pickInfluence=1
                                )
                    return True

                elif event.key() == SET_ORBIT_POS_KEY:
                    with disableUndoContext():
                        if self.testRunOnce():
                            self.orbit.setOrbitPosi()
                    return True

                elif event.modifiers() == QtCore.Qt.AltModifier:
                    if event.key() == TOGGLE_XRAY_KEY:
                        with disableUndoContext():
                            listModelPanels = [
                                el
                                for el in cmds.getPanel(vis=True)
                                if cmds.getPanel(to=el) == "modelPanel"
                            ]
                            val = not cmds.modelEditor(
                                listModelPanels[0], query=True, jointXray=True
                            )
                            for pnel in listModelPanels:
                                cmds.modelEditor(pnel, edit=True, jointXray=val)
                        return True

                    if event.key() == TOGGLE_WIREFRAME_KEY:
                        with disableUndoContext():
                            if self.testRunOnce():
                                if cmds.objExists("SkinningWireframe"):
                                    vis = cmds.getAttr("SkinningWireframe.v")
                                    cmds.setAttr("SkinningWireframe.v", not vis)
                                else:
                                    listModelPanels = [
                                        el
                                        for el in cmds.getPanel(vis=True)
                                        if cmds.getPanel(to=el) == "modelPanel"
                                    ]
                                    val = not cmds.modelEditor(
                                        listModelPanels[0],
                                        query=True,
                                        wireframeOnShaded=True,
                                    )
                                    for pnel in listModelPanels:
                                        cmds.modelEditor(pnel, edit=True, wireframeOnShaded=val)
                        return True

                    if event.key() == SOLO_KEY:
                        with disableUndoContext():
                            if self.testRunOnce():
                                toggleSoloMode()
                        return True

                    if event.key() == SOLO_OPAQUE_KEY:
                        with disableUndoContext():
                            if self.testRunOnce():
                                soloOpaque = callPaintEditorFunction("soloOpaque_cb")
                                if soloOpaque:
                                    soloOpaque.toggle()
                                else:
                                    minColor = cmds.brSkinBrushContext(
                                        "brSkinBrushContext1", query=True, minColor=True
                                    )
                                    if minColor == 1.0:
                                        cmds.brSkinBrushContext(
                                            "brSkinBrushContext1",
                                            edit=True,
                                            minColor=0.0,
                                        )
                                    else:
                                        cmds.brSkinBrushContext(
                                            "brSkinBrushContext1",
                                            edit=True,
                                            minColor=1.0,
                                        )
                        return True

                    if event.key() == MIRROR_KEY:
                        with disableUndoContext():
                            if self.testRunOnce():
                                print("mirror active")
                                callPaintEditorFunction("mirrorActive_cb").toggle()
                        return True
        return False

    def closeEvent(self, e):
        """Make sure the eventFilter is removed"""
        self.close()
        return super(CatchEventsWidget, self).closeEvent(e)

    def close(self):
        with disableUndoContext():
            self.setPanelsDisplayOff()

            # remove the markingMenu
            self.markingMenuKeyPressed = False
            self.markingMenuShown = False
            self.closingNextPressMarkingMenu = False

            if cmds.popupMenu(MM_NAME, exists=True):
                cmds.deleteUI(MM_NAME)
            self.removeFilters()
