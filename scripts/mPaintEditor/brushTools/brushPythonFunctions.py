from __future__ import print_function
from __future__ import absolute_import

import json
import random
import time

from Qt import QtGui
from contextlib import contextmanager

from maya import cmds
from six.moves import range, zip

from ..utils import rootWindow
from .. import PAINT_EDITOR_CONTEXT_OPTIONS, GET_CONTEXT


@contextmanager
def disableUndoContext():
    cmds.undoInfo(stateWithoutFlush=False)
    try:
        yield
    finally:
        cmds.undoInfo(stateWithoutFlush=True)


@contextmanager
def UndoContext(chunkName="myProcessTrue"):
    cmds.undoInfo(openChunk=True, chunkName=chunkName)
    try:
        yield
    finally:
        cmds.undoInfo(closeChunk=True)


def get_random_color(pastel_factor=0.5, valueMult=0.5, saturationMult=0.5):
    col = [(random.random() + pastel_factor) / (1.0 + pastel_factor) for _ in range(3)]
    theCol = QtGui.QColor.fromRgbF(*col)
    theCol.setHsvF(
        theCol.hueF(),
        valueMult * theCol.valueF() + 0.5 * valueMult,
        saturationMult * theCol.saturationF() + 0.5 * saturationMult,
    )
    return theCol.getRgbF()[:3]


def color_distance(c1, c2):
    return sum([abs(x[0] - x[1]) for x in zip(c1, c2)])


def generate_new_color(existing_colors, pastel_factor=0.5, valueMult=0.5, saturationMult=0.5):
    max_distance = None
    best_color = None
    for _ in range(0, 100):
        color = get_random_color(
            pastel_factor=pastel_factor,
            valueMult=valueMult,
            saturationMult=saturationMult,
        )
        if not existing_colors:
            return color
        best_distance = min([color_distance(color, c) for c in existing_colors])
        if not max_distance or best_distance > max_distance:
            max_distance = best_distance
            best_color = color
    return best_color


def setColorsOnJoints():
    with UndoContext("setColorsOnJoints"):
        _colors = []
        for i in range(1, 9):
            col = cmds.displayRGBColor("userDefined{0}".format(i), query=True)
            _colors.append(col)

        for jnt in cmds.ls(type="joint"):
            theInd = cmds.getAttr(jnt + ".objectColor")
            currentCol = cmds.getAttr(jnt + ".wireColorRGB")[0]
            if currentCol == (0.0, 0.0, 0.0):
                cmds.setAttr(jnt + ".wireColorRGB", *_colors[theInd])
            for destConn in (
                cmds.listConnections(
                    jnt + ".objectColorRGB",
                    destination=True,
                    source=False,
                    plugs=True,
                    type="skinCluster",
                )
                or []
            ):
                cmds.connectAttr(jnt + ".wireColorRGB", destConn, force=True)


def doRemoveColorSets():
    with UndoContext("doRemoveColorSets"):
        msh = cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), query=True, meshName=True)
        if not msh or not cmds.objExists(msh):
            return
        skinnedMesh_history = cmds.listHistory(msh, levels=0, pruneDagObjects=True) or []
        cmds.setAttr(msh + ".displayColors", 0)

        while skinnedMesh_history:
            nd = skinnedMesh_history.pop(0)
            if cmds.nodeType(nd) != "createColorSet":
                break
            cmds.delete(nd)


def getShapesSelected(returnTransform=False):
    typeSurf = ["mesh", "nurbsSurface"]
    selectionShapes = cmds.ls(sl=True, objectsOnly=True, type=typeSurf)
    if not selectionShapes:
        selection = cmds.ls(sl=True, transforms=True) + cmds.ls(hilite=True)
        selectedMesh = cmds.listRelatives(selection, type=typeSurf)
        selectionShapes = cmds.ls(sl=True, objectsOnly=True, type=typeSurf)
        if selectedMesh:
            selectionShapes += selectedMesh
        selectionShapes = [
            el for el in selectionShapes if not cmds.getAttr(el + ".intermediateObject")
        ]
    if selectionShapes and returnTransform:
        return cmds.listRelatives(selectionShapes, path=True, parent=True)
    return selectionShapes


def setToDgMode():
    with UndoContext("setToDgMode"):
        goodMode = "off"  # "serial" anmd "serialUncached" and "parallel" crashes
        if cmds.evaluationManager(query=True, mode=True) != [goodMode]:
            val = cmds.optionVar(query="evaluationMode")
            cmds.evaluationManager(mode=goodMode)
            cmds.optionVar(intValue=("revertParallelEvaluationMode", val))
            # Set everything in the entire scene dirty
            cmds.dgdirty(allPlugs=True)
        else:
            cmds.optionVar(intValue=("revertParallelEvaluationMode", 0))


def retrieveParallelMode():
    with UndoContext("retrieveParallelMode"):
        val = cmds.optionVar(query="revertParallelEvaluationMode")
        if val != 0:
            cmds.optionVar(intValue=("revertParallelEvaluationMode", 0))
            mode = "parallel" if val == 3 else "serial"
            cmds.evaluationManager(mode=mode)


def toolOnSetupStart():  # Called directly from cpp
    with UndoContext("toolOnSetupStart"):
        cmds.optionVar(intValue=("startTime", time.time()))

        setToDgMode()
        # disable AutoSave --------------------------
        if cmds.autoSave(query=True, enable=True):
            if not cmds.optionVar(exists="autoSaveEnable"):
                cmds.optionVar(intValue=("autoSaveEnable", 1))
            cmds.autoSave(enable=False)

        # found that if not Shannon paint doesn't swap deformers
        cmds.optionVar(clearArray="colorShadedDisplay")
        cmds.optionVar(intValueAppend=("colorShadedDisplay", 1))
        cmds.optionVar(intValueAppend=("colorShadedDisplay", 1), intValue=("colorizeSkeleton", 1))

        sel = cmds.ls(sl=True)
        cmds.optionVar(clearArray="brushPreviousSelection")
        for obj in sel:
            cmds.optionVar(stringValueAppend=("brushPreviousSelection", obj))
        shapeSelected = getShapesSelected(returnTransform=True)
        if not shapeSelected:  # if nothing selected
            mshShape = cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), query=True, meshName=True)
            if mshShape and cmds.objExists(mshShape):
                (theMesh,) = cmds.listRelatives(mshShape, parent=True, path=True)
                cmds.select(theMesh)
        else:
            cmds.select(shapeSelected)

        mshShapeSelected = getShapesSelected(returnTransform=False)
        if cmds.optionVar(query="brushSwapShaders"):
            restoreShading()
            swapShading(mshShapeSelected)

        # add nurbs Tesselate
        selectedNurbs = cmds.ls(mshShapeSelected, type="nurbsSurface")

        if selectedNurbs:
            mshShapeSelected = addNurbsTessellate(selectedNurbs)
            for nrbs in selectedNurbs:
                cmds.hide(nrbs)
        # for colors
        for mshShape in cmds.ls(mshShapeSelected, type="mesh"):
            cmds.polyOptions(mshShape, colorShadedDisplay=True)
            if not cmds.attributeQuery("lockedVertices", node=mshShape, exists=True):
                cmds.addAttr(mshShape, longName="lockedVertices", dataType="Int32Array")
            cmds.setAttr(mshShape + ".colorSet", size=2)
            cmds.setAttr(mshShape + ".colorSet[0].colorName", "multiColorsSet", type="string")
            cmds.setAttr(mshShape + ".colorSet[1].colorName", "soloColorsSet", type="string")
            cmds.setAttr(mshShape + ".vertexColorSource", 2)
            cmds.setAttr(mshShape + ".displayColors", 1)
            cmds.setAttr(mshShape + ".displaySmoothMesh", 0)
        callEventCatcher()


def toolOnSetupEnd():
    pass


def createMeshFromNurbs(att, prt):
    nurbsTessellate = cmds.createNode("nurbsTessellate", skipSelect=True)
    cmds.setAttr(nurbsTessellate + ".format", 3)
    cmds.setAttr(nurbsTessellate + ".polygonType", 1)
    cmds.setAttr(nurbsTessellate + ".matchNormalDir", 1)
    cmds.connectAttr(att, nurbsTessellate + ".inputSurface", force=True)

    msh = cmds.createNode("mesh", parent=prt, skipSelect=True, name="brushTmpDELETEthisMesh")
    cmds.connectAttr(nurbsTessellate + ".outputPolygon", msh + ".inMesh", force=True)

    cmds.sets(msh, edit=True, forceElement="initialShadingGroup")
    return msh


def swapShading(origMshes):
    for node in origMshes:
        shadingConns = cmds.listConnections(
            node, source=False, destination=True, plugs=True, connections=True, type="shadingEngine"
        )
        if not shadingConns:
            continue
        storedConns = []
        for src, dst in zip(shadingConns[0::2], shadingConns[1::2]):
            if ".dagSetMembers" in dst:
                cmds.disconnectAttr(src, dst)
                cmds.connectAttr(src, "initialShadingGroup.dagSetMembers", nextAvailable=True)
                storedConns.append((src, dst))
        if not cmds.objExists(node + ".shadingInfos"):
            cmds.addAttr(node, longName="shadingInfos", dataType="string")
        cmds.setAttr(node + ".shadingInfos", json.dumps(storedConns), type="string")


def restoreShading():
    allShadingInfosAttrs = set(cmds.ls("*.shadingInfos"))
    for att in allShadingInfosAttrs:
        storedConns = json.loads(cmds.getAttr(att))
        for src, dst in storedConns:
            if not cmds.objExists(src):
                continue
            shadingConn = cmds.listConnections(src, destination=True, source=False, plugs=True)
            if shadingConn:
                cmds.disconnectAttr(src, shadingConn[0])
            if not cmds.objExists(dst):
                continue
            cmds.connectAttr(src, dst, force=True)
        # delete
        cmds.deleteAttr(att)


def getOrigShape(nrbs):
    (prt,) = cmds.listRelatives(nrbs, parent=True, path=True)
    allShapes = cmds.listRelatives(prt, shapes=True, noIntermediate=False)
    deformedShapes = cmds.listRelatives(prt, shapes=True, noIntermediate=True)
    baseShapes = set(allShapes) - set(deformedShapes)
    if len(baseShapes) > 1:
        for origShape in baseShapes:
            hist = cmds.ls(cmds.listHistory(origShape, future=True), type="shape")
            if nrbs in hist:
                return origShape
    else:
        origShape = baseShapes.pop()
        return origShape
    return None


def addNurbsTessellate(selectedNurbs):
    mshs = []
    for nrbs in selectedNurbs:
        if cmds.listConnections(nrbs, source=False, destination=True, type="nurbsTessellate"):
            continue
        (prt,) = cmds.listRelatives(nrbs, parent=True, path=True)
        att = nrbs + ".local"
        msh = createMeshFromNurbs(att, prt)
        mshs.append(msh)
        cmds.addAttr(msh, longName="nurbsTessellate", attributeType="double", defaultValue=0.0)
        if not cmds.attributeQuery("nurbsTessellate", node=nrbs, exists=True):
            cmds.addAttr(
                nrbs,
                longName="nurbsTessellate",
                attributeType="double",
                defaultValue=0.0,
            )
        cmds.connectAttr(nrbs + ".nurbsTessellate", msh + ".nurbsTessellate", force=True)

        origShape = getOrigShape(nrbs)
        assert origShape is not None
        att = origShape + ".local"
        origMsh = createMeshFromNurbs(att, prt)
        cmds.setAttr(origMsh + ".v", 0)
        cmds.setAttr(origMsh + ".intermediateObject", 1)
        cmds.addAttr(origMsh, longName="origMeshNurbs", attributeType="double", defaultValue=0.0)
        cmds.addAttr(msh, longName="origMeshNurbs", attributeType="double", defaultValue=0.0)
        cmds.connectAttr(msh + ".origMeshNurbs", origMsh + ".origMeshNurbs", force=True)
    return mshs


def disconnectNurbs():
    sel = cmds.ls(sl=True)
    toDelete = []
    for nd in sel:
        prt = (
            nd
            if cmds.nodeType(nd) == "transform"
            else cmds.listRelatives(nd, parent=True, path=True)
        )
        mshs = cmds.listRelatives(prt, type="mesh", path=True)
        for msh in mshs:
            tesselates = cmds.ls(cmds.listHistory(msh), type="nurbsTessellate") or []
            if tesselates:
                toDelete.extend(tesselates)
    cmds.delete(toDelete)


def showBackNurbs(theMesh):
    shps = cmds.listRelatives(theMesh, shapes=True, path=True, type="nurbsSurface") or []
    mshs = cmds.listRelatives(theMesh, shapes=True, path=True, type="mesh") or []
    toDelete = []
    for msh in mshs:
        if cmds.attributeQuery("origMeshNurbs", node=msh, exists=True):
            toDelete.append(msh)
    cmds.delete(toDelete)
    for nrbs in shps:
        if cmds.attributeQuery("nurbsTessellate", node=nrbs, exists=True):
            cmds.deleteAttr(nrbs + ".nurbsTessellate")
    if shps:
        cmds.showHidden(shps)


def cleanTheNurbs():  # Called directly by cpp
    if cmds.contextInfo(GET_CONTEXT.getLatest(), c=True) != "brSkinBrushContext":
        nurbsTessellateAttrs = cmds.ls("*.nurbsTessellate")
        if not nurbsTessellateAttrs:
            return

        nurbsTessellateAttrsNodes = [el.split(".nurbsTessellate")[0] for el in nurbsTessellateAttrs]
        prts = set(cmds.listRelatives(nurbsTessellateAttrsNodes, parent=True, path=True))
        for prt in prts:
            showBackNurbs(prt)


def callEventCatcher():
    from . import catchEventsUI

    if catchEventsUI.ROOTWINDOW is None:
        catchEventsUI.ROOTWINDOW = rootWindow()
    catchEventsUI.EVENTCATCHER = catchEventsUI.CatchEventsWidget()
    catchEventsUI.EVENTCATCHER.open()


def closeEventCatcher():
    from . import catchEventsUI

    if catchEventsUI.EVENTCATCHER is not None:
        catchEventsUI.EVENTCATCHER.close()


def updateWireFrameColorSoloMode(soloColor):
    with disableUndoContext():
        if cmds.objExists("SkinningWireframeShape"):
            overrideColorRGB = [0.8, 0.8, 0.8] if soloColor else [0.1, 0.1, 0.1]
            cmds.setAttr("SkinningWireframeShape.overrideEnabled", 1)
            cmds.setAttr("SkinningWireframeShape.overrideRGBColors", 1)
            cmds.setAttr("SkinningWireframeShape.overrideColorRGB", *overrideColorRGB)


def doUpdateWireFrameColorSoloMode():
    soloColor = cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), query=True, soloColor=True)
    updateWireFrameColorSoloMode(soloColor)


def setSoloMode(soloColor):
    with UndoContext("setSoloMode"):
        cmds.brSkinBrushContext(GET_CONTEXT.getLatest(), edit=True, soloColor=soloColor)
        updateWireFrameColorSoloMode(soloColor)


def fixOptionVarContext():
    if not cmds.optionVar(exists=PAINT_EDITOR_CONTEXT_OPTIONS):
        return {}

    cmd = cmds.optionVar(query=PAINT_EDITOR_CONTEXT_OPTIONS)
    return {k.strip("-"): v for k, v in json.loads(cmd).items()}


def deleteExistingColorSets():
    with UndoContext("deleteExistingColorSets"):
        sel = cmds.ls(sl=True)
        for obj in sel:
            skinnedMesh_history = cmds.listHistory(obj, levels=0, pruneDagObjects=True) or []
            cmds.setAttr(obj + ".displayColors", 0)
            res = cmds.ls(skinnedMesh_history, type=["createColorSet", "deleteColorSet"])
            if res:
                cmds.delete(res)
            existingColorSets = cmds.polyColorSet(obj, query=True, allColorSets=True) or []
            for colSet in [
                "multiColorsSet",
                "multiColorsSet2",
                "soloColorsSet",
                "soloColorsSet2",
            ]:
                if colSet in existingColorSets:
                    cmds.polyColorSet(obj, delete=True, colorSet=colSet)


def afterPaint():
    with UndoContext("afterPaint"):
        import mWeightEditor
        from Qt.QtWidgets import QApplication

        editor = mWeightEditor.WEIGHT_EDITOR
        if editor is not None and editor in QApplication.instance().topLevelWidgets():
            editor.refreshSkinDisplay()
