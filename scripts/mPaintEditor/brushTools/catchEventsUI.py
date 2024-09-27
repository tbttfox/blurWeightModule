from __future__ import print_function
from __future__ import absolute_import

from Qt import QtCore, QtWidgets, QtCompat

from maya import OpenMayaUI, cmds, mel
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


def markingMenuPaintEditorButtonClick(cmdInd, buttonName):
    cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, commandIndex=cmdInd)
    btn = callPaintEditorFunction(buttonName + "_btn")
    if btn:
        btn.click()


def markingMenuUpdateSoloColor(colorIdx):
    cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, soloColorType=colorIdx)
    callPaintEditorFunction("updateSoloColor", colorIdx)


def buttonClickBuilder(func, *args):
    """Build a button-click function for the marking menu"""
    return lambda: func(*args)


def callMarkingMenu(catcher):
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
        kwArgs["sourceType"] = "python"
        # kwArgs["command"] = buttonClickBuilder(markingMenuPaintEditorButtonClick, cmdInd, btn)

        # TODO: THIS
        # catcher.MarkingMenuButtonPressed.emit(cmdInd, btn)

        cmds.menuItem("menuEditorMenuItem{0}".format(ind + 1), **kwArgs)
    kwArgs.pop("radialPosition", None)
    kwArgs["label"] = "solo color"
    kwArgs["subMenu"] = True

    cmds.menuItem("menuEditorMenuItem{0}".format(len(lstCommands) + 1), **kwArgs)
    kwArgs["subMenu"] = False
    for ind, colType in enumerate(["white", "lava", "influence"]):
        kwArgs["label"] = colType
        kwArgs["sourceType"] = "python"
        # kwArgs["command"] = buttonClickBuilder(markingMenuUpdateSoloColor, ind)

        # TODO: THIS
        # catcher.MarkingMenuSoloColorChanged.emit(ind)

        cmds.menuItem("menuEditorMenuItemCol{0}".format(ind + 1), **kwArgs)

    cmds.setParent("..", menu=True)
    cmds.setParent("..", menu=True)


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


class HandleEventsQt:
    """Handle any events that will affect the QT ui"""

    def __init__(self, widget, catcher):
        self.widget = widget  # the Qt paint editor widget
        self.prevButton = "add"
        self.isSmoothKeyPressed = False
        self.isRemoveKeyPressed = False
        self.catcher = catcher

    def hookup(self):
        self.catcher.RemoveKeyReleased.connect(self.removeKeyReleased)
        self.catcher.SmoothKeyReleased.connect(self.smoothKeyReleased)
        self.catcher.RemoveKeyPressed.connect(self.removeKeyPressed)
        self.catcher.SmoothKeyPressed.connect(self.smoothKeyPressed)
        self.catcher.ExitKeyPressed.connect(self.exitKeyPressed)
        self.catcher.SoloModeKeyPressed.connect(self.soloModeKeyPressed)
        self.catcher.SoloOpaqueKeyPressed.connect(self.soloOpaqueKeyPressed)
        self.catcher.MirrorKeyPressed.connect(self.mirrorKeyPressed)

    def highlightBtns(self):
        btnToSelect = self.prevButton
        if self.isSmoothKeyPressed and self.isRemoveKeyPressed:
            btnToSelect = "sharpen"
        elif self.isSmoothKeyPressed:
            if self.prevButton:
                if self.prevButton == "add":
                    btnToSelect = "smooth"
                elif self.prevButton == "locks":
                    btnToSelect = "unLocks"
                else:
                    btnToSelect = self.prevButton
        elif self.isRemoveKeyPressed:
            btnToSelect = "rmv"

        if self.isRemoveKeyPressed:
            value = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, smoothStrength=True)
        else:
            value = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, strength=True)

        callPaintEditorFunction("highlightBtn", btnToSelect)
        callPaintEditorFunction("updateStrengthVal", value)

    def removeKeyPressed(self):
        self.isRemoveKeyPressed = True
        with disableUndoContext():
            if not self.isSmoothKeyPressed:
                self.prevQtButton = callPaintEditorFunction("getEnabledButton")
            self.highlightBtns()

    def smoothKeyPressed(self):
        self.isSmoothKeyPressed = True
        self.highlightBtns()

    def removeKeyReleased(self):
        self.isRemoveKeyPressed = False
        self.highlightBtns()

    def smoothKeyReleased(self):
        self.isSmoothKeyPressed = False
        self.highlightBtns()

    def exitKeyPressed(self):
        escapePressed()

    def soloOpaqueKeyPressed(self):
        soloOpaque = callPaintEditorFunction("soloOpaque_cb")
        if soloOpaque:
            soloOpaque.toggle()

    def mirrorKeyPressed(self):
        mirror = callPaintEditorFunction("mirrorActive_cb")
        if mirror:
            mirror.toggle()

    def soloModeKeyPressed(self):
        toggleSoloMode()


class HandleEventsMaya:
    """Handle any events that will affect Maya directly"""

    def __init__(self, catcher):
        self.orbit = meshFnIntersection.Orbit()
        self.testWireFrame = True
        self.restorePanels = []
        self.catcher = catcher
        self.hookup()

    def hookup(self):
        self.catcher.SetPanelDisplayOn.connect(self.setPanelsDisplayOn)
        self.catcher.SetPanelDisplayOff.connect(self.setPanelsDisplayOff)
        self.catcher.ShowMarkingMenu.connect(self.showMarkingMenu)
        self.catcher.HideMarkingMenu.connect(self.hideMarkingMenu)

        self.catcher.ExitKeyPressed.connect(self.exitKeyPressed)
        self.catcher.PickMaxInfluenceKeyPressed.connect(self.pickMaxInfluenceKeyPressed)
        self.catcher.PickInfluenceKeyPressed.connect(self.pickInfluenceKeyPressed)
        self.catcher.ToggleWireframeKeyPressed.connect(self.toggleWireframeKeyPressed)

        self.catcher.SetOrbitPosKeyPressed.connect(self.setOrbitKeyPressed)
        self.catcher.SetToggleXrayKeyPressed.connect(self.toggleXrayKeyPressed)

    @staticmethod
    def getModelPanels():
        return [el for el in cmds.getPanel(vis=True) if cmds.getPanel(to=el) == "modelPanel"]

    def toggleWireframeKeyPressed(self):
        if cmds.objExists("SkinningWireframe"):
            vis = cmds.getAttr("SkinningWireframe.v")
            cmds.setAttr("SkinningWireframe.v", not vis)
        else:
            listModelPanels = self.getModelPanels()
            val = not cmds.modelEditor(
                listModelPanels[0],
                query=True,
                wireframeOnShaded=True,
            )
            for pnel in listModelPanels:
                cmds.modelEditor(pnel, edit=True, wireframeOnShaded=val)

    def toggleXrayKeyPressed(self):
        listModelPanels = self.getModelPanels()
        val = not cmds.modelEditor(listModelPanels[0], query=True, jointXray=True)
        for pnel in listModelPanels:
            cmds.modelEditor(pnel, edit=True, jointXray=val)

    def setOrbitKeyPressed(self):
        self.orbit.setOrbitPosi()

    def pickInfluenceKeyPressed(self):
        cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, pickInfluence=1)

    def pickMaxInfluenceKeyPressed(self):
        cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, pickMaxInfluence=1)

    def exitKeyPressed(self):
        mel.eval("setToolTo $gMove;")

    def showMarkingMenu(self):
        if self.catcher is not None:
            callMarkingMenu(self.catcher)

    def hideMarkingMenu(self):
        if cmds.popupMenu(MM_NAME, exists=True):
            cmds.deleteUI(MM_NAME)

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

        for panel in self.getModelPanels():
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


class CatchEventsWidget(QtCore.QObject):
    """Custom QObject event filter so we can catch right-clicks

    Maya made the decision to *NOT* allow their normal contexts
    to interact with right clicks. To get around that, we have this
    class
    """

    SetPanelDisplayOn = QtCore.Signal()
    SetPanelDisplayOff = QtCore.Signal()
    ShowMarkingMenu = QtCore.Signal()
    HideMarkingMenu = QtCore.Signal()

    RemoveKeyReleased = QtCore.Signal()
    SmoothKeyReleased = QtCore.Signal()
    RemoveKeyPressed = QtCore.Signal()
    SmoothKeyPressed = QtCore.Signal()
    ExitKeyPressed = QtCore.Signal()
    PickMaxInfluenceKeyPressed = QtCore.Signal()
    PickInfluenceKeyPressed = QtCore.Signal()
    SetOrbitPosKeyPressed = QtCore.Signal()
    SetToggleXrayKeyPressed = QtCore.Signal()
    ToggleWireframeKeyPressed = QtCore.Signal()
    SoloModeKeyPressed = QtCore.Signal()
    SoloOpaqueKeyPressed = QtCore.Signal()
    MirrorKeyPressed = QtCore.Signal()

    def __init__(self):
        super(CatchEventsWidget, self).__init__(ROOTWINDOW)
        self.QApplicationInstance = QtWidgets.QApplication.instance()

        self.markingMenuKeyPressed = False
        self.markingMenuShown = False
        self.closingNextPressMarkingMenu = False
        self.isRemoveKeyPressed = False
        self.isSmoothKeyPressed = False
        self.filterInstalled = False
        self.eventFilterWidgetReceiver = None

        self.mayaEventHandler = HandleEventsMaya(self)

    def open(self):
        with disableUndoContext():
            if not self.filterInstalled:
                self.installFilters()
            self.Opening.emit()

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
                                    self.ShowMarkingMenu.emit()
                                    self.markingMenuShown = True
                                    self.closingNextPressMarkingMenu = False
                            elif self.closingNextPressMarkingMenu:
                                self.HideMarkingMenu.emit()
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
                    self.isRemoveKeyPressed = False
                    self.RemoveKeyReleased.emit()
                    return False
                elif event.key() == SMOOTH_KEY:
                    self.isSmoothKeyPressed = False
                    self.SmoothKeyReleased.emit()
                    return False
                elif event.key() == MARKING_MENU_KEY:
                    if self.markingMenuKeyPressed:
                        self.markingMenuKeyPressed = False
                    return True

            # action on Press
            elif event.type() == QtCore.QEvent.KeyPress:
                if event.key() == REMOVE_KEY:
                    if self.isRemoveKeyPressed:  # already pressed
                        return False
                    if QtWidgets.QApplication.mouseButtons() == QtCore.Qt.NoButton:
                        self.isRemoveKeyPressed = True
                        self.RemoveKeyPressed.emit()
                        return False

                elif event.key() == SMOOTH_KEY:
                    if self.isSmoothKeyPressed:  # already pressed
                        return False
                    if QtWidgets.QApplication.mouseButtons() == QtCore.Qt.NoButton:
                        self.isSmoothKeyPressed = True
                        self.SmoothKeyPressed.emit()
                        return False

                elif event.key() == MARKING_MENU_KEY:
                    self.markingMenuKeyPressed = True
                    return True

                elif event.key() == EXIT_KEY:
                    with disableUndoContext():
                        self.ExitKeyPressed.emit()
                    return True

                elif event.key() == PICK_INFLUENCE_KEY:
                    with disableUndoContext():
                        if not event.isAutoRepeat():
                            if event.modifiers() == QtCore.Qt.AltModifier:
                                self.PickMaxInfluenceKeyPressed.emit()
                            else:
                                self.PickInfluenceKeyPressed.emit()
                    return True

                elif event.key() == SET_ORBIT_POS_KEY:
                    with disableUndoContext():
                        if not event.isAutoRepeat():
                            self.SetOrbitPosKeyPressed.emit()
                    return True

                elif event.modifiers() == QtCore.Qt.AltModifier:
                    if event.key() == TOGGLE_XRAY_KEY:
                        with disableUndoContext():
                            if not event.isAutoRepeat():
                                self.SetToggleXrayKeyPressed.emit()
                        return True

                    if event.key() == TOGGLE_WIREFRAME_KEY:
                        with disableUndoContext():
                            if not event.isAutoRepeat():
                                self.ToggleWireframeKeyPressed.emit()
                        return True

                    if event.key() == SOLO_KEY:
                        with disableUndoContext():
                            if not event.isAutoRepeat():
                                self.SoloModeKeyPressed.emit()
                        return True

                    if event.key() == SOLO_OPAQUE_KEY:
                        with disableUndoContext():
                            if not event.isAutoRepeat():
                                self.SoloOpaqueKeyPressed.emit()
                        return True

                    if event.key() == MIRROR_KEY:
                        with disableUndoContext():
                            if not event.isAutoRepeat():
                                self.MirrorKeyPressed.emit()
                        return True
        return False

    def closeEvent(self, e):
        """Make sure the eventFilter is removed"""
        self.close()
        return super(CatchEventsWidget, self).closeEvent(e)

    def close(self):
        with disableUndoContext():
            self.SetPanelDisplayOff.emit()

            # remove the markingMenu
            self.markingMenuKeyPressed = False
            self.markingMenuShown = False
            self.closingNextPressMarkingMenu = False

            if cmds.popupMenu(MM_NAME, exists=True):
                cmds.deleteUI(MM_NAME)
            self.removeFilters()
