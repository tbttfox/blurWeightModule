try:
    from Qt import QtGui, QtCore, QtWidgets
    from Qt import QtCompat
    from Qt.QtWidgets import QApplication, QSplashScreen, QDialog, QMainWindow
except:
    from PySide2 import QtGui, QtCore, QtWidgets
    import shiboken2 as QtCompat
    from PySide2.QtWidgets import QApplication, QSplashScreen, QDialog, QMainWindow
from maya import OpenMayaUI, cmds, mel

from brushPythonFunctions import (
    callPaintEditorFunction,
    escapePressed,
    toggleSoloMode,
    disableUndoContext,
)
import meshFnIntersection

"""
import catchEventsUI
reload(catchEventsUI)
EVENTCATCHER = catchEventsUI.CatchEventsWidget()
EVENTCATCHER.open()
"""


def callMarkingMenu():
    if cmds.popupMenu("tempMM", exists=True):
        cmds.deleteUI("tempMM")
    res = mel.eval("findPanelPopupParent")
    cmds.popupMenu(
        "tempMM",
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
        cmd = "brSkinBrushContext -edit -commandIndex {} `currentCtx`;".format(cmdInd)
        cmd += 'python("import __main__;__main__.paintEditor.' + btn + '_btn.click()");\n'
        kwArgs["command"] = cmd
        cmds.menuItem("menuEditorMenuItem{0}".format(ind + 1), **kwArgs)
    kwArgs.pop("radialPosition", None)
    kwArgs["label"] = "solo color"
    kwArgs["subMenu"] = True

    cmds.menuItem("menuEditorMenuItem{0}".format(len(lstCommands) + 1), **kwArgs)
    kwArgs["subMenu"] = False
    for ind, colType in enumerate(["white", "lava", "influence"]):
        kwArgs["label"] = colType
        cmd = 'python("import __main__;__main__.paintEditor.updateSoloColor(' + str(ind) + ')");\n'
        cmd += "brSkinBrushContext -edit -soloColorType {} `currentCtx`;".format(ind)
        kwArgs["command"] = cmd

        cmds.menuItem("menuEditorMenuItemCol{0}".format(ind + 1), **kwArgs)
    mel.eval("setParent -menu ..;")
    # setParent -menu
    mel.eval("setParent -menu ..;")


def rootWindow():
    """
    Returns the currently active QT main window
    Only works for QT UIs like Maya
    """
    # for MFC apps there should be no root window
    window = None
    if QApplication.instance():
        inst = QApplication.instance()
        window = inst.activeWindow()
        # Ignore QSplashScreen s, they should never be considered the root window.
        if isinstance(window, QSplashScreen):
            return None
        # If the application does not have focus try to find A top level widget
        # that doesn t have a parent and is a QMainWindow or QDialog
        if window == None:
            windows = []
            dialogs = []
            for w in QApplication.instance().topLevelWidgets():
                if w.parent() == None:
                    if isinstance(w, QMainWindow):
                        windows.append(w)
                    elif isinstance(w, QDialog):
                        dialogs.append(w)
            if windows:
                window = windows[0]
            elif dialogs:
                window = dialogs[0]
        # grab the root window
        if window:
            while True:
                parent = window.parent()
                if not parent:
                    break
                if isinstance(parent, QSplashScreen):
                    break
                window = parent
    return window


class CatchEventsWidget(QtWidgets.QWidget):
    # transparent widget over viewport to catch rightclicks
    verbose = False
    filterInstalled = False
    displayLabel = None
    EventFilterWidgetReceiver = None
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
        super(CatchEventsWidget, self).__init__(rootWindow())
        self.setMask(QtGui.QRegion(0, 0, 1, 1))
        # self.mainWindow = connectedWindow

        self.OPressed = False
        self.markingMenuShown = False
        self.closingNextPressMarkingMenu = False
        self.ctrlPressed = False
        self.shiftPressed = False

        self.rootWin = rootWindow()
        ptr = OpenMayaUI.MQtUtil.mainWindow()
        self.mainMaya = QtCompat.wrapInstance(long(ptr), QtWidgets.QWidget)
        # self.setAttribute(QtCore.Qt.WA_MouseNoMask, True)
        self.prevButton = self.lstButtons[0]
        self.prevQtButton = "add"

        self.orbit = meshFnIntersection.Orbit()

    # ---------- GAMMA --------------------------------------
    restorePanels = []

    def setPanelsDisplayOn(self):
        self.restorePanels = []
        dicPanel = {"edit": True, "displayLights": "flat"}
        wireframeCB = callPaintEditorFunction("wireframe_cb")

        if wireframeCB and wireframeCB.isChecked():
            dicPanel["wireframeOnShaded"] = False
        for panel in cmds.getPanel(vis=True):
            if cmds.getPanel(to=panel) == "modelPanel":
                valDic = {}
                for key in [
                    "displayLights",
                    "cmEnabled",
                    "selectionHiliteDisplay",
                    "wireframeOnShaded",
                ]:
                    dic = {"query": True, key: True}
                    valDic[key] = cmds.modelEditor(panel, **dic)
                # cmEnabled = cmds.modelEditor(panel, query=True, cmEnabled = True)
                # selectionHiliteDisplay= cmds.modelEditor(panel, query=True, selectionHiliteDisplay = True)
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
            self.show()
        # print "THAT SHOULD BE OPEN"

    def installFilters(self):
        listModelPanels = [
            el for el in cmds.getPanel(vis=True) if cmds.getPanel(to=el) == "modelPanel"
        ]
        ptr = OpenMayaUI.MQtUtil.findControl(listModelPanels[0])
        model_panel_4 = QtCompat.wrapInstance(long(ptr), QtWidgets.QWidget)
        self.EventFilterWidgetReceiver = model_panel_4.parent().parent()

        self.filterInstalled = True
        QApplication.instance().installEventFilter(self)

    def removeFilters(self):
        self.hide()
        self.filterInstalled = False
        QApplication.instance().removeEventFilter(self)

    def highlightBtns(self):
        btnQtToSelect = ""
        btnMayaToSelect = ""
        showStrenghtValue = False
        if self.shiftPressed and self.ctrlPressed:
            btnQtToSelect = "sharpen"
            btnMayaToSelect = "brSkinBrushSharpenRb"
        elif self.shiftPressed:
            if self.prevButton == "brSkinBrushAddRb":
                btnMayaToSelect = "brSkinBrushRemoveRb"
            elif self.prevButton == "brSkinBrushLockVerticesRb":
                btnMayaToSelect = "brSkinBrushUnLockVerticesRb"
            else:
                btnMayaToSelect = self.prevButton
            if self.prevQtButton:
                if self.prevQtButton == "add":
                    btnQtToSelect = "rmv"
                elif self.prevQtButton == "locks":
                    btnQtToSelect = "unLocks"
                else:
                    btnQtToSelect = self.prevQtButton
        elif self.ctrlPressed:
            btnQtToSelect = "smooth"
            btnMayaToSelect = "brSkinBrushSmoothRb"
        else:
            btnQtToSelect = self.prevQtButton
            btnMayaToSelect = self.prevButton
        callPaintEditorFunction("highlightBtn", btnQtToSelect)
        if cmds.radioButton(btnMayaToSelect, ex=True):
            cmds.radioButton(btnMayaToSelect, edit=True, select=True)
        if self.ctrlPressed:
            value = cmds.brSkinBrushContext("brSkinBrushContext1", query=True, smoothStrength=True)
        else:
            value = cmds.brSkinBrushContext("brSkinBrushContext1", query=True, strength=True)
        callPaintEditorFunction("updateStrengthVal", value)
        try:
            cmds.floatSliderGrp("brSkinBrushStrength", edit=True, value=value)
        except:
            pass

    def eventFilter(self, obj, event):
        # with disableUndoContext():
        if event.type() == QtCore.QEvent.MouseMove:
            event.ignore()
            return super(CatchEventsWidget, self).eventFilter(obj, event)
        if event.type() == QtCore.QEvent.KeyRelease:
            shiftIsReleased = event.key() == QtCore.Qt.Key_Shift
            ctrlIsReleased = event.key() == QtCore.Qt.Key_Control
            if (shiftIsReleased and self.shiftPressed) or (ctrlIsReleased and self.ctrlPressed):
                with disableUndoContext():
                    # print("shiftIsReleased {} ctrlIsReleased {}".format(shiftIsReleased, ctrlIsReleased))
                    if self.shiftPressed and shiftIsReleased:
                        self.shiftPressed = False
                    if self.ctrlPressed and ctrlIsReleased:
                        self.ctrlPressed = False
                    self.highlightBtns()
                # print("self.shiftPressed {} self.ctrlPressed {}".format(self.shiftPressed, self.ctrlPressed))
            elif event.key() == QtCore.Qt.Key_U:
                if obj is self.EventFilterWidgetReceiver and self.OPressed:
                    # print "  OReleased"
                    self.OPressed = False
                    event.ignore()
                    return True
                return super(CatchEventsWidget, self).eventFilter(obj, event)
            return super(CatchEventsWidget, self).eventFilter(obj, event)
        if (
            event.type() in [QtCore.QEvent.MouseButtonPress, QtCore.QEvent.MouseButtonRelease]
            and event.modifiers() != QtCore.Qt.AltModifier
        ):
            if event.modifiers() == QtCore.Qt.NoModifier:  # regular click
                if event.type() == QtCore.QEvent.MouseButtonPress:  # click
                    with disableUndoContext():
                        if self.OPressed:
                            if not self.markingMenuShown:
                                callMarkingMenu()
                                self.markingMenuShown = True
                                self.closingNextPressMarkingMenu = False
                                # print "-- callMarkingMenu --"
                        elif self.closingNextPressMarkingMenu:
                            if cmds.popupMenu("tempMM", exists=True):
                                cmds.deleteUI("tempMM")
                            self.markingMenuShown = False
                            self.OPressed = False
                            self.closingNextPressMarkingMenu = False
                elif event.type() == QtCore.QEvent.MouseButtonRelease:  # click release
                    if self.markingMenuShown:
                        # print "Closing markingMenu !!"
                        self.closingNextPressMarkingMenu = True
                return super(CatchEventsWidget, self).eventFilter(obj, event)
            return super(CatchEventsWidget, self).eventFilter(obj, event)
        if event.type() == QtCore.QEvent.KeyPress:
            if event.key() == QtCore.Qt.Key_P:  # print info of the click press
                with disableUndoContext():
                    active_view = OpenMayaUI.M3dView.active3dView()
                    sw = active_view.widget()
                    res = QtCompat.wrapInstance(long(sw), QtWidgets.QWidget)

                    listModelPanels = [
                        el for el in cmds.getPanel(vis=True) if cmds.getPanel(to=el) == "modelPanel"
                    ]
                    listModelPanelsCompats = [
                        QtCompat.wrapInstance(
                            long(OpenMayaUI.MQtUtil.findControl(el)), QtWidgets.QWidget
                        )
                        for el in listModelPanels
                    ]
                    listModelPanelsCompatsPrts = [el.parent() for el in listModelPanelsCompats]
                    # ptr = OpenMayaUI.MQtUtil.findControl(listModelPanels [0])
                    # model_panel_4 = QtCompat.wrapInstance(long(ptr), QtWidgets.QWidget)
                    if res is obj:
                        print ("ViewPort")
                    elif res is self.mainMaya:
                        print ()
                    elif obj is self.mainMaya:
                        print ("self.mainMaya")
                    elif obj is self:
                        print ("self")
                    elif obj is self.parent():
                        print ("self Prt")
                    elif obj is self.parent().parent():
                        print ("self Prt Prt")
                    elif obj is self.rootWin:
                        print ("self.rootWin")
                    elif obj in listModelPanelsCompats:
                        print ("it is a model_panel")
                    elif obj in listModelPanelsCompatsPrts:
                        print ("it is a model_panel Parent")
                    # elif obj is model_panel_4.parent() : print "model_panel_4 Prt"
                    # elif obj is model_panel_4.parent().parent() : print "model_panel_4 Prt PRT"
                    else:
                        print (obj)
                return super(CatchEventsWidget, self).eventFilter(obj, event)
            if event.key() == QtCore.Qt.Key_U:
                if self.OPressed:
                    event.ignore()
                    return True
                if obj is self.EventFilterWidgetReceiver:
                    self.OPressed = True
                    return True
                else:
                    return super(CatchEventsWidget, self).eventFilter(obj, event)
            elif event.key() == QtCore.Qt.Key_Escape:
                with disableUndoContext():
                    escapePressed()
                    event.ignore()
                    # self.close()
                    mel.eval("setToolTo $gMove;")
                    return True
            elif event.key() == QtCore.Qt.Key_D:
                with disableUndoContext():
                    listModelPanels = [
                        el for el in cmds.getPanel(vis=True) if cmds.getPanel(to=el) == "modelPanel"
                    ]
                    listModelPanelsCompats = [
                        QtCompat.wrapInstance(
                            long(OpenMayaUI.MQtUtil.findControl(el)), QtWidgets.QWidget
                        )
                        for el in listModelPanels
                    ]
                    listModelPanelsCompatsPrts = [el.parent() for el in listModelPanelsCompats]
                    # ptr = OpenMayaUI.MQtUtil.findControl(listModelPanels [0])
                    # model_panel_4 = QtCompat.wrapInstance(long(ptr), QtWidgets.QWidget)
                    if obj in listModelPanelsCompats or obj in listModelPanelsCompatsPrts:
                        # print "it is a model_panel"
                        event.ignore()

                        if event.modifiers() == QtCore.Qt.AltModifier:
                            mel.eval("brSkinBrushContext -edit -pickMaxInfluence 1 `currentCtx`;")
                        else:
                            mel.eval("brSkinBrushContext -edit -pickInfluence 1 `currentCtx`;")
                        # self.mainWindow.pickInfluence( vertexPicking=altPressed)
                        # return True
                    elif obj is self.mainMaya:
                        event.ignore()
                        return True
                # else : return super(CatchEventsWidget, self).eventFilter(obj, event)
            if event.key() == QtCore.Qt.Key_F:
                with disableUndoContext():
                    self.orbit.setOrbitPosi()
                event.ignore()
                return True
            elif event.key() == QtCore.Qt.Key_Control:
                if QApplication.mouseButtons() == QtCore.Qt.NoButton and not self.ctrlPressed:
                    self.ctrlPressed = True
                    event.ignore()
                    with disableUndoContext():
                        if not self.shiftPressed:
                            self.prevButton = self.lstButtons[
                                cmds.brSkinBrushContext(
                                    "brSkinBrushContext1", query=True, commandIndex=True
                                )
                            ]
                            self.prevQtButton = callPaintEditorFunction("getEnabledButton")
                        self.highlightBtns()
            if event.key() == QtCore.Qt.Key_Shift:
                if QApplication.mouseButtons() == QtCore.Qt.NoButton and not self.shiftPressed:
                    self.shiftPressed = True
                    event.ignore()
                    with disableUndoContext():
                        # callPaintEditorFunction("highlightBtn", "sharpen")
                        if not self.ctrlPressed:
                            if self.verbose:
                                print "custom SHIFT pressed"
                            self.prevButton = self.lstButtons[
                                cmds.brSkinBrushContext(
                                    "brSkinBrushContext1", query=True, commandIndex=True
                                )
                            ]
                            self.prevQtButton = callPaintEditorFunction("getEnabledButton")
                        self.highlightBtns()
            elif event.modifiers() == QtCore.Qt.AltModifier:
                if event.key() == QtCore.Qt.Key_X:
                    with disableUndoContext():
                        listModelPanels = [
                            el
                            for el in cmds.getPanel(vis=True)
                            if cmds.getPanel(to=el) == "modelPanel"
                        ]
                        val = not cmds.modelEditor(listModelPanels[0], query=True, jointXray=True)
                        for pnel in listModelPanels:
                            cmds.modelEditor(pnel, edit=True, jointXray=val)
                    event.ignore()
                    return True
                if event.key() == QtCore.Qt.Key_W:
                    with disableUndoContext():
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
                                listModelPanels[0], query=True, wireframeOnShaded=True
                            )
                            for pnel in listModelPanels:
                                cmds.modelEditor(pnel, edit=True, wireframeOnShaded=val)
                    event.ignore()
                    return True
                if event.key() == QtCore.Qt.Key_S:
                    # print "toggle soloMode"
                    with disableUndoContext():
                        toggleSoloMode()
                    event.ignore()
                    return True
                if event.key() == QtCore.Qt.Key_A:
                    with disableUndoContext():
                        soloOpaque = callPaintEditorFunction("soloOpaque_cb")
                        if soloOpaque:
                            soloOpaque.toggle()
                        else:
                            minColor = cmds.brSkinBrushContext(
                                "brSkinBrushContext1", query=True, minColor=True
                            )
                            if minColor == 1.0:
                                cmds.brSkinBrushContext(
                                    "brSkinBrushContext1", edit=True, minColor=0.0
                                )
                            else:
                                cmds.brSkinBrushContext(
                                    "brSkinBrushContext1", edit=True, minColor=1.0
                                )
                    event.ignore()
                    return True
                # if event.key() == QtCore.Qt.Key_F:
                #     print "FLOOD"
                #     cmds.brSkinBrushContext("brSkinBrushContext1", edit=True, flood=True)
                #     event.ignore()
                #     return True
                if event.key() == QtCore.Qt.Key_M:
                    with disableUndoContext():
                        print "mirror active"
                        callPaintEditorFunction("mirrorActive_cb").toggle()
                    # self.mainWindow.mirrorActive_cb
                    event.ignore()
                    return True
            return super(CatchEventsWidget, self).eventFilter(obj, event)
        try:
            return super(CatchEventsWidget, self).eventFilter(obj, event)
        except TypeError:  # TypeError: super(type, obj): obj must be an instance or subtype of type
            print "Shannon bug ..."
            with disableUndoContext():
                self.removeFilters()

    def closeEvent(self, e):
        """
        Make sure the eventFilter is removed
        """
        self.fermer()
        return super(CatchEventsWidget, self).closeEvent(e)

    def fermer(self):
        with disableUndoContext():
            self.setPanelsDisplayOff()
            self.removeFilters()


"""
a = CatchEventsWidget()
"""
