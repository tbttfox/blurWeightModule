from __future__ import print_function
from __future__ import absolute_import

import datetime
import six
import sys
import time

from contextlib import contextmanager
from maya import cmds, OpenMaya
from six.moves import range, zip


#
# global functions
#


@contextmanager
def SettingWithRedraw(theWindow, raise_error=True):
    theWindow.storeSelection()
    theWindow._tm.beginResetModel()
    try:
        yield
    finally:
        theWindow._tm.endResetModel()
        theWindow.retrieveSelection()
        # add a refresh of the locator ?


@contextmanager
def ResettingModel(tm, raise_error=True):
    tm.beginResetModel()
    try:
        yield
    finally:
        tm.endResetModel()


@contextmanager
def SettingVariable(variableHolder, variableName, valueOn=True, valueOut=False):
    if not isinstance(variableHolder, dict):
        variableHolder = variableHolder.__dict__

    variableHolder[variableName] = valueOn
    try:
        yield
    finally:
        variableHolder[variableName] = valueOut


@contextmanager
def ToggleHeaderVisibility(HH, raise_error=True):
    HH.hide()
    try:
        yield
    finally:
        HH.show()


@contextmanager
def GlobalContext(
    message="processing",
    raise_error=True,
    openUndo=True,
    suspendRefresh=False,
    doPrint=True,
):
    startTime = time.time()
    cmds.waitCursor(state=True)
    if openUndo:
        cmds.undoInfo(openChunk=True, chunkName=message)
    if suspendRefresh:
        cmds.refresh(suspend=True)

    try:
        yield
    except Exception as e:
        import traceback

        if raise_error:
            traceback.print_exc()
            raise e
        else:
            traceback.print_exc(file=sys.stderr)

    finally:
        if cmds.waitCursor(q=True, state=True):
            cmds.waitCursor(state=False)
        if openUndo:
            cmds.undoInfo(closeChunk=True)
        if suspendRefresh:
            cmds.refresh(suspend=False)
            cmds.refresh()

        completionTime = time.time() - startTime
        timeRes = str(datetime.timedelta(seconds=int(completionTime))).split(":")
        if doPrint:
            result = "{0} hours {1} mins {2} secs".format(*timeRes)
            print("{0} executed in {1}[{2:.2f} secs]".format(message, result, completionTime))


@contextmanager
def toggleBlockSignals(listWidgets, raise_error=True):
    for widg in listWidgets:
        widg.blockSignals(True)
    try:
        yield
    finally:
        for widg in listWidgets:
            widg.blockSignals(False)


#
# selection
#
def getListDeformersFromSel(sel):
    listDeformers = []
    selShape = ""
    if sel:
        selShape = cmds.ls(sel, objectsOnly=True)[0]
        if cmds.ls(selShape, transforms=True):
            selShape = (
                cmds.listRelatives(selShape, shapes=True, path=True, noIntermediate=True) or [""]
            )[0]

        if not cmds.ls(selShape, shapes=True):
            return "", listDeformers

        hist = cmds.listHistory(selShape, lv=0, pruneDagObjects=True, interestLevel=True)
        if hist:
            listDeformers = cmds.ls(hist, type="geometryFilter") or []
    return selShape, listDeformers


def getNumberVertices(msh):
    if cmds.nodeType(msh) == "mesh":
        return cmds.polyEvaluate(msh, vertex=True)
    elif cmds.nodeType(msh) in ["nurbsSurface", "nurbsCurve"]:
        return len(cmds.ls(msh + ".cv[*]", flatten=True))
    return 0


def orderMelList(listInd, onlyStr=True):
    """Group a sorted list of indices into ranges for use in mel scripts

    The ranges *include* the last item in the list so (5, 10) returned
    by this script would be range(5, 11) in python

    Arguments:
        listInd (List[int]): A list of indices
        onlyStr (bool): Whether to return the list of mel-formatted
            strings or the list of tuples
    Returns:
        List: A list of strings or a list of tuples depending on
            the onlyStr argument
    """
    if not listInd:
        return []

    # Index: 0 1 2   3  4  5
    # Value: 5 6 7  10 11 12
    # Diff : 5 5 5   7  7  7  <-- (Value - Index)
    # Note: each "group" has the same diff
    def tup(a, b):
        return (a,) if a == b else (a, b)

    listInd = sorted(listInd)
    curVal = listInd[0]
    start = listInd[0]
    ret = []
    for i, val in enumerate(listInd):
        key = val - i
        if curVal != key:
            ret.append(tup(start, listInd[i - 1]))
            start = val
            curVal = key
    ret.append(tup(start, listInd[-1]))

    if onlyStr:
        return [":".join(map(six.text_type, x)) for x in ret]
    return ret


def orderMelListWithWeights(listInd):
    """Group a sorted list of indices with paired weights into ranges for
    use in mel scripts

    The ranges *include* the last item in the list so (5, 10) returned
    by this script would be range(5, 11) in python

    Arguments:
        listInd (List[int]): A list of indices
    Returns:
        List: A list of strings or a list of tuples depending on
            the onlyStr argument
    """
    if not listInd:
        return []

    # Index: 0 1 2   3  4  5
    # Value: 5 6 7  10 11 12
    # Diff : 5 5 5   7  7  7  <-- (Value - Index)
    # Note: each "group" has the same diff
    def tup(a, b, w):
        return [(a,), w] if a == b else [(a, b), w]

    listInd = sorted(listInd)
    curVal = listInd[0]
    start = listInd[0]
    ret = []
    weights = []
    for i, (val, weight) in enumerate(listInd):
        key = val - i
        if curVal != key:
            ret.append(tup(start, listInd[i - 1], weights))
            start = val
            curVal = key
            weights = []
        weights.append(weight)
    ret.append(tup(start, listInd[-1], weights))
    return ret


#
# softSelections
#


def _getSingleComponent(component, dagPath, componentFn, softOn):
    elementIndices = []
    elementWeights = []

    count = componentFn.elementCount()
    if componentFn.componentType() == OpenMaya.MFn.kMeshPolygonComponent:
        polyIter = OpenMaya.MItMeshPolygon(dagPath, component)
        setOfVerts = set()
        while not polyIter.isDone():
            connectedVertices = OpenMaya.MIntArray()
            polyIter.getVertices(connectedVertices)
            for j in range(connectedVertices.length()):
                setOfVerts.add(connectedVertices[j])
            polyIter.next()
        lstVerts = list(setOfVerts)
        lstVerts.sort()
        for vtx in lstVerts:
            elementIndices.append(vtx)
            elementWeights.append(1)

    elif componentFn.componentType() == OpenMaya.MFn.kMeshEdgeComponent:
        edgeIter = OpenMaya.MItMeshEdge(dagPath, component)
        setOfVerts = set()
        while not edgeIter.isDone():
            for j in [0, 1]:
                setOfVerts.add(edgeIter.index(j))
            edgeIter.next()
        lstVerts = list(setOfVerts)
        lstVerts.sort()
        for vtx in lstVerts:
            elementIndices.append(vtx)
            elementWeights.append(1)

    else:
        # regular vertices or softSelection
        singleFn = OpenMaya.MFnSingleIndexedComponent(component)
        for i in range(count):
            weight = componentFn.weight(i).influence() if softOn else 1
            elementIndices.append(singleFn.element(i))
            elementWeights.append(weight)
    return elementIndices, elementWeights


def _getSurfaceCVComponent(component, dagPath, componentFn, softOn, returnSimpleIndices):
    count = componentFn.elementCount()
    uVal = OpenMaya.MScriptUtil()
    uVal.createFromInt(0)
    ptru = uVal.asIntPtr()

    vVal = OpenMaya.MScriptUtil()
    vVal.createFromInt(0)
    ptrv = vVal.asIntPtr()

    depNode_name = dagPath.fullPathName()

    numCVsInV_ = cmds.getAttr(depNode_name + ".spansV") + cmds.getAttr(depNode_name + ".degreeV")

    doubleFn = OpenMaya.MFnDoubleIndexedComponent(component)
    elementWeights = []
    elementIndices = []
    for i in range(count):
        weight = componentFn.weight(i).influence() if softOn else 1
        doubleFn.getElement(i, ptru, ptrv)
        u = uVal.getInt(ptru)
        v = vVal.getInt(ptrv)
        if returnSimpleIndices:
            elementIndices.append(numCVsInV_ * u + v)
        else:
            elementIndices.append((u, v))
        elementWeights.append(weight)
    return elementIndices, elementWeights


def _getLatticeComponent(component, dagPath, componentFn, softOn, returnSimpleIndices):
    count = componentFn.elementCount()
    uVal = OpenMaya.MScriptUtil()
    uVal.createFromInt(0)
    ptru = uVal.asIntPtr()

    vVal = OpenMaya.MScriptUtil()
    vVal.createFromInt(0)
    ptrv = vVal.asIntPtr()

    wVal = OpenMaya.MScriptUtil()
    wVal.createFromInt(0)
    ptrw = wVal.asIntPtr()

    depNode_name = dagPath.fullPathName()

    div_s = cmds.getAttr(depNode_name + ".sDivisions")
    div_t = cmds.getAttr(depNode_name + ".tDivisions")
    div_u = cmds.getAttr(depNode_name + ".uDivisions")

    tripleFn = OpenMaya.MFnTripleIndexedComponent(component)
    elementWeights = []
    elementIndices = []
    for i in range(count):
        tripleFn.getElement(i, ptru, ptrv, ptrw)
        s = uVal.getInt(ptru)
        t = vVal.getInt(ptrv)
        u = wVal.getInt(ptrw)
        simpleIndex = getThreeIndices(div_s, div_t, div_u, s, t, u)
        weight = componentFn.weight(i).influence() if softOn else 1

        if returnSimpleIndices:
            elementIndices.append(simpleIndex)
        else:
            elementIndices.append((s, t, u))
        elementWeights.append(weight)
    return elementIndices, elementWeights


def componentIter(selList):
    """An iterator for maya iterators so we can just use for-loops
    This way we don't have to worry so much about calling .next()
    any time we continue the loop

    Arguments:
        selList (MSelectionList): A maya selection list to iterate over

    Yields:
        MDagPath, MObject: The dag path and component objects
    """
    selIter = OpenMaya.MItSelectionList(selList)
    while not selIter.isDone():
        component = OpenMaya.MObject()
        dagPath = OpenMaya.MDagPath()
        try:
            selIter.getDagPath(dagPath, component)
        except RuntimeError:
            # The item is not a dag item
            selIter.next()
            continue

        if component.isNull():
            selIter.next()
            continue

        yield dagPath, component
        selIter.next()


def getSoftSelectionValues(returnSimpleIndices=True, forceReturnWeight=False):
    softOn = cmds.softSelect(q=True, softSelectEnabled=True)
    richSelList = OpenMaya.MSelectionList()

    if softOn:
        richSel = OpenMaya.MRichSelection()
        try:
            OpenMaya.MGlobal.getRichSelection(richSel)
        except RuntimeError:
            return {}
        richSel.getSelection(richSelList)
    else:
        OpenMaya.MGlobal.getActiveSelectionList(richSelList)

    toReturn = {}
    if richSelList.isEmpty():
        return toReturn

    for dagPath, component in componentIter(richSelList):
        componentFn = OpenMaya.MFnComponent(component)
        if componentFn.componentType() in [
            OpenMaya.MFn.kCurveCVComponent,
            OpenMaya.MFn.kMeshVertComponent,
            OpenMaya.MFn.kMeshPolygonComponent,
            OpenMaya.MFn.kMeshEdgeComponent,
        ]:
            elementIndices, elementWeights = _getSingleComponent(
                component, dagPath, componentFn, softOn
            )

        elif componentFn.componentType() == OpenMaya.MFn.kSurfaceCVComponent:
            elementIndices, elementWeights = _getSurfaceCVComponent(
                component, dagPath, componentFn, softOn, returnSimpleIndices
            )

        elif componentFn.componentType() == OpenMaya.MFn.kLatticeComponent:
            elementIndices, elementWeights = _getLatticeComponent(
                component, dagPath, componentFn, softOn, returnSimpleIndices
            )
        else:
            raise ValueError("Don't know how to get component indices from this type")

        if forceReturnWeight or softOn:
            toReturn[dagPath.fullPathName()] = (elementIndices, elementWeights)
        else:
            toReturn[dagPath.fullPathName()] = elementIndices

    return toReturn


def getThreeIndices(div_s, div_t, div_u, *args):
    if len(args) == 1:
        (simpleIndex,) = args
        s = simpleIndex % div_s
        t = (simpleIndex - s) // div_s % div_t
        u = (simpleIndex - s - t * div_s) // (div_s * div_t)
        return s, t, u
    elif len(args) == 3:
        s, t, u = args
        simpleIndex = u * div_s * div_t + t * div_s + s
        return simpleIndex
    raise ValueError("Invalid Arguments")


def _fillComponentObject(componentObj, selPath):
    componentSelList = OpenMaya.MSelectionList()
    componentSelList.clear()
    objName = selPath.partialPathName()

    # Transform
    if selPath.apiType() == OpenMaya.MFn.kTransform:
        numShapesUtil = OpenMaya.MScriptUtil()
        numShapesUtil.createFromInt(0)
        numShapesPtr = numShapesUtil.asUintPtr()
        selPath.numberOfShapesDirectlyBelow(numShapesPtr)
        numShapes = OpenMaya.MScriptUtil(numShapesPtr).asUint()
        selPath.extendToShapeDirectlyBelow(numShapes - 1)

    # Mesh
    if selPath.apiType() == OpenMaya.MFn.kMesh:
        meshFn = OpenMaya.MFnMesh(selPath.node())
        vtxCount = meshFn.numVertices()
        componentSelList.add(objName + ".vtx[0:" + str(vtxCount - 1) + "]")
    # Curve
    elif selPath.apiType() == OpenMaya.MFn.kNurbsCurve:
        curveFn = OpenMaya.MFnNurbsCurve(selPath.node())
        componentSelList.add(objName + ".cv[0:" + str(curveFn.numCVs() - 1) + "]")
    # Surface
    elif selPath.apiType() == OpenMaya.MFn.kNurbsSurface:
        surfaceFn = OpenMaya.MFnNurbsSurface(selPath.node())
        componentSelList.add(
            objName
            + ".cv[0:"
            + str(surfaceFn.numCVsInU() - 1)
            + "][0:"
            + str(surfaceFn.numCVsInV() - 1)
            + "]"
        )
    # Lattice
    elif selPath.apiType() == OpenMaya.MFn.kLattice:
        sDiv = cmds.getAttr(objName + ".sDivisions")
        tDiv = cmds.getAttr(objName + ".tDivisions")
        uDiv = cmds.getAttr(objName + ".uDivisions")
        componentSelList.add(
            objName
            + ".pt[0:"
            + str(sDiv - 1)
            + "][0:"
            + str(tDiv - 1)
            + "][0:"
            + str(uDiv - 1)
            + "]"
        )

    # Get object component MObject
    componentSelList.getDagPath(0, selPath, componentObj)


def getComponentIndexList(componentList=None):
    # https://github.com/bungnoid/glTools/blob/master/utils/component.py
    """
    Return an list of integer component index values
    @param componentList: A list of component names. if empty will default to selection.
    @type componentList: list
    """
    # Initialize return dictionary
    componentIndexList = {}

    # Check string input
    if componentList is None:
        componentList = []

    if isinstance(componentList, six.string_types):
        componentList = [componentList]

    # Get selection if componentList is empty
    if not componentList:
        componentList = cmds.ls(sl=True, flatten=True) or []
    if not componentList:
        return []

    # Get MSelectionList
    selList = OpenMaya.MSelectionList()
    for i in componentList:
        selList.add(str(i))

    # Iterate through selection list
    selPath = OpenMaya.MDagPath()
    componentObj = OpenMaya.MObject()
    for i in range(selList.length()):
        # Check for valid component selection
        selList.getDagPath(i, selPath, componentObj)
        if componentObj.isNull():
            _fillComponentObject(componentObj, selPath)

        # MESH / NURBS CURVE
        if selPath.apiType() in (OpenMaya.MFn.kMesh, OpenMaya.MFn.kNurbsCurve):
            indexList = OpenMaya.MIntArray()
            componentFn = OpenMaya.MFnSingleIndexedComponent(componentObj)
            componentFn.getElements(indexList)
            componentIndexList[selPath.partialPathName()] = list(indexList)
        # NURBS SURFACE
        if selPath.apiType() == OpenMaya.MFn.kNurbsSurface:
            indexListU = OpenMaya.MIntArray()
            indexListV = OpenMaya.MIntArray()
            componentFn = OpenMaya.MFnDoubleIndexedComponent(componentObj)
            componentFn.getElements(indexListU, indexListV)
            componentIndexList[selPath.partialPathName()] = list(
                zip(list(indexListU), list(indexListV))
            )
        # LATTICE
        if selPath.apiType() == OpenMaya.MFn.kLattice:
            indexListS = OpenMaya.MIntArray()
            indexListT = OpenMaya.MIntArray()
            indexListU = OpenMaya.MIntArray()
            componentFn = OpenMaya.MFnTripleIndexedComponent(componentObj)
            componentFn.getElements(indexListS, indexListT, indexListU)
            componentIndexList[selPath.partialPathName()] = list(
                zip(list(indexListS), list(indexListT), list(indexListU))
            )
    # Return Result
    return componentIndexList


#
# get UV Map
def getMapForSelectedVerticesFromSelection(normalize=True, opp=False, axis="uv"):
    # Get MSelectionList
    selList = OpenMaya.MSelectionList()
    OpenMaya.MGlobal.getActiveSelectionList(selList)

    iterSel = OpenMaya.MItSelectionList(selList)

    util = OpenMaya.MScriptUtil()
    util.createFromList([0.0, 0.0], 2)
    uvPoint = util.asFloat2Ptr()
    indicesValues = []
    while not iterSel.isDone():
        component = OpenMaya.MObject()
        dagPath = OpenMaya.MDagPath()
        iterSel.getDagPath(dagPath, component)
        if not component.isNull():
            componentFn = OpenMaya.MFnComponent(component)
            if componentFn.componentType() == OpenMaya.MFn.kMeshVertComponent:  # vertex
                vertIter = OpenMaya.MItMeshVertex(dagPath, component)
                while not vertIter.isDone():
                    theVert = vertIter.index()
                    vertIter.getUV(uvPoint)
                    u = OpenMaya.MScriptUtil.getFloat2ArrayItem(uvPoint, 0, 0)
                    v = OpenMaya.MScriptUtil.getFloat2ArrayItem(uvPoint, 0, 1)
                    indicesValues.append((theVert, u, v))
                    vertIter.next()
        iterSel.next()
    if normalize:
        maxV = max(indicesValues, key=lambda x: x[2])[2]
        minV = min(indicesValues, key=lambda x: x[2])[2]
        diffV = maxV - minV

        maxU = max(indicesValues, key=lambda x: x[1])[1]
        minU = min(indicesValues, key=lambda x: x[1])[1]
        diffU = maxU - minU

        indicesValues = [
            (theVert, (u - minU) / diffU, (v - minU) / diffV) for (theVert, u, v) in indicesValues
        ]
    if opp:
        indicesValues = [(theVert, -1.0 * u, -1.0 * v) for (theVert, u, v) in indicesValues]
    if axis != "uv":
        indReturn = "uv".index(axis) + 1
        indicesValues = [(el[0], el[indReturn]) for el in indicesValues]

    return indicesValues


def getMapForSelectedVertices(vertIter, normalize=True, opp=False, axis="uv"):
    # Get MSelectionList
    util = OpenMaya.MScriptUtil()
    util.createFromList([0.0, 0.0], 2)
    uvPoint = util.asFloat2Ptr()
    indicesValues = []

    while not vertIter.isDone():
        theVert = vertIter.index()
        vertIter.getUV(uvPoint)
        u = OpenMaya.MScriptUtil.getFloat2ArrayItem(uvPoint, 0, 0)
        v = OpenMaya.MScriptUtil.getFloat2ArrayItem(uvPoint, 0, 1)
        indicesValues.append((theVert, u, v))
        vertIter.next()

    if normalize:
        maxV = max(indicesValues, key=lambda x: x[2])[2]
        minV = min(indicesValues, key=lambda x: x[2])[2]
        diffV = maxV - minV

        maxU = max(indicesValues, key=lambda x: x[1])[1]
        minU = min(indicesValues, key=lambda x: x[1])[1]
        diffU = maxU - minU

        indicesValues = [
            (theVert, (u - minU) / diffU, (v - minU) / diffV) for (theVert, u, v) in indicesValues
        ]
    if opp:
        indicesValues = [(theVert, 1.0 - u, 1.0 - v) for (theVert, u, v) in indicesValues]
    if axis != "uv":
        indReturn = "uv".index(axis) + 1
        indicesValues = [(el[0], el[indReturn]) for el in indicesValues]

    return indicesValues


#
# callBacks
#
def deleteTheJobs(toSearch="BrushFunctions.callAfterPaint"):
    res = cmds.scriptJob(listJobs=True)
    for job in res:
        if toSearch in job:
            jobIndex = int(job.split(":")[0])
            cmds.scriptJob(kill=jobIndex)


def addNameChangedCallback(callback):
    def omcallback(mobject, oldname, _):
        newname = OpenMaya.MFnDependencyNode(mobject).name()
        callback(oldname, newname)  #

    listenTo = OpenMaya.MObject()
    return OpenMaya.MNodeMessage.addNameChangedCallback(listenTo, omcallback)


def removeNameChangedCallback(callbackId):
    OpenMaya.MNodeMessage.removeCallback(callbackId)


def addUserEventCallback(eventName, callback):
    # Go through this closure to strip out the client data
    # That we can't read because maya only passes void pointers
    # from user even callbacks
    def omcallback(clientData=None):
        callback()

    # Add the callback to the list of functions which should execute when your plugin event is posted
    return OpenMaya.MUserEventMessage.addUserEventCallback(eventName, omcallback)


def removeUserEventCallback(callbackId):
    OpenMaya.MUserEventMessage.removeCallback(callbackId)
