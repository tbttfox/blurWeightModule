from __future__ import print_function
from __future__ import absolute_import

import os
import re
import six
from fractions import Fraction

from maya import cmds, mel, OpenMaya
from six.moves import range
import numpy as np
from Qt import QtGui, QtCore, QtWidgets, QtCompat
from functools import partial

from . import GET_CONTEXT

from .influenceTree import InfluenceTree, InfluenceTreeWidgetItem

from .icons import ICONS
from .brushTools import cmdSkinCluster

from .brushTools.hotkeys import HOTKEYS

from .brushTools.brushPythonFunctions import (
    UndoContext,
    setColorsOnJoints,
    fixOptionVarContext,
    generate_new_color,
    deleteExistingColorSets,
    setSoloMode,
    afterPaint,
    closeEventCatcher,
    showBackNurbs,
    restoreShading,
    doRemoveColorSets,
    retrieveParallelMode,
    disconnectNurbs,
    doUpdateWireFrameColorSoloMode,
)
from mWeightEditor.weightTools.skinData import DataOfSkin
from mWeightEditor.weightTools.spinnerSlider import ValueSetting
from mWeightEditor.weightTools.utils import (
    GlobalContext,
    toggleBlockSignals,
    deleteTheJobs,
    addNameChangedCallback,
    addUserEventCallback,
    SettingVariable,
    orderMelList,
    Prefs,
)

try:
    from blurdev.gui import Window
except ImportError:
    from Qt.QtWidgets import QMainWindow as Window


def isInPaint():
    currentContext = cmds.currentCtx()
    # Can't use full flag name "class" in python
    if cmds.contextInfo(currentContext, c=True) == "brSkinBrush":
        return currentContext
    return False


FLAT_BUTTON_ENABLED_SS = "border : 1px solid black; background-color:rgb(200,200,200)"
FLAT_BUTTON_DISABLED_SS = "border : 1px solid black; background-color:rgb(170,170,170)"


class ValueSettingPE(ValueSetting):
    blockPostSet = False

    def postSet(self):
        if not self.blockPostSet:
            if isInPaint():
                value = self.theSpinner.value()
                if self.commandArg in ["strength", "smoothStrength"]:
                    value /= 100.0
                kArgs = {"edit": True}
                kArgs[self.commandArg] = value
                cmds.brSkinBrushContext(cmds.currentCtx(), **kArgs)

    def progressValueChanged(self, val):
        pos = self.theProgress.pos().x() + val / 100.0 * (
            self.theProgress.width() - self.btn.width()
        )
        self.btn.move(int(pos), 0)

    def setEnabled(self, val):
        if val:
            self.btn.setStyleSheet(FLAT_BUTTON_ENABLED_SS)
        else:
            self.btn.setStyleSheet(FLAT_BUTTON_DISABLED_SS)
        super(ValueSettingPE, self).setEnabled(val)

    def updateBtn(self):
        self.progressValueChanged(self.theProgress.value())

    def __init__(self, *args, **kwargs):
        text = ""
        if "text" in kwargs:
            text = kwargs["text"]
            kwargs.pop("text")
        if "commandArg" in kwargs:
            self.commandArg = kwargs["commandArg"]
            kwargs.pop("commandArg")

        super(ValueSettingPE, self).__init__(*args, **kwargs)

        self.theProgress.valueChanged.connect(self.progressValueChanged)
        self.theProgress.setTextVisible(True)
        self.theProgress.setFormat(text)
        self.theProgress.setMaximumHeight(12)
        self.theSpinner.setMaximumHeight(16)
        self.setMinimumHeight(18)

        self.theSpinner.setMaximum(100)
        self.theSpinner.setMinimum(0)

        btn = QtWidgets.QFrame(self)
        btn.show()
        btn.resize(6, 18)
        btn.move(100, 0)
        btn.pos()
        btn.show()
        btn.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents, True)
        btn.setStyleSheet(FLAT_BUTTON_ENABLED_SS)
        self.btn = btn
        self.updateBtn()


def getUiFile(fileVar, subFolder="ui", uiName=None):
    uiFolder, filename = os.path.split(fileVar)
    if uiName is None:
        uiName = os.path.splitext(filename)[0]
    return os.path.join(uiFolder, subFolder, uiName + ".ui")


INFLUENCE_COLORS = [
    (0, 0, 224),
    (224, 224, 0),
    (224, 0, 224),
    (96, 224, 192),
    (224, 128, 0),
    (192, 0, 192),
    (0, 192, 64),
    (192, 160, 0),
    (160, 0, 32),
    (128, 192, 224),
    (224, 192, 128),
    (64, 32, 160),
    (192, 160, 32),
    (224, 32, 160),
]


class SkinPaintWin(Window):
    maxWidthCentralWidget = 230
    valueMult = 0.6
    saturationMult = 0.6
    commandIndex = -1
    previousInfluenceName = ""
    value = 1.0
    commandArray = [
        "add",
        "rmv",
        "addPerc",
        "abs",
        "smooth",
        "sharpen",
        "locks",
        "unLocks",
    ]

    def __init__(self, parent=None):
        self.doPrint = False
        super(SkinPaintWin, self).__init__(parent)

        if not cmds.pluginInfo("brSkinBrush", query=True, loaded=True):
            cmds.loadPlugin("brSkinBrush")
        uiPath = getUiFile(__file__)
        QtCompat.loadUi(uiPath, self)

        self.useShortestNames = (
            bool(cmds.optionVar(query="useShortestNames"))
            if cmds.optionVar(exists="useShortestNames")
            else True
        )
        with GlobalContext(message="create dataOfSkin", doPrint=self.doPrint):
            self.dataOfSkin = DataOfSkin(
                useShortestNames=self.useShortestNames, createDisplayLocator=False
            )
        self.dataOfSkin.softOn = False
        self.weightEditor = None

        self.createWindow()
        self.addShortCutsHelp()
        self.setWindowDisplay()

        self.buildRCMenu()
        self.createColorPicker()
        self.uiInfluenceTREE.clear()
        self._treeDicWidgName = {}

        self.refresh()

        styleSheet = open(os.path.join(os.path.dirname(__file__), "maya.css"), "r").read()
        self.setStyleSheet(styleSheet)

        self._docked = False
        self.refreshSJ = None
        self.kill_scriptJob = []
        self.close_callback = []

        self._eventHandler = None
        self.loadUiState()

    def saveUiState(self):
        if self._docked:
            return
        prefs = Prefs("mPaintEditor")

        prefs.recordProperty("geometry", self.saveGeometry())
        prefs.recordProperty("shiftSmooths", self.shiftSmooths_rb.isChecked())
        prefs.save()

    def loadUiState(self):
        if self._docked:
            return
        prefs = Prefs("mPaintEditor")

        geo = prefs.restoreProperty("geometry", None)
        if geo is not None:
            self.restoreGeometry(geo)

        shiftSmooths = prefs.restoreProperty("shiftSmooths", True)
        self.shiftSmooths_rb.setChecked(shiftSmooths)

    def addShortCutsHelp(self):
        for name, hk, tooltip in HOTKEYS.buildHotkeyList():
            helpItem = QtWidgets.QTreeWidgetItem()
            helpItem.setText(0, hk)
            helpItem.setText(1, name)
            helpItem.setToolTip(0, tooltip)
            helpItem.setToolTip(1, tooltip)
            self.shortCut_Tree.addTopLevelItem(helpItem)
        self.shortCut_Tree.setStyleSheet(
            """
            QTreeWidget::item {
                padding-right:5px;
                padding-left:5px;
                border-right: 1px solid grey;
                border-bottom: 1px solid grey;
            }
            """
        )
        self.shortCut_Tree.setIndentation(0)
        self.shortCut_Tree.header().hide()
        self.shortCut_Tree.resizeColumnToContents(0)

    def showEvent(self, event):
        super(SkinPaintWin, self).showEvent(event)
        self.addCallBacks()
        cmds.evalDeferred(self.updateUIwithContextValues)
        cmds.evalDeferred(self.refresh)

    def colorSelected(self, color):
        values = [color.red() / 255.0, color.green() / 255.0, color.blue() / 255.0]
        item = self.colorDialog.item
        ind = item._index
        item.setColor(values)

        self.refreshWeightEditor(getLocks=False)
        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, refreshDfmColor=ind)

    def refreshWeightEditor(self, getLocks=True):
        if self.weightEditor is not None:
            if getLocks:
                self.weightEditor.dataOfDeformer.getLocksInfo()
            self.weightEditor._tv.repaint()

    def revertColor(self):
        self.colorDialog.setCurrentColor(self.colorDialog.cancelColor)

    def createColorPicker(self):
        self.colorDialog = QtWidgets.QColorDialog()
        self.colorDialog.currentColorChanged.connect(self.colorSelected)
        self.colorDialog.rejected.connect(self.revertColor)
        self.colorDialog.setWindowFlags(QtCore.Qt.Tool)
        self.colorDialog.setWindowTitle("pick color")
        self.colorDialog.setWindowModality(QtCore.Qt.ApplicationModal)

    def buildRCMenu(self):
        self.mainPopMenu = QtWidgets.QMenu(self)
        self.subMenuSoloColor = self.mainPopMenu.addMenu("solo color")
        soloColorIndex = (
            cmds.optionVar(query="soloColor_SkinPaintWin")
            if cmds.optionVar(exists="soloColor_SkinPaintWin")
            else 0
        )
        for ind, colType in enumerate(["white", "lava", "influence"]):
            theFn = partial(self.updateSoloColor, ind)
            act = self.subMenuSoloColor.addAction(colType, theFn)
            act.setCheckable(True)
            act.setChecked(soloColorIndex == ind)

        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.showMainMenu)

        self.popMenu = QtWidgets.QMenu(self.uiInfluenceTREE)

        selectItems = self.popMenu.addAction("select node")
        selectItems.triggered.connect(partial(self.applyLock, "selJoints"))
        self.popMenu.addAction(selectItems)

        self.popMenu.addSeparator()

        colorItems = self.popMenu.addAction("color selected")
        colorItems.triggered.connect(partial(self.randomColors, True))
        self.popMenu.addAction(colorItems)

        self.popMenu.addSeparator()

        lockSel = self.popMenu.addAction("lock Sel")
        lockSel.triggered.connect(partial(self.applyLock, "lockSel"))
        self.popMenu.addAction(lockSel)

        allButSel = self.popMenu.addAction("lock all but Sel")
        allButSel.triggered.connect(partial(self.applyLock, "lockAllButSel"))
        self.popMenu.addAction(allButSel)

        unLockSel = self.popMenu.addAction("unlock Sel")
        unLockSel.triggered.connect(partial(self.applyLock, "unlockSel"))
        self.popMenu.addAction(unLockSel)

        unLockAllButSel = self.popMenu.addAction("unlock all but Sel")
        unLockAllButSel.triggered.connect(partial(self.applyLock, "unlockAllButSel"))
        self.popMenu.addAction(unLockAllButSel)

        self.popMenu.addSeparator()

        unLockSel = self.popMenu.addAction("unlock ALL")
        unLockSel.triggered.connect(partial(self.applyLock, "clearLocks"))
        self.popMenu.addAction(unLockSel)

        self.popMenu.addSeparator()

        resetBindPose = self.popMenu.addAction("reset bindPreMatrix")
        resetBindPose.triggered.connect(self.resetBindPreMatrix)
        self.popMenu.addAction(resetBindPose)

        self.popMenu.addSeparator()

        self.showZeroDeformers = (
            bool(cmds.optionVar(query="showZeroDeformers"))
            if cmds.optionVar(exists="showZeroDeformers")
            else True
        )
        chbox = QtWidgets.QCheckBox("show Zero Deformers", self.popMenu)
        chbox.setChecked(self.showZeroDeformers)
        chbox.toggled.connect(self.showZeroDefmChecked)

        checkableAction = QtWidgets.QWidgetAction(self.popMenu)
        checkableAction.setDefaultWidget(chbox)
        self.popMenu.addAction(checkableAction)

        self.uiInfluenceTREE.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.uiInfluenceTREE.customContextMenuRequested.connect(self.showMenu)

    def showMenu(self, pos):
        self.popMenu.exec_(self.uiInfluenceTREE.mapToGlobal(pos))

    def showMainMenu(self, pos):
        self.mainPopMenu.exec_(self.mapToGlobal(pos))

    def updateSoloColor(self, ind):
        self.soloColor_cb.setCurrentIndex(ind)

    def comboSoloColorChanged(self, ind):
        with UndoContext("comboSoloColorChanged"):
            cmds.optionVar(intValue=("soloColor_SkinPaintWin", ind))
            for i in range(3):
                self.subMenuSoloColor.actions()[i].setChecked(i == ind)
            if isInPaint():
                cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, soloColorType=ind)

    def showZeroDefmChecked(self, checked):
        cmds.optionVar(intValue=("showZeroDeformers", checked))
        self.showZeroDeformers = checked
        self.popMenu.close()

        allItems = [
            self.uiInfluenceTREE.topLevelItem(ind)
            for ind in range(self.uiInfluenceTREE.topLevelItemCount())
        ]
        for item in allItems:
            if item.isZeroDfm:
                item.setHidden(not self.showZeroDeformers)

    def setWindowDisplay(self):
        self.setWindowFlags(QtCore.Qt.Window | QtCore.Qt.Tool)
        self.setWindowTitle("Paint Editor")
        self.show()

    def renameCB(self, oldName, newName):
        if self.dataOfSkin:
            lst = self.dataOfSkin.driverNames + [
                self.dataOfSkin.theSkinCluster,
                self.dataOfSkin.deformedShape,
            ]
            self.dataOfSkin.renameCB(oldName, newName)
            if oldName in lst:
                self.refresh(force=False, renamedCalled=True)

    def eventHandlerConnectCallBack(self):
        """Connect and disconnect from the eventhandler"""
        from .brushTools.catchEventsUI import EVENTCATCHER, HandleEventsQt

        if isInPaint():
            if EVENTCATCHER is not None and self._eventHandler is None:
                self._eventHandler = HandleEventsQt(self, EVENTCATCHER)
        else:
            if self._eventHandler is not None:
                self._eventHandler.disconnect()
                self._eventHandler = None

    def influencesReorderedCB(self):
        orderOfJoints = cmds.brSkinBrushContext(
            cmds.currentCtx(), query=True, weightOrderedIndices=True
        )
        self.updateOrderOfInfluences(orderOfJoints)

    def strengthChangedCB(self):
        newStrength = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, dragValue=True)
        self.updateStrengthVal(newStrength)

    def sizeChangedCB(self):
        newSize = cmds.brSkinBrushContext(cmds.currentCtx(), query=True, dragValue=True)
        self.updateSizeVal(newSize)

    def addCallBacks(self):
        # fmt: off
        self.refreshSJ = cmds.scriptJob(event=["SelectionChanged", self.refreshCallBack])

        self.kill_scriptJob = []
        self.kill_scriptJob.append(cmds.scriptJob(event=["PostToolChanged", self.eventHandlerConnectCallBack]))

        self.close_callback = []
        self.close_callback.append(OpenMaya.MSceneMessage.addCallback(OpenMaya.MSceneMessage.kBeforeNew, self.exitPaint))
        self.close_callback.append(OpenMaya.MSceneMessage.addCallback(OpenMaya.MSceneMessage.kBeforeOpen, self.exitPaint))
        self.close_callback.append(OpenMaya.MSceneMessage.addCallback(OpenMaya.MSceneMessage.kBeforeSave, self.exitPaint))

        self.close_callback.append(addNameChangedCallback(self.renameCB))

        self.close_callback.append(addUserEventCallback("brSkinBrush_influencesReordered", self.influencesReorderedCB))
        self.close_callback.append(addUserEventCallback("brSkinBrush_updateDisplayStrength", self.strengthChangedCB))
        self.close_callback.append(addUserEventCallback("brSkinBrush_updateDisplaySize", self.sizeChangedCB))
        self.close_callback.append(addUserEventCallback("brSkinBrush_pickedInfluence", self.updateCurrentInfluenceCB))
        self.close_callback.append(addUserEventCallback("brSkinBrush_toolOnSetupStart", self.toolOnSetupStart))
        self.close_callback.append(addUserEventCallback("brSkinBrush_toolOnSetupEnd", self.toolOnSetupEnd))
        self.close_callback.append(addUserEventCallback("brSkinBrush_afterPaint", afterPaint))
        self.close_callback.append(addUserEventCallback("brSkinBrush_toolOffCleanup", self.toolOffCleanup))
        # fmt: on

    def toolOnSetupStart(self):
        pass

    def toolOnSetupEnd(self):
        with UndoContext("toolOnSetupEnd"):
            with GlobalContext(message="toolOnSetupEndDeferred", doPrint=False):
                disconnectNurbs()
                cmds.select(clear=True)
                cmds.evalDeferred(doUpdateWireFrameColorSoloMode)
                self.paintStart()

    def toolOffCleanup(self):
        with GlobalContext(message="toolOffCleanup", doPrint=False):
            if cmds.objExists("SkinningWireframe"):
                cmds.delete("SkinningWireframe")
            closeEventCatcher()
            # unhide previous wireFrames

            # This is the cleanup step before the context finishes switching
            # so the current context must be a skin brush context
            mshShape = cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), query=True, meshName=True)
            if mshShape and cmds.objExists(mshShape):
                (theMesh,) = cmds.listRelatives(mshShape, parent=True, path=True)
                showBackNurbs(theMesh)

            restoreShading()

            # delete colors on Q pressed
            doRemoveColorSets()
            retrieveParallelMode()

            # retrieve autoSave
            if (
                cmds.optionVar(exists="autoSaveEnable")
                and cmds.optionVar(query="autoSaveEnable") == 1
            ):
                cmds.autoSave(enable=True)

            self.paintEnd()
            if cmds.optionVar(exists="brushPreviousSelection"):
                cmds.select(cmds.optionVar(query="brushPreviousSelection"))

    def deleteCallBacks(self):
        deleteTheJobs("SkinPaintWin.refreshCallBack")

        if self.refreshSJ is not None:
            cmds.scriptJob(kill=self.refreshSJ, force=True)
        self.refreshSJ = None

        for sj in self.kill_scriptJob:
            cmds.scriptJob(kill=sj, force=True)
        self.kill_scriptJob = []

        for callBck in self.close_callback:
            try:
                OpenMaya.MMessage.removeCallback(callBck)
            except RuntimeError:
                print("Unable to remove a callback")
        self.close_callback = []

    def highlightBtn(self, btnName):
        thebtn = self.findChild(QtWidgets.QPushButton, btnName + "_btn")
        if thebtn:
            thebtn.setChecked(True)

    def getCommandIndex(self):
        for ind, btnName in enumerate(self.commandArray):
            thebtn = self.findChild(QtWidgets.QPushButton, btnName + "_btn")
            if thebtn and thebtn.isChecked():
                return ind
        return -1

    def getEnabledButton(self):
        for btnName in self.commandArray:
            thebtn = self.findChild(QtWidgets.QPushButton, btnName + "_btn")
            if thebtn and thebtn.isChecked():
                return btnName
        return False

    def changeCommand(self, newCommand):
        commandText = self.commandArray[newCommand]
        if commandText in ["locks", "unLocks"]:
            self.valueSetter.setEnabled(False)
            self.widgetAbs.setEnabled(False)
        else:
            self.valueSetter.setEnabled(True)
            self.widgetAbs.setEnabled(True)

            contextExists = cmds.brSkinBrushContext(
                GET_CONTEXT.getLatest(), query=True, exists=True
            )
            if commandText == "smooth":
                self.valueSetter.commandArg = "smoothStrength"
                theValue = self.smoothStrengthVarStored
                if contextExists:
                    theValue = cmds.brSkinBrushContext(
                        GET_CONTEXT.getLatest(), query=True, smoothStrength=True
                    )
            else:
                self.valueSetter.commandArg = "strength"
                theValue = self.strengthVarStored
                if contextExists:
                    theValue = cmds.brSkinBrushContext(
                        GET_CONTEXT.getLatest(), query=True, strength=True
                    )

            try:
                cmds.floatSliderGrp("brSkinBrushStrength", edit=True, value=theValue)
            except Exception:
                pass
            self.updateStrengthVal(theValue)

        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, commandIndex=newCommand)

    def closeEvent(self, event):
        mel.eval("setToolTo $gMove;")
        try:
            self.deleteCallBacks()
        except RuntimeError:
            print("Error removeing callbacks")

        self.saveUiState()
        super(SkinPaintWin, self).closeEvent(event)

    def addButtonsDirectSet(self, lstBtns):
        theCarryWidget = QtWidgets.QWidget()
        carryWidgLayoutlayout = QtWidgets.QHBoxLayout(theCarryWidget)
        carryWidgLayoutlayout.setContentsMargins(0, 0, 0, 0)
        carryWidgLayoutlayout.setSpacing(0)

        for theVal in lstBtns:
            nm = str(Fraction(theVal))
            newBtn = QtWidgets.QPushButton(nm)
            newBtn.clicked.connect(partial(self.updateStrengthVal, theVal / 100.0))
            newBtn.clicked.connect(self.valueSetter.postSet)
            carryWidgLayoutlayout.addWidget(newBtn)
        theCarryWidget.setMaximumSize(self.maxWidthCentralWidget, 14)

        return theCarryWidget

    def changeLock(self, val):
        if val:
            self.lock_btn.setIcon(ICONS["lock"])
            cmds.scriptJob(kill=self.refreshSJ, force=True)
        else:
            self.lock_btn.setIcon(ICONS["unlock"])
            self.refreshSJ = cmds.scriptJob(event=["SelectionChanged", self.refreshCallBack])
        self.unLock = not val

    def changePin(self, val):
        selectedItems = self.uiInfluenceTREE.selectedItems()
        allItems = [
            self.uiInfluenceTREE.topLevelItem(ind)
            for ind in range(self.uiInfluenceTREE.topLevelItemCount())
        ]
        if val:
            self.pinSelection_btn.setIcon(ICONS["pinOn"])
            for item in allItems:
                toHide = item not in selectedItems
                toHide |= not self.showZeroDeformers and item.isZeroDfm
                item.setHidden(toHide)
        else:
            for item in allItems:
                toHide = not self.showZeroDeformers and item.isZeroDfm
                item.setHidden(toHide)
            self.pinSelection_btn.setIcon(ICONS["pinOff"])

    def showHideLocks(self, val):
        allItems = [
            self.uiInfluenceTREE.topLevelItem(ind)
            for ind in range(self.uiInfluenceTREE.topLevelItemCount())
        ]
        if val:
            self.showLocks_btn.setIcon(ICONS["eye"])
            for item in allItems:
                item.setHidden(False)
        else:
            for item in allItems:
                item.setHidden(item.isLocked())
            self.showLocks_btn.setIcon(ICONS["eye-half"])

    def isInPaint(self):
        currentContext = cmds.currentCtx()
        if cmds.contextInfo(currentContext, c=True) == "brSkinBrush":
            return currentContext
        return False

    def exitPaint(self, *args):
        self.enterPaint_btn.setEnabled(True)
        with UndoContext("exitPaint"):
            if isInPaint():
                mel.eval("setToolTo $gMove;")

    def enterPaint(self):
        if not cmds.pluginInfo("brSkinBrush", query=True, loaded=True):
            cmds.loadPlugin("brSkinBrush")

        if not self.dataOfSkin.theSkinCluster:
            return

        self.enterPaint_btn.setEnabled(False)

        with UndoContext("enterPaint"):
            setColorsOnJoints()
            dic = {
                "soloColor": int(self.solo_rb.isChecked()),
                "soloColorType": self.soloColor_cb.currentIndex(),
                "size": self.sizeBrushSetter.theSpinner.value(),
                "strength": self.valueSetter.theSpinner.value() * 0.01,
                "commandIndex": self.getCommandIndex(),
                "mirrorPaint": self.uiSymmetryCB.currentIndex(),
            }
            selectedInfluences = self.selectedInfluences()
            if selectedInfluences:
                dic["influenceName"] = selectedInfluences[0]

            GET_CONTEXT.updateIndex()

            # getMirrorInfluenceArray
            # let's select the shape first
            cmds.select(self.dataOfSkin.deformedShape, replace=True)
            cmds.setToolTo(GET_CONTEXT.getLatest())
            self.getMirrorInfluenceArray()

    def setFocusToPanel(self):
        QtCore.QTimer.singleShot(10, self.parent().setFocus)
        for panel in cmds.getPanel(visiblePanels=True):
            if cmds.getPanel(typeOf=panel) == "modelPanel":
                cmds.setFocus(panel)

    def upateSoloModeRBs(self, val):
        if val:
            self.solo_rb.setChecked(True)
        else:
            self.multi_rb.setChecked(True)

    def updateStrengthVal(self, value):
        with SettingVariable(self.valueSetter, "blockPostSet", valueOn=True, valueOut=False):
            self.valueSetter.setVal(int(value * 100.0))
            self.valueSetter.theProgress.setValue(int(value * 100.0))

    def updateSizeVal(self, value):
        with SettingVariable(self.sizeBrushSetter, "blockPostSet", valueOn=True, valueOut=False):
            self.sizeBrushSetter.setVal(int(value))
            self.sizeBrushSetter.theProgress.setValue(int(value))

    def updateOrderOfInfluences(self, orderOfJoints):
        allItems = {
            self.uiInfluenceTREE.topLevelItem(ind)._index: self.uiInfluenceTREE.topLevelItem(ind)
            for ind in range(self.uiInfluenceTREE.topLevelItemCount())
        }
        for i, influenceIndex in enumerate(orderOfJoints):
            allItems[influenceIndex].setText(4, "{:09d}".format(i))
        if self.orderType_cb.currentIndex() == 3:
            self.uiInfluenceTREE.sortByColumn(4, QtCore.Qt.AscendingOrder)  # 0

    def sortByColumn(self, ind):
        dicColumnCorrespondance = {0: 3, 1: 1, 2: 2, 3: 4}
        self.uiInfluenceTREE.sortByColumn(dicColumnCorrespondance[ind], QtCore.Qt.AscendingOrder)
        selItems = self.uiInfluenceTREE.selectedItems()
        if selItems:
            self.uiInfluenceTREE.scrollToItem(selItems[-1])

        # column 2 is side alpha name
        # column 3 is the default indices
        # column 4 is the sorted by weight picked indices

    def updateCurrentInfluenceCB(self):
        jointName = self.brSkinBrushContext(
            GET_CONTEXT.getLatest(), query=True, pickedInfluence=True
        )
        self.updateCurrentInfluence(jointName)

    def updateCurrentInfluence(self, jointName):
        items = {}
        ito = None
        for i in range(self.uiInfluenceTREE.topLevelItemCount()):
            it = self.uiInfluenceTREE.topLevelItem(i)
            items[it.text(1)] = it
            if i == 0:
                ito = it
        if jointName in items:
            self.uiInfluenceTREE.clearSelection()
            self.uiInfluenceTREE.setCurrentItem(items[jointName])
        else:
            self.uiInfluenceTREE.clearSelection()
            if ito:  # if there's joints , selct first one
                self.uiInfluenceTREE.setCurrentItem(ito)

    def changeMultiSolo(self, val):
        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, soloColor=val)
            setSoloMode(val)

    def addInfluences(self):
        sel = cmds.ls(sl=True, type="joint")
        skn = self.dataOfSkin.theSkinCluster
        prt = (
            cmds.listRelatives(self.dataOfSkin.deformedShape, path=True, parent=True)[0]
            if not cmds.nodeType(self.dataOfSkin.deformedShape) == "transform"
            else self.dataOfSkin.deformedShape
        )
        if prt in sel:
            sel.remove(prt)
        allInfluences = cmds.skinCluster(skn, query=True, influence=True)
        toAdd = [x for x in sel if x not in allInfluences]
        if toAdd:
            toAddStr = "add Influences :\n - "
            toAddStr += "\n - ".join(toAdd[:10])
            if len(toAdd) > 10:
                toAddStr += "\n -....and {0} others..... ".format(len(toAdd) - 10)

            res = cmds.confirmDialog(
                title="add Influences",
                message=toAddStr,
                button=["Yes", "No"],
                defaultButton="Yes",
                cancelButton="No",
                dismissString="No",
            )
            if res == "Yes":
                self.delete_btn.click()
                cmds.skinCluster(skn, edit=True, lockWeights=False, weight=0.0, addInfluence=toAdd)
                toSelect = list(
                    range(
                        self.uiInfluenceTREE.topLevelItemCount(),
                        self.uiInfluenceTREE.topLevelItemCount() + len(toAdd),
                    )
                )
                cmds.evalDeferred(self.selectRefresh)
                cmds.evalDeferred(partial(self.reselectIndices, toSelect))
                cmds.evalDeferred(partial(self.addInfluencesColors, toSelect))
                # add color to the damn influences added

    def addInfluencesColors(self, toSelect):
        count = self.uiInfluenceTREE.topLevelItemCount()
        colors = []
        for ind in toSelect:
            item = self.uiInfluenceTREE.topLevelItem(ind)
            if ind < count:
                ind = item._index
                values = generate_new_color(
                    colors,
                    pastel_factor=0.2,
                    valueMult=self.valueMult,
                    saturationMult=self.saturationMult,
                )
                colors.append(values)
                item.setColor(values)
            else:
                colors.append(item.currentColor)

    def fromScene(self):
        sel = cmds.ls(sl=True, transforms=True)
        for ind in range(self.uiInfluenceTREE.topLevelItemCount()):
            item = self.uiInfluenceTREE.topLevelItem(ind)
            toSel = item._influence in sel
            item.setSelected(toSel)
            if toSel:
                self.uiInfluenceTREE.scrollToItem(item)

    def reselectIndices(self, toSelect):
        count = self.uiInfluenceTREE.topLevelItemCount()
        for ind in toSelect:
            if ind < count:
                self.uiInfluenceTREE.topLevelItem(ind).setSelected(True)
                self.uiInfluenceTREE.scrollToItem(self.uiInfluenceTREE.topLevelItem(ind))

    def removeInfluences(self):
        skn = self.dataOfSkin.theSkinCluster

        toRemove = [item._influence for item in self.uiInfluenceTREE.selectedItems()]
        removeable = []
        non_removable = []
        for nm in toRemove:
            if self.dataOfSkin.display2dArray is not None:
                columnIndex = self.dataOfSkin.driverNames.index(nm)
                res = self.dataOfSkin.display2dArray[:, columnIndex]
                notNormalizable = np.where(res >= 1.0)[0]
                if notNormalizable.size == 0:
                    removeable.append(nm)
                else:
                    non_removable.append((nm, notNormalizable.tolist()))

        message = ""
        toRmvStr = "\n - ".join(removeable[:10])
        if len(removeable) > 10:
            toRmvStr += "\n -....and {0} others..... ".format(len(removeable) - 10)

        message += "remove Influences :\n - {0}".format(toRmvStr)
        if non_removable:
            toNotRmvStr = "\n - ".join([el for el, vtx in non_removable])
            message += "\n\n\ncannot remove Influences :\n - {0}".format(toNotRmvStr)
            for nm, vtx in non_removable:
                selVertices = orderMelList(vtx)
                inList = [
                    "{0}.vtx[{1}]".format(self.dataOfSkin.deformedShape, el) for el in selVertices
                ]
                print(nm, "\n", inList, "\n")

        res = cmds.confirmDialog(
            title="remove Influences",
            message=message,
            button=["Yes", "No"],
            defaultButton="Yes",
            cancelButton="No",
            dismissString="No",
        )
        if res == "Yes":
            self.delete_btn.click()
            cmds.skinCluster(skn, edit=True, removeInfluence=toRemove)
            cmds.skinCluster(skn, edit=True, forceNormalizeWeights=True)
            cmds.evalDeferred(self.selectRefresh)

    def removeUnusedInfluences(self):
        skn = self.dataOfSkin.theSkinCluster
        if skn:
            allInfluences = set(cmds.skinCluster(skn, query=True, influence=True))
            weightedInfluences = set(cmds.skinCluster(skn, query=True, weightedInfluence=True))
            zeroInfluences = list(allInfluences - weightedInfluences)
            if zeroInfluences:
                toRmvStr = "\n - ".join(zeroInfluences[:10])
                if len(zeroInfluences) > 10:
                    toRmvStr += "\n -....and {0} others..... ".format(len(zeroInfluences) - 10)

                res = cmds.confirmDialog(
                    title="remove Influences",
                    message="remove Unused Influences :\n - {0}".format(toRmvStr),
                    button=["Yes", "No"],
                    defaultButton="Yes",
                    cancelButton="No",
                    dismissString="No",
                )
                if res == "Yes":
                    self.delete_btn.click()
                    cmds.skinCluster(skn, edit=True, removeInfluence=zeroInfluences)
                    cmds.evalDeferred(self.selectRefresh)

    def randomColors(self, selected=False):
        colors = []
        lstItems = (
            self.uiInfluenceTREE.selectedItems()
            if selected
            else [
                self.uiInfluenceTREE.topLevelItem(itemIndex)
                for itemIndex in range(self.uiInfluenceTREE.topLevelItemCount())
            ]
        )

        for item in lstItems:
            values = generate_new_color(
                colors,
                pastel_factor=0.2,
                valueMult=self.valueMult,
                saturationMult=self.saturationMult,
            )
            colors.append(values)

            item.setColor(values)

        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, refresh=True)

    def createWindow(self):
        self.unLock = True
        dialogLayout = self.mainLayout

        # changing the treeWidghet
        for ind in range(dialogLayout.count()):
            it = dialogLayout.itemAt(ind)
            if isinstance(it, QtWidgets.QWidgetItem) and it.widget() == self.uiInfluenceTREE:
                break
        dialogLayout.setSpacing(0)
        self.uiInfluenceTREE.deleteLater()

        self.uiInfluenceTREE = InfluenceTree(self)
        dialogLayout.insertWidget(dialogLayout.count() - 1, self.uiInfluenceTREE)
        # end changing the treeWidghet

        self.lock_btn.setIcon(ICONS["unlock"])
        self.refresh_btn.setIcon(ICONS["refresh"])
        self.lock_btn.toggled.connect(self.changeLock)
        self.dgParallel_btn.toggled.connect(self.changeDGParallel)
        self.refresh_btn.clicked.connect(self.refreshBtn)
        self.enterPaint_btn.clicked.connect(self.enterPaint)

        self.deleteExisitingColorSets_btn.clicked.connect(deleteExistingColorSets)

        self.showLocks_btn.setIcon(ICONS["eye"])
        self.showLocks_btn.toggled.connect(self.showHideLocks)
        self.showLocks_btn.setText("")

        self.delete_btn.setIcon(ICONS["del"])
        self.delete_btn.setText("")
        self.delete_btn.clicked.connect(self.exitPaint)

        self.pinSelection_btn.setIcon(ICONS["pinOff"])
        self.pinSelection_btn.toggled.connect(self.changePin)
        self.pickVertex_btn.clicked.connect(self.pickMaxInfluence)
        self.pickInfluence_btn.clicked.connect(self.pickInfluence)
        self.clearText_btn.clicked.connect(self.clearInputText)

        self.searchInfluences_le.textChanged.connect(self.filterInfluences)
        self.solo_rb.toggled.connect(self.changeMultiSolo)

        self.soloColor_cb.currentIndexChanged.connect(self.comboSoloColorChanged)
        self.uiInfluenceTREE.itemDoubleClicked.connect(self.influenceDoubleClicked)
        self.uiInfluenceTREE.itemClicked.connect(self.influenceClicked)

        self.orderType_cb.currentIndexChanged.connect(self.sortByColumn)
        self.option_btn.clicked.connect(self.displayOptions)

        self.addInfluences_btn.clicked.connect(self.addInfluences)
        self.removeInfluences_btn.clicked.connect(self.removeInfluences)
        self.removeUnusedInfluences_btn.clicked.connect(self.removeUnusedInfluences)
        self.fromScene_btn.clicked.connect(self.fromScene)
        self.randomColors_btn.clicked.connect(self.randomColors)

        for btn, icon in [
            ("clearText", "clearText"),
            ("addInfluences", "plus"),
            ("removeInfluences", "minus"),
            ("removeUnusedInfluences", "removeUnused"),
            ("randomColors", "randomColor"),
            ("fromScene", "fromScene"),
        ]:
            thebtn = self.findChild(QtWidgets.QPushButton, btn + "_btn")
            if thebtn:
                thebtn.setText("")
                thebtn.setIcon(ICONS[icon])
        for ind, nm in enumerate(self.commandArray):
            thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
            if thebtn:
                thebtn.clicked.connect(partial(self.changeCommand, ind))
        for ind, nm in enumerate(["curveNone", "curveLinear", "curveSmooth", "curveNarrow"]):
            thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
            if thebtn:
                thebtn.setText("")
                thebtn.setIcon(ICONS[nm])
                thebtn.clicked.connect(partial(self.brSkinConn, "curve", ind))
        self.flood_btn.clicked.connect(partial(self.brSkinConn, "flood", True))

        for nm in ["lock", "refresh", "pinSelection"]:
            thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
            if thebtn:
                thebtn.setText("")

        self.uiToActivateWithPaint = [
            "pickVertex_btn",
            "pickInfluence_btn",
            "flood_btn",
            "mirrorActive_cb",
        ]
        for btnName in self.uiToActivateWithPaint:
            thebtn = self.findChild(QtWidgets.QPushButton, btnName)
            if thebtn:
                thebtn.setEnabled(False)

        self.valueSetter = ValueSettingPE(
            self, precision=2, text="Intensity", commandArg="strength", spacing=2
        )
        self.valueSetter.setAddMode(False, autoReset=False)

        self.sizeBrushSetter = ValueSettingPE(
            self, precision=2, text="Brush Size", commandArg="size", spacing=2
        )
        self.sizeBrushSetter.setAddMode(False, autoReset=False)

        Hlayout = QtWidgets.QHBoxLayout()
        Hlayout.setContentsMargins(0, 0, 0, 0)
        Hlayout.setSpacing(0)

        Vlayout = QtWidgets.QVBoxLayout()
        Vlayout.setContentsMargins(0, 0, 0, 0)
        Vlayout.setSpacing(0)
        Vlayout.addWidget(self.valueSetter)
        Vlayout.addWidget(self.sizeBrushSetter)

        Hlayout.addLayout(Vlayout)

        self.valueSetter.setMaximumSize(self.maxWidthCentralWidget, 18)
        self.sizeBrushSetter.setMaximumSize(self.maxWidthCentralWidget, 18)

        self.widgetAbs = self.addButtonsDirectSet([0.25, 0.5, 1, 2, 5, 10, 25, 50, 75, 100])

        Hlayout2 = QtWidgets.QHBoxLayout()
        Hlayout2.setContentsMargins(0, 0, 0, 0)
        Hlayout2.setSpacing(0)
        Hlayout2.addWidget(self.widgetAbs)

        dialogLayout.insertSpacing(2, 10)
        dialogLayout.insertLayout(1, Hlayout)
        dialogLayout.insertLayout(1, Hlayout2)
        dialogLayout.insertSpacing(1, 10)
        cmds.evalDeferred(self.fixUI)

        self.scrollAreaWidgetContents.layout().setContentsMargins(9, 9, 9, 9)
        sz = self.splitter.sizes()
        self.splitter.setSizes([sz[0] + sz[1], 0])

        self.drawManager_rb.toggled.connect(self.drawManager_gb.setEnabled)

        self.listCheckBoxesDirectAction = [
            "meshdrawTriangles",
            "meshdrawEdges",
            "meshdrawPoints",
            "meshdrawTransparency",
            "drawBrush",
            "coverage",
            "postSetting",
            "message",
            "ignoreLock",
            "verbose",
        ]
        self.replaceShader_cb.setChecked(cmds.optionVar(query="brushSwapShaders"))
        self.replaceShader_cb.toggled.connect(self.toggleBrushSwapShaders)

        for att in self.listCheckBoxesDirectAction:
            checkBox = self.findChild(QtWidgets.QCheckBox, att + "_cb")
            if checkBox:
                checkBox.toggled.connect(partial(self.brSkinConn, att))

        self.colorSets_rb.toggled.connect(partial(self.brSkinConn, "useColorSetsWhilePainting"))
        self.smoothRepeat_spn.valueChanged.connect(partial(self.brSkinConn, "smoothRepeat"))

        self.maxColor_sb.valueChanged.connect(partial(self.brSkinConn, "maxColor"))
        self.minColor_sb.valueChanged.connect(self.editSoloColor)

        self.soloOpaque_cb.toggled.connect(self.opaqueSet)

        self.ctrlSmooths_rb.toggled.connect(self.setSmoothKey)

        self.WarningFixSkin_btn.setVisible(False)
        self.WarningFixSkin_btn.clicked.connect(self.fixSparseArray)

        # mirror options ------------------------------------------------------
        mirrorModes = [
            "Off",
            "OrigShape X",
            "OrigShape Y",
            "OrigShape Z",
            "Object X",
            "Object Y",
            "Object Z",
        ]
        # , "World X", "World Y", "World Z"]#, "Topology"]
        self.uiSymmetryCB.addItems(mirrorModes)
        self.uiResetSymmetryBtn.clicked.connect(partial(self.uiSymmetryCB.setCurrentIndex, 0))
        self.uiSymmetryCB.currentIndexChanged.connect(self.symmetryChanged)
        self.uiTolerance_SB.valueChanged.connect(partial(self.brSkinConn, "toleranceMirror"))

        self.mirrorActive_cb.toggled.connect(self.changedMirrorActiveMode)
        self.uiLeftNamesLE.editingFinished.connect(self.getMirrorInfluenceArray)
        self.uiRightNamesLE.editingFinished.connect(self.getMirrorInfluenceArray)

    def symmetryChanged(self, index):
        self.brSkinConn("mirrorPaint", index)
        if index != 0:
            self.getMirrorInfluenceArray()
            with toggleBlockSignals([self.mirrorActive_cb]):
                self.mirrorActive_cb.setChecked(True)
            cmds.optionVar(intValue=("mirrorDefaultMode", index))
        else:
            with toggleBlockSignals([self.mirrorActive_cb]):
                self.mirrorActive_cb.setChecked(False)

    def changedMirrorActiveMode(self, val):
        indexToChangeTo = 0
        if val:
            indexToChangeTo = (
                cmds.optionVar(query="mirrorDefaultMode")
                if cmds.optionVar(exists="mirrorDefaultMode")
                else 1
            )
        self.uiSymmetryCB.setCurrentIndex(indexToChangeTo)

    def opaqueSet(self, checked):
        val = 1.0 if checked else 0.0
        self.brSkinConn("minColor", val)
        with toggleBlockSignals([self.minColor_sb]):
            self.minColor_sb.setValue(val)

    def editSoloColor(self, val):
        with toggleBlockSignals([self.soloOpaque_cb]):
            self.soloOpaque_cb.setChecked(val == 1.0)
        self.brSkinConn("minColor", val)

    def changeDGParallel(self, val):
        if val:
            self.dgParallel_btn.setText("parallel on")
            cmds.evaluationManager(mode="parallel")
        else:
            self.dgParallel_btn.setText("parallel off")
            cmds.evaluationManager(mode="off")

    def toggleBrushSwapShaders(self, val):
        cmds.optionVar(intValue=("brushSwapShaders", val))

    def brSkinConn(self, nm, val):
        if isInPaint():
            kArgs = {"edit": True}
            kArgs[nm] = val
            cmds.brSkinBrushContext(cmds.currentCtx(), **kArgs)

    def displayOptions(self, val):
        heightOption = 480
        sz = self.splitter.sizes()
        sumSizes = sz[0] + sz[1]
        if sz[1] != 0:
            self.splitter.setSizes([sumSizes, 0])
        else:
            if sumSizes > heightOption:
                self.splitter.setSizes([sumSizes - heightOption, heightOption])
            else:
                self.splitter.setSizes([0, sumSizes])

    def fixUI(self):
        for nm in self.commandArray:
            thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
            if thebtn:
                thebtn.setMinimumHeight(23)
        self.valueSetter.updateBtn()
        self.sizeBrushSetter.updateBtn()

    def updateUIwithContextValues(self):
        with GlobalContext(message="updateUIwithContextValues", doPrint=self.doPrint):
            self.dgParallel_btn.setChecked(cmds.optionVar(query="evaluationMode") == 3)

            KArgs = fixOptionVarContext()

            if "soloColor" in KArgs:
                self.solo_rb.setChecked(bool(KArgs["soloColor"]))

            if "soloColorType" in KArgs:
                self.soloColor_cb.setCurrentIndex(int(KArgs["soloColorType"]))

            self.updateSizeVal(KArgs.get("size", 4.0))

            self.strengthVarStored = KArgs.get("strength", 1.0)
            self.updateStrengthVal(self.strengthVarStored)

            if "commandIndex" in KArgs:
                commandIndex = KArgs["commandIndex"]
                commandText = self.commandArray[commandIndex]
                thebtn = self.findChild(QtWidgets.QPushButton, commandText + "_btn")
                if thebtn:
                    thebtn.setChecked(True)
                if commandText in ["locks", "unLocks"]:
                    self.valueSetter.setEnabled(False)
                    self.widgetAbs.setEnabled(False)

            if "mirrorPaint" in KArgs:
                mirrorPaintIndex = KArgs["mirrorPaint"]
                with toggleBlockSignals([self.uiSymmetryCB, self.mirrorActive_cb]):
                    self.uiSymmetryCB.setCurrentIndex(mirrorPaintIndex)
                    self.mirrorActive_cb.setChecked(mirrorPaintIndex != 0)

            if "curve" in KArgs:
                curveIndex = KArgs["curve"]
                nm = ["curveNone", "curveLinear", "curveSmooth", "curveNarrow"][curveIndex]
                thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
                if thebtn:
                    thebtn.setChecked(True)

            self.smoothStrengthVarStored = KArgs.get("smoothStrength", 1.0)
            if self.smooth_btn.isChecked():
                self.updateStrengthVal(self.smoothStrengthVarStored)

            if "influenceName" in KArgs:
                jointName = KArgs["influenceName"]
                self.previousInfluenceName = jointName
                self.updateCurrentInfluence(jointName)

            if "useColorSetsWhilePainting" in KArgs:
                self.colorSets_rb.setChecked(bool(KArgs["useColorSetsWhilePainting"]))

            if "smoothRepeat" in KArgs:
                self.smoothRepeat_spn.setValue(KArgs["smoothRepeat"])

            if "minColor" in KArgs:
                self.minColor_sb.setValue(KArgs["minColor"])

            if "maxColor" in KArgs:
                self.maxColor_sb.setValue(KArgs["maxColor"])

            if "toleranceMirror" in KArgs:
                self.uiTolerance_SB.setValue(KArgs["maxColor"])

            for att in self.listCheckBoxesDirectAction:
                if att in KArgs:
                    val = bool(KArgs[att])
                    checkBox = self.findChild(QtWidgets.QPushButton, att + "_cb")
                    if checkBox:
                        checkBox.setChecked(val)

    def clearInputText(self):
        self.searchInfluences_le.clear()

    def storeMirrorOptions(self):
        cmds.optionVar(clearArray="mirrorOptions")
        cmds.optionVar(stringValueAppend=("mirrorOptions", self.uiLeftNamesLE.text()))
        cmds.optionVar(stringValueAppend=("mirrorOptions", self.uiRightNamesLE.text()))

    def getMirrorInfluenceArray(self):
        leftInfluence = self.uiLeftNamesLE.text()
        rightInfluence = self.uiRightNamesLE.text()
        driverNames_oppIndices = self.dataOfSkin.getArrayOppInfluences(
            leftInfluence=leftInfluence,
            rightInfluence=rightInfluence,
            useRealIndices=True,
        )
        if driverNames_oppIndices and isInPaint():
            cmds.brSkinBrushContext(
                cmds.currentCtx(),
                edit=True,
                mirrorInfluences=driverNames_oppIndices,
            )

    # --------------------------------------------------------------
    # artAttrSkinPaintCtx
    # --------------------------------------------------------------
    def pickMaxInfluence(self):
        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, pickMaxInfluence=True)

    def pickInfluence(self, vertexPicking=False):
        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, pickInfluence=True)

    def selectedInfluences(self):
        return [item.influence() for item in self.uiInfluenceTREE.selectedItems()]

    def influenceDoubleClicked(self, item, column):
        txt = item._influence
        if not cmds.objExists(txt):
            return
        currentCursor = QtGui.QCursor().pos()
        autoHide = not self.showLocks_btn.isChecked()
        if column == 1:
            pos = self.uiInfluenceTREE.mapFromGlobal(currentCursor)
            if pos.x() > 40:
                cmds.select(txt)
            else:
                item.setLocked(not item.isLocked(), autoHide=autoHide)
                if isInPaint():
                    # refresh lock color
                    cmds.brSkinBrushContext(
                        cmds.currentCtx(), edit=True, refreshDfmColor=item._index
                    )

        elif column == 0:
            pos = currentCursor - QtCore.QPoint(355, 100)
            self.colorDialog.item = item
            with toggleBlockSignals([self.colorDialog]):
                self.colorDialog.cancelColor = QtGui.QColor(*item.color())
                self.colorDialog.setCurrentColor(self.colorDialog.cancelColor)
            self.colorDialog.move(pos)
            self.colorDialog.show()

    def influenceClicked(self, item, column):
        text = item._influence
        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, influenceName=text)

    def applyLock(self, typeOfLock):
        autoHide = not self.showLocks_btn.isChecked()
        selectedItems = self.uiInfluenceTREE.selectedItems()
        allItems = [
            self.uiInfluenceTREE.topLevelItem(ind)
            for ind in range(self.uiInfluenceTREE.topLevelItemCount())
        ]
        if typeOfLock == "selJoints":
            toSel = cmds.ls([item._influence for item in selectedItems])
            if toSel:
                cmds.select(toSel)
            else:
                cmds.select(clear=True)
        if typeOfLock == "clearLocks":
            for item in allItems:
                item.setLocked(False, autoHide=autoHide)
        elif typeOfLock == "lockSel":
            for item in selectedItems:
                item.setLocked(True, autoHide=autoHide)
        elif typeOfLock == "unlockSel":
            for item in selectedItems:
                item.setLocked(False, autoHide=autoHide)
        elif typeOfLock == "lockAllButSel":
            for item in allItems:
                item.setLocked(item not in selectedItems, autoHide=autoHide)
        elif typeOfLock == "unlockAllButSel":
            for item in allItems:
                item.setLocked(item in selectedItems, autoHide=autoHide)

        if typeOfLock in [
            "clearLocks",
            "lockSel",
            "unlockSel",
            "lockAllButSel",
            "unlockAllButSel",
        ]:
            self.refreshWeightEditor(getLocks=True)

        if isInPaint():
            cmds.brSkinBrushContext(cmds.currentCtx(), edit=True, refresh=True)

    def resetBindPreMatrix(self):
        selectedItems = self.uiInfluenceTREE.selectedItems()
        for item in selectedItems:
            item.resetBindPose()

    def filterInfluences(self, newText):
        self.pinSelection_btn.setChecked(False)
        if newText:
            newTexts = newText.split(" ")
            while "" in newTexts:
                newTexts.remove("")

            for nm, it in six.iteritems(self._treeDicWidgName):
                foundText = False
                for txt in newTexts:
                    txt = txt.replace("*", ".*")
                    foundText = re.search(txt, nm, re.IGNORECASE) is not None
                    if foundText:
                        break
                it.setHidden(not foundText)
        else:
            for item in six.itervalues(self._treeDicWidgName):
                item.setHidden(not self.showZeroDeformers and item.isZeroDfm)

    def refreshBtn(self):
        self.dataOfSkin = DataOfSkin(
            useShortestNames=self.useShortestNames, createDisplayLocator=False
        )
        self.dataOfSkin.softOn = False
        self.refresh(force=True)

    def selectRefresh(self):
        cmds.select(self.dataOfSkin.deformedShape)
        self.refresh(force=True)

    def refreshColorsAndLocks(self):
        for i in range(self.uiInfluenceTREE.topLevelItemCount()):
            item = self.uiInfluenceTREE.topLevelItem(i)
            item.setDisplay()
            if item.currentColor != item.color():
                item.currentColor = item.color()

    def refreshCallBack(self):
        if not self.lock_btn.isChecked():
            self.refresh()

    def refresh(self, force=False, renamedCalled=False):
        with GlobalContext(message="paintEditor getAllData", doPrint=self.doPrint):
            prevDataOfSkin = self.dataOfSkin.deformedShape, self.dataOfSkin.theDeformer
            resultData = self.dataOfSkin.getAllData(
                displayLocator=False, getskinWeights=False, force=force
            )
            doForce = not resultData

            dShape = self.dataOfSkin.deformedShape
            itExists = cmds.objExists(dShape) if dShape else False

            doForce = doForce and itExists and self.dataOfSkin.theDeformer == ""

            doForce = doForce and cmds.nodeType(self.dataOfSkin.deformedShape) in [
                "mesh",
                "nurbsSurface",
            ]
            if doForce:
                self.dataOfSkin.clearData()
                force = True
            elif not resultData:
                (
                    self.dataOfSkin.deformedShape,
                    self.dataOfSkin.theDeformer,
                ) = prevDataOfSkin

        if renamedCalled or resultData or force:
            self.uiInfluenceTREE.clear()
            self._treeDicWidgName = {}

            if not hasattr(self.dataOfSkin, "shapePath"):
                return

            isPaintable = False
            if self.dataOfSkin.shapePath:
                isPaintable = self.dataOfSkin.shapePath.apiType() in [
                    OpenMaya.MFn.kMesh,
                    OpenMaya.MFn.kNurbsSurface,
                ]

            for uiObj in [
                "options_widget",
                "buttonWidg",
                "widgetAbs",
                "valueSetter",
                "sizeBrushSetter",
                "widget_paintBtns",
                "option_GB",
            ]:
                wid = self.findChild(QtWidgets.QWidget, uiObj)
                if wid:
                    wid.setEnabled(isPaintable)

            with GlobalContext(message="Just Tree", doPrint=self.doPrint):
                with toggleBlockSignals([self.uiInfluenceTREE]):
                    for ind, nm in enumerate(self.dataOfSkin.driverNames):
                        theIndexJnt = self.dataOfSkin.indicesJoints[ind]
                        theCol = self.uiInfluenceTREE.getDeformerColor(nm)
                        jointItem = InfluenceTreeWidgetItem(
                            nm, theIndexJnt, theCol, self.dataOfSkin.theSkinCluster
                        )

                        self.uiInfluenceTREE.addTopLevelItem(jointItem)
                        self._treeDicWidgName[nm] = jointItem

                        jointItem.isZeroDfm = ind in self.dataOfSkin.hideColumnIndices
                        jointItem.setHidden(not self.showZeroDeformers and jointItem.isZeroDfm)

                self.updateCurrentInfluence(self.previousInfluenceName)
        self.dgParallel_btn.setChecked(cmds.optionVar(query="evaluationMode") == 3)
        self.updateWarningBtn()
        self.showHideLocks(self.showLocks_btn.isChecked())

    def fixSparseArray(self):
        if isInPaint():
            mel.eval("setToolTo $gMove;")
        with GlobalContext(message="fix Sparse Array", doPrint=self.doPrint):
            prevSelection = cmds.ls(sl=True)
            skn = self.dataOfSkin.theSkinCluster
            if skn and cmds.objExists(skn):
                cmdSkinCluster.reloadSkin(skn)
                self.refresh(force=True)
                cmds.select(prevSelection)

    def updateWarningBtn(self):
        skn = self.dataOfSkin.theSkinCluster
        sparseArray = (
            skn != "" and cmds.objExists(skn) and cmdSkinCluster.skinClusterHasSparceArray(skn)
        )
        self.WarningFixSkin_btn.setVisible(sparseArray)

    def paintEnd(self):  # called by the brush
        for btnName in self.uiToActivateWithPaint:
            thebtn = self.findChild(QtWidgets.QPushButton, btnName)
            if thebtn:
                thebtn.setEnabled(False)
        self.uiInfluenceTREE.paintEnd()
        self.previousInfluenceName = cmds.brSkinBrushContext(
            GET_CONTEXT.getLatest(), query=True, influenceName=True
        )
        self.enterPaint_btn.setEnabled(True)

    def paintStart(self):
        with UndoContext("paintstart"):
            for btnName in self.uiToActivateWithPaint:
                thebtn = self.findChild(QtWidgets.QPushButton, btnName)
                if thebtn:
                    thebtn.setEnabled(True)
            self.uiInfluenceTREE.paintStart()
            self.enterPaint_btn.setEnabled(False)

            dicValues = {
                "edit": True,
                "soloColor": self.solo_rb.isChecked(),
                "soloColorType": self.soloColor_cb.currentIndex(),
                "size": self.sizeBrushSetter.theSpinner.value(),
                "strength": self.valueSetter.theSpinner.value() * 0.01,
                "commandIndex": self.getCommandIndex(),
                "useColorSetsWhilePainting": self.colorSets_rb.isChecked(),
                "smoothRepeat": self.smoothRepeat_spn.value(),
                "shiftSmooths": HOTKEYS.SMOOTH_KEY == QtCore.Qt.Key_Shift,
                "maxColor": self.maxColor_sb.value(),
                "minColor": self.minColor_sb.value(),
            }
            selectedInfluences = self.selectedInfluences()
            if selectedInfluences:
                dicValues["influenceName"] = selectedInfluences[0]

            for curveIndex, nm in enumerate(
                ["curveNone", "curveLinear", "curveSmooth", "curveNarrow"]
            ):
                thebtn = self.findChild(QtWidgets.QPushButton, nm + "_btn")
                if thebtn and thebtn.isChecked():
                    dicValues["curve"] = curveIndex
                    break
            for att in self.listCheckBoxesDirectAction:
                checkBox = self.findChild(QtWidgets.QCheckBox, att + "_cb")
                if checkBox:
                    dicValues[att] = checkBox.isChecked()

            cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), **dicValues)

    def buttonByCommandIndex(self, cmdIdx):
        ret = {
            0: self.add_btn,
            1: self.rmv_btn,
            2: self.addPerc_btn,
            5: self.sharpen_btn,
            3: self.abs_btn,
            4: self.smooth_btn,
            6: self.locks_btn,
            7: self.unLocks_btn,
        }
        return ret[cmdIdx]

    def setSmoothKey(self):
        """Switch betwen XSI and Maya smooth key settings"""
        names = ["smooth_key", "remove_key"]
        mods = [QtCore.Qt.Key_Shift, QtCore.Qt.Key_Control]
        if self.ctrlSmooths_rb.isChecked():
            mods.reverse()

        HOTKEYS.updateHotkeys(dict(zip(names, mods)))
        self.shortCut_Tree.clear()
        self.addShortCutsHelp()
