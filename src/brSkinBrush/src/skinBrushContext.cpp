
#include "skinBrushFlags.h"
#include "skinBrushTool.h"

// ---------------------------------------------------------------------
// the context
// ---------------------------------------------------------------------


// ---------------------------------------------------------------------
// general methods when calling the context
// ---------------------------------------------------------------------
SkinBrushContext::SkinBrushContext() {
    setTitleString("Smooth Weights");
    setImage("brSkinBrush.svg", MPxContext::kImage1);
    setCursor(MCursor::editCursor);

    // Define the default values for the context.
    // These values will be used to reset the tool from the tool
    // properties window.
    performRefreshViewPort = 0;

    colorVal = MColor(1.0, 0.0, 0.0);
    curveVal = 2;
    drawBrushVal = true;
    drawRangeVal = true;
    moduleImportString = MString("from mPaintEditor.brushTools.brushPythonFunctions import ");
    enterToolCommandVal = "";
    exitToolCommandVal = "";
    fractionOversamplingVal = false;
    ignoreLockVal = false;
    lineWidthVal = 1;
    messageVal = 2;
    oversamplingVal = 1;
    rangeVal = 0.5;
    sizeVal = 5.0;
    strengthVal = 0.25;
    smoothStrengthVal = 1.0;

    pruneWeight = 0.0001;

    undersamplingVal = 2;
    volumeVal = false;
    coverageVal = true;
    postSetting = true;

    commandIndex = ModifierCommands::Add;
    soloColorTypeVal = 1;  // 1 lava
    soloColorVal = 0;
    // True, only if the smoothing is performed. False when adjusting
    // the brush settings. It's used to control whether undo/redo needs
    // to get called.
    performBrush = false;
    firstPaintDone = false;
}

void SkinBrushContext::toolOnSetup(MEvent &) {
    MStatus status = MStatus::kSuccess;

    setHelpString("it's a custom brush weights.");
    setInViewMessage(true);

    if (enterToolCommandVal.length() > 5) MGlobal::executeCommand(enterToolCommandVal);
    MGlobal::executePythonCommand(
        moduleImportString +
        MString(
            "toolOnSetupEnd, "
            "toolOnSetupStart\n"
        ));
    MGlobal::executePythonCommand("toolOnSetupStart()");
    MUserEventMessage::postUserEvent("brSkinBrush_toolOnSetupStart");

    this->firstPaintDone = false;
    this->pickMaxInfluenceVal = false;
    this->pickInfluenceVal = false;

    // first clear a bit the air --------------
    this->multiCurrentColors.clear();
    this->jointsColors.clear();
    this->soloCurrentColors.clear();

    status = getMesh();
    MIntArray editVertsIndices;
    if (!skinObj.isNull()) {
        // get the joints colors
        getListColorsJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, jointsColors);

        this->skinWeightList.clear();
        this->ignoreLockJoints = MIntArray(this->nbJoints, 0);
        if (this->mirrorInfluences.length() == 0) {
            this->mirrorInfluences = MIntArray(this->nbJoints, 0);
            for (unsigned int i = 0; i < this->nbJoints; ++i) this->mirrorInfluences.set(i, i);
        }

        getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
        getListLockVertices(skinObj, this->lockVertices, editVertsIndices);

        status = fillArrayValues(skinObj, true);  // WAY TOO SLOW ... but accurate ?
    } else {
        MGlobal::displayInfo(MString("FAILED : skinObj.isNull"));
        abortAction();
        return;
    }
    // get face color assignments ----------

    // solo colors -----------------------
    this->soloCurrentColors = MColorArray(this->numVertices, MColor(0.0, 0, 0.0));
    this->soloColorsValues = MDoubleArray(this->numVertices, 0.0);

    MStringArray currentColorSets;
    meshFn.getColorSetNames(currentColorSets);
    if (currentColorSets.indexOf(this->fullColorSet) == -1)  // multiColor
        meshFn.createColorSetWithName(this->fullColorSet);

    if (currentColorSets.indexOf(this->soloColorSet) == -1)  // soloColor
        meshFn.createColorSetWithName(this->soloColorSet);

    if (currentColorSets.indexOf(this->fullColorSet2) == -1)  // multiColor
        meshFn.createColorSetWithName(this->fullColorSet2);

    if (currentColorSets.indexOf(this->soloColorSet2) == -1)  // soloColor
        meshFn.createColorSetWithName(this->soloColorSet2);

    meshFn.setColors(this->multiCurrentColors, &this->fullColorSet);  // set the multi assignation
    meshFn.assignColors(fullVertexList, &this->fullColorSet);

    meshFn.setColors(this->soloCurrentColors, &this->soloColorSet);  // set the solo assignation
    meshFn.assignColors(fullVertexList, &this->soloColorSet);

    meshFn.setColors(this->multiCurrentColors, &this->fullColorSet2);  // set the multi assignation
    meshFn.assignColors(fullVertexList, &this->fullColorSet2);

    meshFn.setColors(this->soloCurrentColors, &this->soloColorSet2);  // set the solo assignation
    meshFn.assignColors(fullVertexList, &this->soloColorSet2);

    MString currentColorSet = meshFn.currentColorSetName();  // set multiColor as current Color
    if (soloColorVal == 1) {                                 // solo
        if (currentColorSet != this->soloColorSet)
            meshFn.setCurrentColorSetName(this->soloColorSet);
        editSoloColorSet(true);
    } else {
        if (currentColorSet != this->fullColorSet)
            meshFn.setCurrentColorSetName(this->fullColorSet);  // , &this->colorSetMod);
    }

    // display the locks
    MColorArray multiEditColors, soloEditColors;
    refreshColors(editVertsIndices, multiEditColors, soloEditColors);
    this->skinValuesToSet.clear();

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet);

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet2);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet2);

    meshFn.setDisplayColors(true);

    view = M3dView::active3dView();
    view.refresh(false, true);

    MGlobal::executePythonCommand("toolOnSetupEnd()");
    MUserEventMessage::postUserEvent("brSkinBrush_toolOnSetupEnd");
}

void SkinBrushContext::toolOffCleanup() {
    setInViewMessage(false);
    meshFn.updateSurface();  // try avoiding crashes
    if (exitToolCommandVal.length() > 5) MGlobal::executeCommand(exitToolCommandVal);
    MUserEventMessage::postUserEvent("brSkinBrush_toolOffCleanup");
    if (!this->firstPaintDone) {
        this->firstPaintDone = true;
        MUserEventMessage::postUserEvent("brSkinBrush_cleanCloseUndo");
    }

}

void SkinBrushContext::getClassName(MString &name) const { name.set("brSkinBrush"); }

void SkinBrushContext::refreshJointsLocks() {
    if (!skinObj.isNull()) {
        // Get the skin cluster node from the history of the mesh.
        getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
    }
}

void SkinBrushContext::refreshMirrorInfluences(MIntArray &inputMirrorInfluences) {
    this->mirrorInfluences.clear();
    this->mirrorInfluences.copy(inputMirrorInfluences);
}

void SkinBrushContext::refreshTheseVertices(MIntArray &verticesIndices) {
    // this command is used when undo is called
    querySkinClusterValues(this->skinObj, verticesIndices, this->skinWeightList, true);
    // query the Locks
    getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
    MIntArray editVertsIndices;
    getListLockVertices(skinObj, this->lockVertices, editVertsIndices);

    if (!meshDag.isValid()) {
        return;
    }
    // points and normals
    refreshPointsNormals();

    MColorArray multiEditColors, soloEditColors;
    refreshColors(verticesIndices, multiEditColors, soloEditColors);
    this->skinValuesToSet.clear();
    meshFn.setSomeColors(verticesIndices, multiEditColors, &this->fullColorSet);
    meshFn.setSomeColors(verticesIndices, soloEditColors, &this->soloColorSet);

    meshFn.setSomeColors(verticesIndices, multiEditColors, &this->fullColorSet2);
    meshFn.setSomeColors(verticesIndices, soloEditColors, &this->soloColorSet2);

    // if locking or unlocking
    // without that it doesn't refresh because mesh is not invalidated, meaning the skinCluster
    // hasn't changed
    meshFn.updateSurface();

    // refresh view and display
    maya2019RefreshColors();

    this->previousPaint.clear();
    this->previousMirrorPaint.clear();
}

void SkinBrushContext::refreshDeformerColor(int deformerInd) {
    if (!skinObj.isNull()) {
        getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
        getListColorsJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->jointsColors);
    } else {
        MGlobal::displayInfo(MString("FAILED : skinObj.isNull"));
        return;
    }

    // get the vertices indices to edit -------------------
    MIntArray editVertsIndices;
    for (unsigned int theVert = 0; theVert < this->numVertices; ++theVert) {
        double theWeight = 0.0;
        int ind_swl = theVert * this->nbJoints + deformerInd;
        if (ind_swl < this->skinWeightList.length())
            theWeight = this->skinWeightList[ind_swl];
        if (theWeight != 0.0) {
            editVertsIndices.append(theVert);
        }
    }

    // display the locks ----------------------
    MColorArray multiEditColors, soloEditColors;
    refreshColors(editVertsIndices, multiEditColors, soloEditColors);
    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet);

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet2);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet2);

    if (soloColorVal == 1) editSoloColorSet(true);  // solo
    // refresh view and display
    meshFn.updateSurface();
    maya2019RefreshColors();
}

void SkinBrushContext::refresh() {
    MStatus status;
    refreshPointsNormals();
    MIntArray editVertsIndices;

    if (!skinObj.isNull()) {
        // Get the skin cluster node from the history of the mesh.
        getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
        getListColorsJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->jointsColors);
        status = getListLockVertices(skinObj, this->lockVertices, editVertsIndices);  // problem ?
        status = fillArrayValuesDEP(skinObj, true);  // get the skin data and all the colors
    } else {
        MGlobal::displayError(MString("FAILED : skinObj.isNull"));
        return;
    }

    this->skinValuesToSet.clear();

    meshFn.setColors(this->multiCurrentColors, &this->fullColorSet);  // set the multi assignation
    meshFn.setColors(this->soloCurrentColors, &this->soloColorSet);   // set the solo assignation

    meshFn.setColors(this->multiCurrentColors, &this->fullColorSet2);  // set the multi assignation
    meshFn.setColors(this->soloCurrentColors, &this->soloColorSet2);   // set the solo assignation

    // display the locks ----------------------
    MColorArray multiEditColors, soloEditColors;
    refreshColors(editVertsIndices, multiEditColors, soloEditColors);
    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet);

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet2);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet2);

    if (soloColorVal == 1) editSoloColorSet(true);  // solo

    // refresh view and display
    meshFn.updateSurface();

    maya2019RefreshColors();
}

// ---------------------------------------------------------------------
// viewport 2.0
// ---------------------------------------------------------------------
int SkinBrushContext::getClosestInfluenceToCursor(int screenX, int screenY) {
    MStatus stat;
    MPoint nearClipPt, farClipPt;
    MVector direction, direction2;
    MPoint orig, orig2;
    view.viewToWorld(screenX, screenY, orig, direction);

    int lent = this->inflDagPaths.length();
    int closestInfluence = -1;
    double closestDistance = -1;

    for (unsigned int i = 0; i < lent; i++) {
        MMatrix matI = BBoxOfDeformers[i].mat.inverse();
        orig2 = orig * matI;
        direction2 = direction * matI;

        MPoint minPt = BBoxOfDeformers[i].minPt;
        MPoint maxPt = BBoxOfDeformers[i].maxPt;
        MPoint center = BBoxOfDeformers[i].center;

        bool intersect = RayIntersectsBBox(minPt, maxPt, orig2, direction2);
        if (intersect) {
            double dst = center.distanceTo(orig2);
            if ((closestInfluence == -1) || (dst < closestDistance)) {
                closestDistance = dst;
                closestInfluence = i;
            }
        }
    }
    return closestInfluence;
}

int SkinBrushContext::getHighestInfluence(int faceHit, MFloatPoint &hitPoint) {
    // get closest vertex
    auto verticesSet = getSurroundingVerticesPerFace(faceHit);
    int indexVertex = -1;
    float closestDist;
    for (int ptIndex : verticesSet) {
        MFloatPoint posPoint(this->mayaRawPoints[ptIndex * 3], this->mayaRawPoints[ptIndex * 3 + 1],
                             this->mayaRawPoints[ptIndex * 3 + 2]);
        float dist = posPoint.distanceTo(hitPoint);
        if (indexVertex == -1 || dist < closestDist) {
            indexVertex = ptIndex;
            closestDist = dist;
        }
    }
    // now get highest influence for this vertex

    int biggestInfluence = -1;
    double biggestVal = 0;
    std::vector<double> allWeights;
    for (int indexInfluence = 0; indexInfluence < this->nbJoints; ++indexInfluence) {
        double theWeight = 0.0;
        int ind_swl = indexVertex * this->nbJoints + indexInfluence;
        if (ind_swl < this->skinWeightList.length())
            theWeight = this->skinWeightList[ind_swl];
        allWeights.push_back(theWeight);
        if (theWeight > biggestVal) {
            biggestVal = theWeight;
            biggestInfluence = indexInfluence;
        }
    }
    // now sort the allWights array (I found that online hoepfully it works)
    std::vector<int> indices;
    indices.resize(this->nbJoints);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(),
              [&](int i, int j) { return allWeights[i] > allWeights[j]; });

    // now we transfer that to our UI
    this->orderedIndicesByWeights = MString("");
    this->orderedIndicesByWeightsVals.clear();
    for (int ind : indices) {
        this->orderedIndicesByWeights += MString("") + ind + MString(" ");
        this->orderedIndicesByWeightsVals.append(ind);
    }
    return biggestInfluence;
}

MStatus SkinBrushContext::drawTheMesh(MHWRender::MUIDrawManager &drawManager, MVector &worldVector) {
    drawManager.setColor(MColor(0.5, 0.5, 0.5, 1.0));
    drawManager.setLineWidth(1);

    MPointArray edgeVertices;
    for (const auto &pairEdges : this->perEdgeVertices) {
        double multVal = worldVector * this->verticesNormals[pairEdges.first];
        double multVal2 = worldVector * this->verticesNormals[pairEdges.second];
        if ((multVal > 0.0) && (multVal2 > 0.0)) {
            continue;
        }
        float element[4] = {this->mayaRawPoints[pairEdges.first * 3],
                            this->mayaRawPoints[pairEdges.first * 3 + 1],
                            this->mayaRawPoints[pairEdges.first * 3 + 2], 0};
        edgeVertices.append(element);

        float element2[4] = {this->mayaRawPoints[pairEdges.second * 3],
                             this->mayaRawPoints[pairEdges.second * 3 + 1],
                             this->mayaRawPoints[pairEdges.second * 3 + 2], 0};
        edgeVertices.append(element2);
        // use the normals to paint ...
    }
    drawManager.setColor(MColor(1., 0., 0., .5));
    drawManager.setDepthPriority(1);
    drawManager.mesh(MHWRender::MUIDrawManager::kLines, edgeVertices);
    return MS::kSuccess;
}

MStatus SkinBrushContext::doPtrMoved(MEvent &event, MHWRender::MUIDrawManager &drawManager,
                                     const MHWRender::MFrameContext &context) {
    event.getPosition(screenX, screenY);
    bool displayPickInfluence = this->pickMaxInfluenceVal || this->pickInfluenceVal;
    if (this->pickInfluenceVal) {
        // -------------------------------------------------------------------------------------------------
        // start fill jnts boundingBox
        // --------------------------------------------------------------------
        if (this->BBoxOfDeformers.size() == 0) {  // fill it
            double jointDisplayVal;
            MGlobal::executeCommand("jointDisplayScale -query", jointDisplayVal);
            MGlobal::displayInfo(MString("jointDisplayScale :  ") + jointDisplayVal);

            int lent = this->inflDagPaths.length();
            MPoint zero(0, 0, 0);
            MVector up(0, 1, 0);
            MVector right(1, 0, 0);
            MVector side(0, 0, 1);
            for (unsigned int i = 0; i < lent; i++) {  // for all deformers
                MDagPath path = this->inflDagPaths[i];
                drawingDeformers newDef;

                MMatrix worldMatrix = path.inclusiveMatrix();        // worldMatrix
                MMatrix parentMatrix = path.exclusiveMatrix();       // parentMatrix
                MMatrix mat = worldMatrix * parentMatrix.inverse();  // matrix

                MBoundingBox bbox;

                right = MVector(worldMatrix[0]);
                up = MVector(worldMatrix[1]);
                side = MVector(worldMatrix[2]);

                unsigned int nbShapes;
                path.numberOfShapesDirectlyBelow(nbShapes);
                if (nbShapes != 0) {
                    path.extendToShapeDirectlyBelow(0);
                    MFnDagNode dag(path);
                    bbox = dag.boundingBox();  // Returns the bounding box for the dag node in
                                               // object space.

                    MPoint center = bbox.center() * worldMatrix;
                    newDef.center = center;
                    newDef.width = 0.5 * bbox.width() * right.length();
                    newDef.height = 0.5 * bbox.height() * up.length();
                    newDef.depth = 0.5 * bbox.depth() * side.length();

                    newDef.mat = worldMatrix;
                    newDef.minPt = bbox.min();
                    newDef.maxPt = bbox.max();
                } else {
                    MFnDagNode dag(path);
                    MStatus plugStat;
                    MPlug radiusPlug = dag.findPlug("radius", false, &plugStat);
                    double multVal = jointDisplayVal;
                    if (plugStat == MStatus::kSuccess) {
                        multVal *= radiusPlug.asDouble();
                    }

                    newDef.center = zero * worldMatrix;
                    newDef.width = 0.5 * right.length() * multVal;
                    newDef.height = 0.5 * up.length() * multVal;
                    newDef.depth = 0.5 * side.length() * multVal;

                    newDef.mat = worldMatrix;
                    newDef.minPt = multVal * MPoint(-0.5, -0.5, -0.5);
                    newDef.maxPt = multVal * MPoint(0.5, 0.5, 0.5);
                }
                newDef.up = up;
                newDef.right = right;

                BBoxOfDeformers.push_back(newDef);
            }
        }  // end fill it

        // end fill jnts boundingBox
        // --------------------------------------------------------------------
        // -----------------------------------------------------------------------------------------------
        biggestInfluence = getClosestInfluenceToCursor(screenX, screenY);
    }

    int faceHit;
    successFullHit = computeHit(screenX, screenY, true, faceHit, this->centerOfBrush);

    if (!successFullHit && !this->refreshDone) {  // try to re-get accelParams in case no hit
        refreshPointsNormals();
        successFullHit = computeHit(screenX, screenY, true, faceHit, this->centerOfBrush);
        this->refreshDone = true;
    }

    if (!successFullHit && !displayPickInfluence) return MStatus::kNotFound;

    drawManager.beginDrawable();
    drawManager.setColor(MColor(0.0, 0.0, 1.0));
    drawManager.setLineWidth((float)lineWidthVal);
    MColor biggestInfluenceColor(1.0, 0.0, 0.0);

    if (this->pickMaxInfluenceVal || this->pickInfluenceVal) {
        if (this->pickInfluenceVal) {
            // ---------------------------------------------------------------------------------------
            // start reDraw jnts
            // --------------------------------------------------------------------
            int lent = this->inflDagPaths.length();
            drawManager.setColor(MColor(0.0, 0.0, 0.0));
            for (unsigned int i = 0; i < lent; i++) {
                bool fillDraw = i == biggestInfluence;
                if (i == biggestInfluence) {
                    drawManager.setColor(biggestInfluenceColor);
                } else {
                    if (i == this->influenceIndex) fillDraw = true;
                    drawManager.setColor(jointsColors[i]);
                }
                drawingDeformers bbosDfm = BBoxOfDeformers[i];
                drawManager.box(bbosDfm.center, bbosDfm.up, bbosDfm.right, bbosDfm.width,
                                bbosDfm.height, bbosDfm.depth, fillDraw);
            }
            // end reDraw jnts
            // ----------------------------------------------------------------------
            // ---------------------------------------------------------------------------------------
        }

        drawManager.setFontSize(14);
        drawManager.setFontName(MString("MS Shell Dlg 2"));
        drawManager.setFontWeight(1);
        MColor Yellow(1.0, 1.0, 0.0);

        if (this->pickMaxInfluenceVal) {
            Yellow = MColor(1.0, 0.5, 0.0);
            if (successFullHit)
                biggestInfluence = getHighestInfluence(faceHit, this->centerOfBrush);
            else
                biggestInfluence = -1;
        }
        MString text("--");
        drawManager.setColor(MColor(0.0, 0.0, 0.0));

        int backgroundSize[] = {60, 20};
        if (biggestInfluence != -1) {
            text = this->inflNames[biggestInfluence];
            backgroundSize[0] = this->inflNamePixelSize[2 * biggestInfluence];
            backgroundSize[1] = this->inflNamePixelSize[2 * biggestInfluence + 1];
            worldPoint = worldPoint + .1 * worldVector.normal();
            drawManager.text(worldPoint, text, MHWRender::MUIDrawManager::TextAlignment::kCenter,
                             backgroundSize, &Yellow);
            // drawing full front camera
        } else {
            drawManager.text2d(MPoint(this->screenX, this->screenY, 0.0), text,
                               MHWRender::MUIDrawManager::TextAlignment::kCenter, backgroundSize,
                               &Yellow);
            // drawing behind bboxes
        }
    } else {
        drawManager.circle(this->centerOfBrush, this->normalVector, sizeVal);
        MVector worldVector;
        view.viewToWorld(this->screenX, this->screenY, worldPoint, worldVector);

        if (paintMirror != 0) {  // if mirror is not OFf
            // here paint the mirror Brush
            int faceMirrorHit;
            bool mirroredFound = getMirrorHit(faceMirrorHit, this->centerOfMirrorBrush);
            meshFn.getPolygonNormal(faceHit, this->normalMirroredVector, MSpace::kWorld);

            if (mirroredFound) {
                drawManager.setColor(MColor(0.0, 1.0, 1.0));
                drawManager.circle(this->centerOfMirrorBrush, this->normalMirroredVector, sizeVal);
            }
        }
    }
    drawManager.endDrawable();
    return MS::kSuccess;
}

MStatus SkinBrushContext::doPress(MEvent &event, MHWRender::MUIDrawManager &drawMgr,
                                  const MHWRender::MFrameContext &context) {
    pressStatus = doPressCommon(event);
    CHECK_MSTATUS_AND_RETURN_SILENT(pressStatus);
    doDrag(event, drawMgr, context);
    return MStatus::kSuccess;
}

MStatus SkinBrushContext::doDrag(MEvent &event, MHWRender::MUIDrawManager &drawManager,
                                 const MHWRender::MFrameContext &context) {
    MStatus status = MStatus::kSuccess;
    if (this->pickMaxInfluenceVal || this->pickInfluenceVal) {
        return MS::kFailure;
    }

    status = doDragCommon(event);
    if (this->postSetting && !this->useColorSetsWhilePainting) {
        drawManager.beginDrawable();
        drawMeshWhileDrag(drawManager);
        drawManager.endDrawable();
    }
    CHECK_MSTATUS_AND_RETURN_SILENT(status);

    // -----------------------------------------------------------------
    // display when painting or setting the brush size
    // -----------------------------------------------------------------
    if (this->drawBrushVal || (event.mouseButton() == MEvent::kMiddleMouse)) {
        CHECK_MSTATUS_AND_RETURN_SILENT(pressStatus);
        drawManager.beginDrawable();

        drawManager.setColor(MColor((pow(colorVal.r, 0.454f)), (pow(colorVal.g, 0.454f)),
                                    (pow(colorVal.b, 0.454f))));
        drawManager.setLineWidth((float)lineWidthVal);
        // Draw the circle in regular paint mode.
        // The range circle doens't get drawn here to avoid visual
        // clutter.
        if (event.mouseButton() == MEvent::kLeftMouse) {
            if (this->successFullDragHit)
                drawManager.circle(this->centerOfBrush, this->normalVector, sizeVal);
        }
        // Adjusting the brush settings with the middle mouse button.
        else if (event.mouseButton() == MEvent::kMiddleMouse) {
            // When adjusting the size the circle needs to remain with
            // a static position but the size needs to change.
            drawManager.setColor(MColor(1, 0, 1));

            if (sizeAdjust) {
                drawManager.circle(surfacePointAdjust, worldVectorAdjust, adjustValue);
                if (volumeVal && drawRangeVal)
                    drawManager.circle(surfacePointAdjust, worldVectorAdjust,
                                       adjustValue * rangeVal);
            }
            // When adjusting the strength the circle needs to remain
            // fixed and only the strength indicator changes.
            else {
                drawManager.circle(surfacePointAdjust, worldVectorAdjust, sizeVal);
                if (volumeVal && drawRangeVal)
                    drawManager.circle(surfacePointAdjust, worldVectorAdjust, sizeVal * rangeVal);

                MPoint start(startScreenX, startScreenY);
                MPoint end(startScreenX, startScreenY + adjustValue * 500);
                drawManager.line2d(start, end);

                drawManager.circle2d(end, lineWidthVal + 3.0, true);
            }
        }
        drawManager.endDrawable();
    }

    return status;
}

MStatus SkinBrushContext::drawMeshWhileDrag(MHWRender::MUIDrawManager &drawManager) {
    // This function is the hottest path when painting
    // So it can and should be optimized more
    // I think the endgame for this is to only update the changed vertices each runthrough
    int nbVtx = this->verticesPainted.size();

    MFloatPointArray points(nbVtx);
    MFloatVectorArray normals(nbVtx);
    MColor theCol(1, 1, 1), white(1, 1, 1, 1), black(0, 0, 0, 1);

    MColorArray pointsColors(nbVtx, theCol);

    MUintArray indices, indicesEdges;  // (nbVtx);
    MColorArray darkEdges;             // (nbVtx, MColor(0.5, 0.5, 0.5));

    MColor newCol, col;

    std::unordered_map<int, unsigned int> verticesMap;
    std::vector<bool> fatFaces_bitset;
    std::vector<bool> fatEdges_bitset;
    std::vector<bool> vertMap_bitset;

    fatFaces_bitset.resize(numFaces);
    fatEdges_bitset.resize(numEdges);
    vertMap_bitset.resize(numVertices);

    MColor baseColor, baseMirrorColor;
    float h, s, v;
    // get baseColor ----------------------------------
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // UnLockVertices
    ModifierCommands theCommandIndex = getCommandIndexModifiers();

    if (drawTransparency || drawPoints) {
        if (theCommandIndex == ModifierCommands::LockVertices)
            baseColor = this->lockVertColor;
        else if (theCommandIndex == ModifierCommands::Remove)
            baseColor = black;
        else if (theCommandIndex == ModifierCommands::UnlockVertices)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::Smooth)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::Sharpen)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::LockVertices)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::UnlockVertices)
            baseColor = white;
        else {
            baseColor = this->jointsColors[this->influenceIndex];
            if (this->paintMirror != 0) {
                baseMirrorColor = this->jointsColors[this->mirrorInfluences[this->influenceIndex]];
                baseMirrorColor.get(MColor::kHSV, h, s, v);
                baseMirrorColor.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15));
            }
        }
        baseColor.get(MColor::kHSV, h, s, v);
        baseColor.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15));
        if ((theCommandIndex != ModifierCommands::Add) && (theCommandIndex != ModifierCommands::AddPercent)) {
            baseMirrorColor = baseColor;
        }
    }

    // pull data out of the dictionary
    // TODO: There's probably a copy-less way to do this
    std::vector<std::pair<int, std::pair<float, float>>> mja;
    mja.reserve( this->mirroredJoinedArray.size());
    for (const auto &pt : this->mirroredJoinedArray) {
        mja.push_back(pt);
    }

    MColorArray colors, colorsSolo;
    colors.setLength(mja.size());
    colorsSolo.setLength(mja.size());

    MColorArray *usedColors;
    MColorArray *currentColors;
    if (this->soloColorVal == 1){
        usedColors = &colorsSolo;
        currentColors = &this->soloCurrentColors;
    }
    else {
        usedColors = &colors;
        currentColors = &this->multiCurrentColors;
    }

    bool doTransparency = drawTransparency;
    bool applyGamma = true;
    if (theCommandIndex == ModifierCommands::LockVertices || theCommandIndex == ModifierCommands::UnlockVertices){
        // Don't do transparency when locking/unlocking vertices
        doTransparency = false;
        applyGamma = false;
    }

#pragma omp parallel for
    for (unsigned i = 0; i < mja.size(); ++i){
        const auto &pt = mja[i];
        int ptIndex = pt.first;
        MFloatPoint posPoint(
            this->mayaRawPoints[ptIndex * 3],
            this->mayaRawPoints[ptIndex * 3 + 1],
            this->mayaRawPoints[ptIndex * 3 + 2]
        );
        posPoint = posPoint * this->inclusiveMatrix;
        points.set(posPoint, i);
        normals.set(verticesNormals[ptIndex], i);
    }

    if (drawTriangles) {
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            float weightBase = pt.second.first;
            float weightMirror = pt.second.second;
            MColor multColor, soloColor;
            this->getColorWithMirror(ptIndex, weightBase, weightMirror, colors, colorsSolo, multColor, soloColor);
            colors.set(multColor, i);
            colorsSolo.set(soloColor, i);
        }

        if (applyGamma){
#pragma omp parallel for
            for (unsigned i = 0; i < mja.size(); ++i){
                const auto &pt = mja[i];
                float weightBase = pt.second.first;
                float weightMirror = pt.second.second;
                float transparency = (doTransparency) ? weightBase + weightMirror: 1.0;
                MColor& colRef = (*usedColors)[i];
                colRef.get(MColor::kHSV, h, s, v);
                colRef.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15), transparency);
            }
        }
    }

    if (drawPoints) {
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            float weight = pt.second.first + pt.second.second;
            pointsColors[i] = weight * baseColor + (1.0 - weight) * (*currentColors)[pt.first];
        }
    }

    if (drawEdges) {
        darkEdges.setLength(mja.size());
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            float transparency = (doTransparency) ? pt.second.first + pt.second.second: 1.0;
            darkEdges.set(i, 0.5f, 0.5f, 0.5f, transparency);
        }
    }

    if (drawTriangles || drawEdges) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            verticesMap[pt.first] = i;
            vertMap_bitset[pt.first] = true;
        }
    }

    if (drawTriangles) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            for (int f : this->perVertexFaces[ptIndex]){
                fatFaces_bitset[f] = true;
            }
        }
    }

    if (drawEdges) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            for (int e : this->perVertexEdges[ptIndex]){
                fatEdges_bitset[e] = true;
            }
        }
    }

    if (drawTriangles) {
        // bitset is faster than an unordered_set in this case
        // may be worth keeping the bitsets around on the brush
        // so we don't have to constantly allocate memory
        for (unsigned f = 0; f<fatFaces_bitset.size(); ++f){
            if (!fatFaces_bitset[f]) continue;
            for (auto &tri : this->perFaceTriangleVertices[f]) {
                if (!vertMap_bitset[tri[0]]) continue;
                if (!vertMap_bitset[tri[1]]) continue;
                if (!vertMap_bitset[tri[2]]) continue;
                auto it0 = verticesMap.find(tri[0]);
                auto it1 = verticesMap.find(tri[1]);
                auto it2 = verticesMap.find(tri[2]);
                indices.append(it0->second);
                indices.append(it1->second);
                indices.append(it2->second);
            }
        }

        auto style = MHWRender::MUIDrawManager::kFlat;
        drawManager.setPaintStyle(style);  // kFlat // kShaded // kStippled
        drawManager.mesh(MHWRender::MUIDrawManager::kTriangles, points, &normals, usedColors, &indices);
    }

    if (drawEdges) {
        // bitset is faster than an unordered_set in this case
        // may be worth keeping the bitsets around on the brush
        // so we don't have to constantly allocate memory
        for (unsigned e = 0; e<fatEdges_bitset.size(); ++e){
            if (!fatEdges_bitset[e]) continue;
            auto &pairEdges = this->perEdgeVertices[e];

            if (!vertMap_bitset[pairEdges.first]) continue;
            if (!vertMap_bitset[pairEdges.second]) continue;
            auto it0 = verticesMap.find(pairEdges.first);
            auto it1 = verticesMap.find(pairEdges.second);
            indicesEdges.append(it0->second);
            indicesEdges.append(it1->second);

        }

        drawManager.setDepthPriority(2);
        drawManager.mesh(MHWRender::MUIDrawManager::kLines, points, &normals, &darkEdges, &indicesEdges);
    }

    if (drawPoints) {
        drawManager.setPointSize(4);
        drawManager.mesh(MHWRender::MUIDrawManager::kPoints, points, NULL, &pointsColors);
    }
    return MStatus::kSuccess;
}


MStatus drawMeshWhileDrag(
    // Pretty sure These are the only two things that change each frame
    std::set<int> &verticesPainted, // The full set of vertices that are currently being painted
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray, // An array of weights and mirror weights: <VertexIndex (weightBase, weightMirrored)>

    int numFaces, // The number of faces on the current mesh
    int numEdges, // The number of edges on the current mesh
    int numVertices, // The number of vertices in the current mesh
    int influenceIndex, // The index of the influence currently being painted
    int paintMirror, // The mirror behavior index
    int soloColorVal, // The solo color index

    bool drawTransparency, // Whether to draw transparency
    bool drawPoints,  // Whether to draw points
    bool drawTriangles,  // Whether to draw Triangles
    bool drawEdges,  // Whether to draw Edges

    MColor &lockVertColor,  // The color to draw verts if they're locked
    MColorArray &jointsColors, // An array of joint colors
    MColorArray &soloCurrentColors, // Per-vertex array of solo colors
    MColorArray &multiCurrentColors, // Per-vertex array of multi-colors

    float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions

    ModifierCommands theCommandIndex,  // The current command index

    MIntArray &mirrorInfluences,  // A mapping between the current influence, and the mirrored one
    MFloatMatrix &inclusiveMatrix,  // The worldspace matrix of the current mesh
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh

    std::vector<MIntArray> &perVertexFaces, // The face indices for each vertex
    std::vector<MIntArray> &perVertexEdges, // The edge indices for each vertex
    std::vector<std::vector<MIntArray>> &perFaceTriangleVertices, // Somehow get the triangle indices per face
    std::vector<std::pair<int, int>> &perEdgeVertices, // The endpoint vertex indices of each edge

    MHWRender::MUIDrawManager &drawManager // Maya's DrawManager
) {
    // This function is the hottest path when painting
    // So it can and should be optimized more
    // I think the endgame for this is to only update the changed vertices each runthrough
    int nbVtx = verticesPainted.size();

    MFloatPointArray points(nbVtx);
    MFloatVectorArray normals(nbVtx);
    MColor theCol(1, 1, 1), white(1, 1, 1, 1), black(0, 0, 0, 1);

    MColorArray pointsColors(nbVtx, theCol);

    MUintArray indices, indicesEdges;  // (nbVtx);
    MColorArray darkEdges;             // (nbVtx, MColor(0.5, 0.5, 0.5));

    MColor newCol, col;

    std::unordered_map<int, unsigned int> verticesMap;
    std::vector<bool> fatFaces_bitset;
    std::vector<bool> fatEdges_bitset;
    std::vector<bool> vertMap_bitset;

    fatFaces_bitset.resize(numFaces);
    fatEdges_bitset.resize(numEdges);
    vertMap_bitset.resize(numVertices);

    MColor baseColor, baseMirrorColor;
    float h, s, v;
    // get baseColor ----------------------------------
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // UnLockVertices

    if (drawTransparency || drawPoints) {
        if (theCommandIndex == ModifierCommands::LockVertices)
            baseColor = lockVertColor;
        else if (theCommandIndex == ModifierCommands::Remove)
            baseColor = black;
        else if (theCommandIndex == ModifierCommands::UnlockVertices)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::Smooth)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::Sharpen)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::LockVertices)
            baseColor = white;
        else if (theCommandIndex == ModifierCommands::UnlockVertices)
            baseColor = white;
        else {
            baseColor = jointsColors[influenceIndex];
            if (paintMirror != 0) {
                baseMirrorColor = jointsColors[mirrorInfluences[influenceIndex]];
                baseMirrorColor.get(MColor::kHSV, h, s, v);
                baseMirrorColor.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15));
            }
        }
        baseColor.get(MColor::kHSV, h, s, v);
        baseColor.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15));
        if ((theCommandIndex != ModifierCommands::Add) && (theCommandIndex != ModifierCommands::AddPercent)) {
            baseMirrorColor = baseColor;
        }
    }

    // pull data out of the dictionary
    // TODO: There's probably a copy-less way to do this
    std::vector<std::pair<int, std::pair<float, float>>> mja;
    mja.reserve( mirroredJoinedArray.size());
    for (const auto &pt : mirroredJoinedArray) {
        mja.push_back(pt);
    }

    MColorArray colors, colorsSolo;
    colors.setLength(mja.size());
    colorsSolo.setLength(mja.size());

    MColorArray *usedColors;
    MColorArray *currentColors;
    if (soloColorVal == 1){
        usedColors = &colorsSolo;
        currentColors = &soloCurrentColors;
    }
    else {
        usedColors = &colors;
        currentColors = &multiCurrentColors;
    }

    bool doTransparency = drawTransparency;
    bool applyGamma = true;
    if (theCommandIndex == ModifierCommands::LockVertices || theCommandIndex == ModifierCommands::UnlockVertices){
        // Don't do transparency when locking/unlocking vertices
        doTransparency = false;
        applyGamma = false;
    }

#pragma omp parallel for
    for (unsigned i = 0; i < mja.size(); ++i){
        const auto &pt = mja[i];
        int ptIndex = pt.first;
        MFloatPoint posPoint(
            mayaRawPoints[ptIndex * 3],
            mayaRawPoints[ptIndex * 3 + 1],
            mayaRawPoints[ptIndex * 3 + 2]
        );
        posPoint = posPoint * inclusiveMatrix;
        points.set(posPoint, i);
        normals.set(verticesNormals[ptIndex], i);
    }

    if (drawTriangles) {
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            float weightBase = pt.second.first;
            float weightMirror = pt.second.second;
            MColor multColor, soloColor;
            // TODO: Extract
            //getColorWithMirror(ptIndex, weightBase, weightMirror, colors, colorsSolo, multColor, soloColor);
            colors.set(multColor, i);
            colorsSolo.set(soloColor, i);
        }

        if (applyGamma){
#pragma omp parallel for
            for (unsigned i = 0; i < mja.size(); ++i){
                const auto &pt = mja[i];
                float weightBase = pt.second.first;
                float weightMirror = pt.second.second;
                float transparency = (doTransparency) ? weightBase + weightMirror: 1.0;
                MColor& colRef = (*usedColors)[i];
                colRef.get(MColor::kHSV, h, s, v);
                colRef.set(MColor::kHSV, h, pow(s, 0.8), pow(v, 0.15), transparency);
            }
        }
    }

    if (drawPoints) {
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            float weight = pt.second.first + pt.second.second;
            pointsColors[i] = weight * baseColor + (1.0 - weight) * (*currentColors)[pt.first];
        }
    }

    if (drawEdges) {
        darkEdges.setLength(mja.size());
#pragma omp parallel for
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            float transparency = (doTransparency) ? pt.second.first + pt.second.second: 1.0;
            darkEdges.set(i, 0.5f, 0.5f, 0.5f, transparency);
        }
    }

    if (drawTriangles || drawEdges) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            verticesMap[pt.first] = i;
            vertMap_bitset[pt.first] = true;
        }
    }

    if (drawTriangles) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            for (int f : perVertexFaces[ptIndex]){
                fatFaces_bitset[f] = true;
            }
        }
    }

    if (drawEdges) {
        for (unsigned i = 0; i < mja.size(); ++i){
            const auto &pt = mja[i];
            int ptIndex = pt.first;
            for (int e : perVertexEdges[ptIndex]){
                fatEdges_bitset[e] = true;
            }
        }
    }

    if (drawTriangles) {
        // bitset is faster than an unordered_set in this case
        // may be worth keeping the bitsets around on the brush
        // so we don't have to constantly allocate memory
        for (unsigned f = 0; f<fatFaces_bitset.size(); ++f){
            if (!fatFaces_bitset[f]) continue;
            for (auto &tri : perFaceTriangleVertices[f]) {
                if (!vertMap_bitset[tri[0]]) continue;
                if (!vertMap_bitset[tri[1]]) continue;
                if (!vertMap_bitset[tri[2]]) continue;
                auto it0 = verticesMap.find(tri[0]);
                auto it1 = verticesMap.find(tri[1]);
                auto it2 = verticesMap.find(tri[2]);
                indices.append(it0->second);
                indices.append(it1->second);
                indices.append(it2->second);
            }
        }

        auto style = MHWRender::MUIDrawManager::kFlat;
        drawManager.setPaintStyle(style);  // kFlat // kShaded // kStippled
        drawManager.mesh(MHWRender::MUIDrawManager::kTriangles, points, &normals, usedColors, &indices);
    }

    if (drawEdges) {
        // bitset is faster than an unordered_set in this case
        // may be worth keeping the bitsets around on the brush
        // so we don't have to constantly allocate memory
        for (unsigned e = 0; e<fatEdges_bitset.size(); ++e){
            if (!fatEdges_bitset[e]) continue;
            auto &pairEdges = perEdgeVertices[e];

            if (!vertMap_bitset[pairEdges.first]) continue;
            if (!vertMap_bitset[pairEdges.second]) continue;
            auto it0 = verticesMap.find(pairEdges.first);
            auto it1 = verticesMap.find(pairEdges.second);
            indicesEdges.append(it0->second);
            indicesEdges.append(it1->second);

        }

        drawManager.setDepthPriority(2);
        drawManager.mesh(MHWRender::MUIDrawManager::kLines, points, &normals, &darkEdges, &indicesEdges);
    }

    if (drawPoints) {
        drawManager.setPointSize(4);
        drawManager.mesh(MHWRender::MUIDrawManager::kPoints, points, NULL, &pointsColors);
    }
    return MStatus::kSuccess;
}

MStatus SkinBrushContext::doRelease(MEvent &event, MHWRender::MUIDrawManager &drawMgr,
                                    const MHWRender::MFrameContext &context) {
    return doReleaseCommon(event);
}

MStatus SkinBrushContext::refreshPointsNormals() {
    MStatus status = MStatus::kSuccess;

    if (!skinObj.isNull() && meshDag.isValid(&status)) {
        this->meshFn.freeCachedIntersectionAccelerator();  // yes ?
        this->mayaRawPoints = this->meshFn.getRawPoints(&status);
        this->rawNormals = this->meshFn.getRawNormals(&status);
        int rawNormalsLength = sizeof(this->rawNormals);

#pragma omp parallel for
        for (int vertexInd = 0; vertexInd < this->numVertices; vertexInd++) {
            int indNormal = this->verticesNormalsIndices[vertexInd];
            int rawIndNormal = indNormal * 3 + 2;
            if (rawIndNormal < rawNormalsLength) {
                MVector theNormal(
                    this->rawNormals[indNormal * 3],
                    this->rawNormals[indNormal * 3 + 1],
                    this->rawNormals[indNormal * 3 + 2]
                );
                this->verticesNormals.set(theNormal, vertexInd);
            }
        }
    }
    return status;
}

// ---------------------------------------------------------------------
// common methods for legacy viewport and viewport 2.0
// ---------------------------------------------------------------------
MStatus SkinBrushContext::doPressCommon(MEvent &event) {
    MStatus status = MStatus::kSuccess;

    if (meshDag.node().isNull()) return MStatus::kNotFound;

    view = M3dView::active3dView();

    if (this->pickMaxInfluenceVal || this->pickInfluenceVal) {
        this->BBoxOfDeformers.clear();

        if (this->pickMaxInfluenceVal && biggestInfluence != -1) {
            MUserEventMessage::postUserEvent("brSkinBrush_influencesReordered");
        }

        if (biggestInfluence != this->influenceIndex && biggestInfluence != -1) {
            setInfluenceIndex(biggestInfluence, true);  // true for select in UI
        }

        return MStatus::kNotFound;
    }

    // store for undo purposes --------------------------------------------------------------
    // only if painting not after
    if (!this->postSetting || paintMirror != 0) {
        this->fullUndoSkinWeightList = MDoubleArray(this->skinWeightList);
    }
    // update values ------------------------------------------------------------------------
    refreshPointsNormals();

    // first reset attribute to paint values off if we're doing that ------------------------
    paintArrayValues.copy(MDoubleArray(numVertices, 0.0));
    this->skinValuesToSet.clear();
    this->skinValuesMirrorToSet.clear();
    this->verticesPainted.clear();

    // reset values ---------------------------------
    this->intensityValuesOrig = std::vector<float>(this->numVertices, 0);
    this->intensityValuesMirror = std::vector<float>(this->numVertices, 0);
    // initialize --
    undersamplingSteps = 0;
    performBrush = false;

    event.getPosition(this->screenX, this->screenY);

    // Get the size of the viewport and calculate the center for placing
    // the value messages when adjusting the brush settings.
    unsigned int x;
    unsigned int y;
    view.viewport(x, y, width, height);
    viewCenterX = (short)width / 2;
    viewCenterY = (short)height / 2;

    // Store the initial mouse position. These get used when adjusting
    // the brush size and strength values.
    startScreenX = this->screenX;
    startScreenY = this->screenY;
    storedDistance = 0.0;  // for the drag screen middle click

    // Reset the adjustment from the previous drag.
    initAdjust = false;
    sizeAdjust = true;
    adjustValue = 0.0;

    // -----------------------------------------------------------------
    // closest point on surface
    // -----------------------------------------------------------------
    // Getting the closest index cannot be performed when in flood mode.

    MStatus mbStat;
    if (event.mouseButton(&mbStat)) {
        // init at false
        successFullDragHit = false;
        successFullDragMirrorHit = false;
        this->dicVertsDistSTART.clear();
        this->mirroredJoinedArray.clear();
        successFullHit =
            computeHit(screenX, screenY, false, this->previousfaceHit, this->centerOfBrush);
        if (!successFullHit) {
            return MStatus::kNotFound;
        }
        this->AllHitPoints.clear();
        this->AllHitPointsMirror.clear();

        // we put it inside our world matrix
        this->inMatrixHit = this->centerOfBrush * this->inclusiveMatrixInverse;
        successFullHit =
            expandHit(this->previousfaceHit, this->inMatrixHit, this->dicVertsDistSTART);

        // mirror part -------------------
        if (paintMirror != 0) {  // if mirror is not OFf
            this->dicVertsMirrorDistSTART.clear();
            int faceMirrorHit;
            successFullMirrorHit = getMirrorHit(faceMirrorHit, this->centerOfMirrorBrush);
            meshFn.getPolygonNormal(faceMirrorHit, this->normalMirroredVector, MSpace::kWorld);

            this->inMatrixHitMirror = this->centerOfMirrorBrush * this->inclusiveMatrixInverse;
            if (successFullMirrorHit) {
                expandHit(faceMirrorHit, this->inMatrixHitMirror, this->dicVertsMirrorDistSTART);
            }
        }
        // Store the initial surface point and view vector to use when
        // the brush settings are adjusted because the brush circle
        // needs to be static during the adjustment.
        surfacePointAdjust = this->centerOfBrush;
        worldVectorAdjust = this->worldVector;
    }
    return status;
}


std::vector<int> getSurroundingVerticesPerVert(
    int vertexIndex,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX
) {
    auto first = perVertexVerticesSetFLAT.begin() + perVertexVerticesSetINDEX[vertexIndex];
    auto last = perVertexVerticesSetFLAT.begin() + perVertexVerticesSetINDEX[vertexIndex + (int)1];
    std::vector<int> newVec(first, last);
    return newVec;
};



void growArrayOfHitsFromCenters(
    bool coverageVal,
    double sizeVal,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,
    const float * mayaRawPoints,
    const MVectorArray &verticesNormals,
    const MVector &worldVector,
    const MFloatPointArray &AllHitPoints,

    std::unordered_map<int, float> &dicVertsDist // Return value
) {
    // set of visited vertices
    std::vector<int> vertsVisited, vertsWithinDistance;

    for (const auto& element : dicVertsDist) {
        vertsVisited.push_back(element.first);
    }
    std::sort(vertsVisited.begin(), vertsVisited.end());
    vertsWithinDistance = vertsVisited;

    // start of growth---------------------
    bool processing = true;

    std::vector<int> borderOfGrowth;
    borderOfGrowth = vertsVisited;

    if (AllHitPoints.length() == 0) return;  // if not it will crash

    // make the std vector points for faster sorting -----------
    std::vector<point_t> points;
    for (auto hitPt : AllHitPoints) {
        points.push_back(std::make_tuple(hitPt.x, hitPt.y, hitPt.z));
    }
    while (processing) {
        processing = false;
        // -------------------- grow the vertices ----------------------------------------------
        std::vector<int> setOfVertsGrow;
        for (int vertexIndex : borderOfGrowth) {
            setOfVertsGrow = setOfVertsGrow + getSurroundingVerticesPerVert(
                vertexIndex, perVertexVerticesSetFLAT, perVertexVerticesSetINDEX
            );
        }
        // get the vertices that are grown -----------------------------------------------------
        std::vector<int> verticesontheborder = setOfVertsGrow - vertsVisited;
        std::vector<int> foundGrowVertsWithinDistance;

        // for all vertices grown ------------------------------
        for (int vertexBorder : verticesontheborder) {
            // First check the normal
            if (!coverageVal) {
                MVector vertexBorderNormal = verticesNormals[vertexBorder];
                double multVal = worldVector * vertexBorderNormal;
                if (multVal > 0.0) continue;
            }
            float closestDist = -1;
            // find the closestDistance and closest Vertex from visited vertices
            // ------------------------
            point_t thisPoint = std::make_tuple(mayaRawPoints[vertexBorder * 3],
                                                mayaRawPoints[vertexBorder * 3 + 1],
                                                mayaRawPoints[vertexBorder * 3 + 2]);
            auto glambda = [&thisPoint](const point_t &a, const point_t &b) {
                float aRes = distance_sq(a, thisPoint);
                float bRes = distance_sq(b, thisPoint);
                return aRes < bRes;
            };
            std::partial_sort(points.begin(), points.begin() + 1, points.end(), glambda);
            auto closestPoint = points.front();
            closestDist = distance(closestPoint, thisPoint);
            // get the new distance between the closest visited vertex and the grow vertex
            if (closestDist <= sizeVal) {  // if in radius of the brush
                // we found a vertex in the radius
                // now add to the visited and add the distance to the dictionnary
                processing = true;
                foundGrowVertsWithinDistance.push_back(vertexBorder);
                auto ret = dicVertsDist.insert(std::make_pair(vertexBorder, closestDist));
                if (!ret.second) ret.first->second = std::min(closestDist, ret.first->second);
            }
        }
        // this vertices has been visited, let's not consider them anymore
        std::sort(foundGrowVertsWithinDistance.begin(), foundGrowVertsWithinDistance.end());
        vertsVisited = vertsVisited + verticesontheborder;
        vertsWithinDistance = vertsWithinDistance + foundGrowVertsWithinDistance;

        borderOfGrowth = foundGrowVertsWithinDistance;
    }
}






void SkinBrushContext::growArrayOfHitsFromCenters(std::unordered_map<int, float> &dicVertsDist,
                                                  MFloatPointArray &AllHitPoints) {
    // set of visited vertices
    std::vector<int> vertsVisited, vertsWithinDistance;

    for (const auto &element : dicVertsDist) vertsVisited.push_back(element.first);
    std::sort(vertsVisited.begin(), vertsVisited.end());
    vertsWithinDistance = vertsVisited;

    // start of growth---------------------
    bool processing = true;

    std::vector<int> borderOfGrowth;
    borderOfGrowth = vertsVisited;

    // make the std vector points for faster sorting -----------
    std::vector<point_t> points;
    for (auto hitPt : AllHitPoints) points.push_back(std::make_tuple(hitPt.x, hitPt.y, hitPt.z));
    if (AllHitPoints.length() == 0) return;  // if not it will crash
    while (processing) {
        processing = false;
        // -------------------- grow the vertices ----------------------------------------------
        std::vector<int> setOfVertsGrow;
        for (int vertexIndex : borderOfGrowth) {
            setOfVertsGrow = setOfVertsGrow + getSurroundingVerticesPerVert(vertexIndex);
        }
        // get the vertices that are grown -----------------------------------------------------
        std::vector<int> verticesontheborder = setOfVertsGrow - vertsVisited;
        std::vector<int> foundGrowVertsWithinDistance;

        // for all vertices grown ------------------------------
        for (int vertexBorder : verticesontheborder) {
            // First check the normal
            if (!this->coverageVal) {
                MVector vertexBorderNormal = this->verticesNormals[vertexBorder];
                double multVal = worldVector * vertexBorderNormal;
                if (multVal > 0.0) continue;
            }
            float closestDist = -1;
            // find the closestDistance and closest Vertex from visited vertices
            // ------------------------
            point_t thisPoint = std::make_tuple(this->mayaRawPoints[vertexBorder * 3],
                                                this->mayaRawPoints[vertexBorder * 3 + 1],
                                                this->mayaRawPoints[vertexBorder * 3 + 2]);
            auto glambda = [&thisPoint](const point_t &a, const point_t &b) {
                float aRes = distance_sq(a, thisPoint);
                float bRes = distance_sq(b, thisPoint);
                return aRes < bRes;
            };
            std::partial_sort(points.begin(), points.begin() + 1, points.end(), glambda);
            auto closestPoint = points.front();
            closestDist = distance(closestPoint, thisPoint);
            // get the new distance between the closest visited vertex and the grow vertex
            if (closestDist <= this->sizeVal) {  // if in radius of the brush
                // we found a vertex in the radius
                // now add to the visited and add the distance to the dictionnary
                processing = true;
                foundGrowVertsWithinDistance.push_back(vertexBorder);
                auto ret = dicVertsDist.insert(std::make_pair(vertexBorder, closestDist));
                if (!ret.second) ret.first->second = std::min(closestDist, ret.first->second);
            }
        }
        // this vertices has been visited, let's not consider them anymore
        std::sort(foundGrowVertsWithinDistance.begin(), foundGrowVertsWithinDistance.end());
        vertsVisited = vertsVisited + verticesontheborder;
        vertsWithinDistance = vertsWithinDistance + foundGrowVertsWithinDistance;

        borderOfGrowth = foundGrowVertsWithinDistance;
    }
}

MStatus SkinBrushContext::doDragCommon(MEvent &event) {
    MStatus status = MStatus::kSuccess;

    // -----------------------------------------------------------------
    // Dragging with the left mouse button performs the painting.
    // -----------------------------------------------------------------
    if (event.mouseButton() == MEvent::kLeftMouse) {
        // from previous hit get a line----------
        short previousX = this->screenX;
        short previousY = this->screenY;
        event.getPosition(this->screenX, this->screenY);

        // dictionnary of visited vertices and distances --- prefill it with the previous hit ---
        std::unordered_map<int, float> dicVertsDistToGrow = this->dicVertsDistSTART;
        std::unordered_map<int, float> dicVertsDistToGrowMirror = this->dicVertsMirrorDistSTART;

        // for linear growth ----------------------------------
        MFloatPointArray lineHitPoints, lineHitPointsMirror;
        lineHitPoints.append(this->inMatrixHit);
        if (paintMirror != 0 && successFullMirrorHit) {  // if mirror is not OFf
            lineHitPointsMirror.append(this->inMatrixHitMirror);
        }
        // --------- LINE OF PIXELS --------------------
        std::vector<std::pair<short, short>> line2dOfPixels;
        // get pixels of the line of pixels
        lineC(previousX, previousY, this->screenX, this->screenY, line2dOfPixels);
        int nbPixelsOfLine = (int)line2dOfPixels.size();

        MFloatPoint hitPoint, hitMirrorPoint;
        MFloatPoint hitPointIM, hitMirrorPointIM;
        int faceHit, faceMirrorHit;

        bool successFullHit2 =
            computeHit(this->screenX, this->screenY, this->drawBrushVal, faceHit, hitPoint);
        bool successFullMirrorHit2 = false;
        if (successFullHit2) {
            // stored in start dic for next call of drag function
            this->previousfaceHit = faceHit;
            this->dicVertsDistSTART.clear();
            hitPointIM = hitPoint * this->inclusiveMatrixInverse;
            expandHit(faceHit, hitPointIM, this->dicVertsDistSTART);  // for next beginning

            // If the mirror happens -------------------------
            if (paintMirror != 0) {  // if mirror is not OFf
                successFullMirrorHit2 = getMirrorHit(faceMirrorHit, hitMirrorPoint);
                if (successFullMirrorHit2) {
                    hitMirrorPointIM = hitMirrorPoint * this->inclusiveMatrixInverse;
                    expandHit(faceMirrorHit, hitMirrorPointIM, this->dicVertsMirrorDistSTART);
                }
            }
        }
        if (!this->successFullDragHit && !successFullHit2)  // moving in empty zone
            return MStatus::kNotFound;
        //////////////////////////////////////////////////////////////////////////////
        this->successFullDragHit = successFullHit2;
        this->successFullDragMirrorHit = successFullMirrorHit2;

        if (this->successFullDragHit) {
            this->centerOfBrush = hitPoint;
            this->inMatrixHit = hitPointIM;
            if (paintMirror != 0 && this->successFullDragMirrorHit) {
                this->centerOfMirrorBrush = hitMirrorPoint;
                this->inMatrixHitMirror = hitMirrorPointIM;
            }
        }
        int incrementValue = 1;
        if (incrementValue < nbPixelsOfLine) {
            for (int i = incrementValue; i < nbPixelsOfLine; i += incrementValue) {
                auto myPair = line2dOfPixels[i];
                short x = myPair.first;
                short y = myPair.second;

                bool successFullHit2 = computeHit(x, y, false, faceHit, hitPoint);
                if (successFullHit2) {
                    hitPointIM = hitPoint * this->inclusiveMatrixInverse;
                    lineHitPoints.append(hitPointIM);
                    successFullHit2 = expandHit(faceHit, hitPointIM, dicVertsDistToGrow);
                    // mirror part -------------------
                    if (paintMirror != 0) {  // if mirror is not OFf
                        successFullMirrorHit2 = getMirrorHit(faceMirrorHit, hitMirrorPoint);
                        if (successFullMirrorHit2) {
                            hitMirrorPointIM = hitMirrorPoint * this->inclusiveMatrixInverse;
                            lineHitPointsMirror.append(hitMirrorPointIM);
                            expandHit(faceMirrorHit, hitMirrorPointIM, dicVertsDistToGrowMirror);
                        }
                    }
                }
            }
        }
        // only now add last hit -------------------------
        if (this->successFullDragHit) {
            lineHitPoints.append(this->inMatrixHit);
            expandHit(faceHit, this->inMatrixHit, dicVertsDistToGrow);  // to get closest hit
            if (paintMirror != 0 && this->successFullDragMirrorHit) {   // if mirror is not OFf
                lineHitPointsMirror.append(this->inMatrixHitMirror);
                expandHit(faceMirrorHit, this->inMatrixHitMirror, dicVertsDistToGrowMirror);
            }
        }

        this->modifierNoneShiftControl = ModifierKeys::NoModifier;
        if (event.isModifierShift()) {
            if (event.isModifierControl()) {
                this->modifierNoneShiftControl = ModifierKeys::ControlShift;
            } else {
                this->modifierNoneShiftControl = ModifierKeys::Shift;
            }
        }
        else if (event.isModifierControl()) {
            this->modifierNoneShiftControl = ModifierKeys::Control;
        }

        // let's expand these arrays to the outer part of the brush----------------
        for (auto hitPoint : lineHitPoints) this->AllHitPoints.append(hitPoint);
        for (auto hitPoint : lineHitPointsMirror) this->AllHitPointsMirror.append(hitPoint);

        growArrayOfHitsFromCenters(dicVertsDistToGrow, lineHitPoints);
        addBrushShapeFallof(dicVertsDistToGrow);
        preparePaint(dicVertsDistToGrow, this->previousPaint, this->intensityValuesOrig,
                     this->skinValuesToSet, this->verticesPainted, false);

        if (paintMirror != 0) {  // mirror
            growArrayOfHitsFromCenters(dicVertsDistToGrowMirror, lineHitPointsMirror);
            addBrushShapeFallof(dicVertsDistToGrowMirror);
            preparePaint(dicVertsDistToGrowMirror, this->previousMirrorPaint,
                         this->intensityValuesMirror, this->skinValuesMirrorToSet, this->verticesPainted, true);
        }
        mergeMirrorArray(this->skinValuesToSet, this->skinValuesMirrorToSet);
        if (this->useColorSetsWhilePainting || !this->postSetting) {
            doPerformPaint();
        }
        performBrush = true;
    }
    // -----------------------------------------------------------------
    // Dragging with the middle mouse button adjusts the settings.
    // -----------------------------------------------------------------
    else if (event.mouseButton() == MEvent::kMiddleMouse) {
        // Skip several evaluation steps. This has several reasons:
        // - It reduces the smoothing strength because not every evaluation
        //   triggers a calculation.
        // - It lets adjusting the brush appear smoother because the lines
        //   show less flicker.
        // - It also improves the differentiation between horizontal and
        //   vertical dragging when adjusting.
        undersamplingSteps++;
        if (undersamplingSteps < undersamplingVal) return status;
        undersamplingSteps = 0;

        // get screen position
        event.getPosition(this->screenX, this->screenY);
        // Get the current and initial cursor position and calculate the
        // delta movement from them.
        MPoint currentPos(this->screenX, this->screenY);
        MPoint startPos(startScreenX, startScreenY);
        MVector deltaPos(currentPos - startPos);

        // Switch if the size should get adjusted or the strength based
        // on the drag direction. A drag along the x axis defines size
        // and a drag along the y axis defines strength.
        // InitAdjust makes sure that direction gets set on the first
        // drag event and gets reset the next time a mouse button is
        // pressed.
        if (!initAdjust) {
            if (deltaPos.length() < 6)
                return status;  // only if we move at least 6 pixels do we know the direction to
                                // pick !
            sizeAdjust = (abs(deltaPos.x) > abs(deltaPos.y));
            initAdjust = true;
        }
        // Define the settings for either setting the brush size or the
        // brush strength.
        MString message = "Brush Size";
        MString slider = "Size";
        double dragDistance = deltaPos.x;
        double min = 0.001;
        unsigned int max = 1000;
        double baseValue = sizeVal;
        // The adjustment speed depends on the distance to the mesh.
        // Closer distances allows for a feiner control whereas larger
        // distances need a coarser control.
        double speed = pow(0.001 * pressDistance, 0.9);

        // Vary the settings if the strength gets adjusted.
        if (!sizeAdjust) {
            if (event.isModifierControl()) {
                message = "Smooth Strength";
                baseValue = smoothStrengthVal;
            } else {
                message = "Brush Strength";
                baseValue = strengthVal;
            }
            slider = "Strength";
            dragDistance = deltaPos.y;
            max = 1;
            speed *= 0.1;  // smaller for the upd and down
        }
        double prevDist = 0.0;
        // The shift modifier scales the speed for a fine adjustment.
        if (event.isModifierShift()) {
            if (!shiftMiddleDrag) {             // if we weren't in shift we reset
                storedDistance = dragDistance;  // store the pixels to remove
                shiftMiddleDrag = true;
            }
            prevDist = storedDistance * speed;  // store the previsou drag done
            speed *= 0.1;
        } else {
            if (shiftMiddleDrag) {
                storedDistance = dragDistance;
                shiftMiddleDrag = false;
            }
            prevDist = storedDistance * speed;  // store the previous drag done
        }
        dragDistance -= storedDistance;

        // Calculate the new value by adding the drag distance to the
        // start value.
        double value = baseValue + prevDist + dragDistance * speed;

        // Clamp the values to the min/max range.
        if (value < min)
            value = min;
        else if (value > max)
            value = max;

        // Store the modified value for drawing and for setting the
        // values when releasing the mouse button.
        adjustValue = value;

        // -------------------------------------------------------------
        // value display in the viewport
        // -------------------------------------------------------------
        short offsetX = startScreenX - viewCenterX;
        short offsetY = startScreenY - viewCenterY - 50;

        int precision = 2;
        if (event.isModifierShift()) precision = 3;

        std::string stdMessage = std::string(message.asChar());

        std::stringstream stream;
        stream << std::fixed << std::setprecision(precision) << adjustValue;

        std::string theMessage = stdMessage + ": " + stream.str();
        std::string headsUpFmt = "headsUpMessage -horizontalOffset "+ std::to_string(offsetX) +" -verticalOffset "+ std::to_string(offsetY) +" -time 0.1 \""+ theMessage +"\"";
        MGlobal::executeCommand(MString(headsUpFmt.c_str(), headsUpFmt.length()));

        // Also, adjust the slider in the tool settings window if it's
        // currently open.
        if (sizeAdjust){
            MUserEventMessage::postUserEvent("brSkinBrush_updateDisplaySize");
        }
        else {
            MUserEventMessage::postUserEvent("brSkinBrush_updateDisplayStrength");
        }

    }
    return status;
}

MStatus SkinBrushContext::doReleaseCommon(MEvent &event) {
    // Don't continue if no mesh has been set.
    if (meshFn.object().isNull()) return MS::kFailure;
    if (this->pickMaxInfluenceVal || this->pickInfluenceVal) {
        this->pickMaxInfluenceVal = false;
        this->pickInfluenceVal = false;
    }
    this->refreshDone = false;
    // Define, which brush setting has been adjusted and needs to get
    // stored.
    if (event.mouseButton() == MEvent::kMiddleMouse && initAdjust) {
        CHECK_MSTATUS_AND_RETURN_SILENT(pressStatus);
        if (sizeAdjust) {
            sizeVal = adjustValue;
        } else {
            if (event.isModifierControl()) {
                smoothStrengthVal = adjustValue;
            } else {
                strengthVal = adjustValue;
            }
        }
    }
    if (performBrush) {
        doTheAction();
    }
    return MS::kSuccess;
}

void SkinBrushContext::doTheAction() {
    // If the smoothing has been performed send the current values to
    // the tool command along with the necessary data for undo and redo.
    // The same goes for the select mode.
    MColorArray multiEditColors, soloEditColors;
    int nbVerticesPainted = (int)this->verticesPainted.size();
    MIntArray editVertsIndices(nbVerticesPainted, 0);
    MIntArray undoLocks, redoLocks;

    MStatus status;
    if (this->lockJoints.length() < this->nbJoints) {
        getListLockJoints(skinObj, this->nbJoints, indicesForInfluenceObjects, this->lockJoints);
        if (this->lockJoints.length() < this->nbJoints) {
            this->lockJoints = MIntArray(this->nbJoints, 0);
        }
    }
    MDoubleArray prevWeights((int)this->verticesPainted.size() * this->nbJoints, 0);

    std::vector<int> intArray;
    intArray.resize(this->verticesPainted.size());

    int i = 0;
    for (const auto &theVert : this->verticesPainted) {
        editVertsIndices[i] = theVert;
        i++;
    }

    ModifierCommands theCommandIndex = getCommandIndexModifiers();
    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        undoLocks.copy(this->lockVertices);
        bool addLocks = theCommandIndex == ModifierCommands::LockVertices;
        editLocks(this->skinObj, editVertsIndices, addLocks, this->lockVertices);
        redoLocks.copy(this->lockVertices);
    } else {
        if (this->paintMirror != 0) {
            int mirrorInfluenceIndex = this->mirrorInfluences[this->influenceIndex];
            mergeMirrorArray(this->skinValuesToSet, this->skinValuesMirrorToSet);

            if (mirrorInfluenceIndex != this->influenceIndex)
                status = applyCommandMirror();
            else {  // we merge in one array, it's easier
                for (const auto &element : this->skinValuesMirrorToSet) {
                    int index = element.first;
                    float value = element.second;

                    auto ret = this->skinValuesToSet.insert(std::make_pair(index, value));
                    if (!ret.second) ret.first->second = std::max(value, ret.first->second);
                }
                status = applyCommand(this->influenceIndex, this->skinValuesToSet);  //
            }
        } else if (this->skinValuesToSet.size() > 0) {
            status = applyCommand(this->influenceIndex, this->skinValuesToSet);  //
            if (status == MStatus::kFailure) {
                MGlobal::displayError(
                    MString("Something went wrong. EXIT the brush and RESTART it"));
                return;
            }
        }
        if (!this->postSetting) {  // only store if not constant setting
            int i = 0;
            for (const auto &theVert : this->verticesPainted) {
                for (int j = 0; j < this->nbJoints; ++j) {
                    prevWeights[i * this->nbJoints + j] =
                        this->fullUndoSkinWeightList[theVert * this->nbJoints + j];
                }
                i++;
            }
        }
    }
    refreshColors(editVertsIndices, multiEditColors, soloEditColors);
    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet);

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet2);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet2);
    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        // without that it doesn't refresh because mesh is not invalidated, meaning the skinCluster
        // hasn't changed
        meshFn.updateSurface();
    }
    this->skinValuesToSet.clear();
    this->skinValuesMirrorToSet.clear();
    this->previousPaint.clear();
    this->previousMirrorPaint.clear();

    if (!this->firstPaintDone) {
        this->firstPaintDone = true;
        MUserEventMessage::postUserEvent("brSkinBrush_cleanCloseUndo");
    }

    cmd = (skinBrushTool *)newToolCommand();
    cmd->setColor(colorVal);
    cmd->setCurve(curveVal);
    cmd->setDrawBrush(drawBrushVal);
    cmd->setDrawRange(drawRangeVal);
    cmd->setPythonImportPath(moduleImportString);
    cmd->setEnterToolCommand(enterToolCommandVal);
    cmd->setExitToolCommand(exitToolCommandVal);
    cmd->setFractionOversampling(fractionOversamplingVal);
    cmd->setIgnoreLock(ignoreLockVal);
    cmd->setLineWidth(lineWidthVal);
    cmd->setOversampling(oversamplingVal);
    cmd->setRange(rangeVal);
    cmd->setSize(sizeVal);
    cmd->setStrength(strengthVal);
    // storing options for the finalize optionVar
    cmd->setMinColor(minSoloColor);
    cmd->setMaxColor(maxSoloColor);
    cmd->setSoloColor(soloColorVal);
    cmd->setSoloColorType(soloColorTypeVal);

    cmd->setPaintMirror(paintMirror);
    cmd->setUseColorSetsWhilePainting(useColorSetsWhilePainting);
    cmd->setDrawTriangles(drawTriangles);
    cmd->setDrawEdges(drawEdges);
    cmd->setDrawPoints(drawPoints);
    cmd->setDrawTransparency(drawTransparency);
    cmd->setPostSetting(postSetting);
    cmd->setCoverage(coverageVal);
    cmd->setMessage(messageVal);
    cmd->setSmoothRepeat(smoothRepeat);

    cmd->setSmoothStrength(smoothStrengthVal);
    cmd->setUndersampling(undersamplingVal);
    cmd->setVolume(volumeVal);
    cmd->setCommandIndex(theCommandIndex);

    cmd->setUndoLocks(undoLocks);
    cmd->setRedoLocks(redoLocks);

    MFnDependencyNode skinDep(this->skinObj);
    MString skinName = skinDep.name();
    cmd->setMesh(meshDag);

    if (isNurbs) {
        cmd->setNurbs(nurbsDag);
        cmd->setnumCVInV(numCVsInV_);
    }
    cmd->setSkinCluster(skinObj);
    cmd->setIsNurbs(isNurbs);

    cmd->setInfluenceIndices(influenceIndices);
    MString iname = getInfluenceName();
    cmd->setInfluenceName(iname);

    cmd->setUndoVertices(editVertsIndices);
    if (!this->postSetting) {
        cmd->setWeights(prevWeights);
    } else {
        cmd->setWeights(this->skinWeightsForUndo);
    }
    cmd->setNormalize(normalize);
    cmd->setContextPointer(this);

    // Regular context implementations usually call
    // (MPxToolCommand)::redoIt at this point but in this case it
    // is not necessary since the the smoothing already has been
    // performed. There is no need to apply the values twice.
    cmd->finalize();
    maya2019RefreshColors();
    MUserEventMessage::postUserEvent("brSkinBrush_afterPaint");
}

ModifierCommands SkinBrushContext::getCommandIndexModifiers() const {
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // unlockVertices
    ModifierCommands theCommandIndex = this->commandIndex;

    if (this->commandIndex == ModifierCommands::Add){
        if (this->modifierNoneShiftControl == this->smoothModifier){
            theCommandIndex = ModifierCommands::Smooth;
        }
        else if (this->modifierNoneShiftControl == this->removeModifier){
            theCommandIndex = ModifierCommands::Remove;
        }
    }
    else if (this->commandIndex == ModifierCommands::LockVertices){
        if (this->modifierNoneShiftControl == ModifierKeys::Shift){
            theCommandIndex = ModifierCommands::UnlockVertices;
        }
    }
    if (this->modifierNoneShiftControl == ModifierKeys::ControlShift){
        theCommandIndex = ModifierCommands::Sharpen;
    }

    return theCommandIndex;
}
void SkinBrushContext::mergeMirrorArray(std::unordered_map<int, float> &valuesBase,
                                        std::unordered_map<int, float> &valuesMirrored) {
    mirroredJoinedArray.clear();
    for (const auto &elem : valuesBase) {
        int theVert = elem.first;
        float theWeight = elem.second;
        std::pair<float, float> secondElem(theWeight, 0.0);
        std::pair<int, std::pair<float, float>> toAdd(theVert, secondElem);
        mirroredJoinedArray.insert(toAdd);
    }

    for (const auto &elem : valuesMirrored) {
        int theVert = elem.first;
        float theWeight = elem.second;
        std::pair<float, float> secondElem(0.0, theWeight);
        std::pair<int, std::pair<float, float>> toAdd(theVert, secondElem);
        auto ret = mirroredJoinedArray.insert(toAdd);
        if (!ret.second) {
            std::pair<float, float> origSecondElem = ret.first->second;
            origSecondElem.second = theWeight;
            ret.first->second = origSecondElem;
        }
    }
}
MStatus SkinBrushContext::applyCommandMirror() {
    MStatus status;
    MGlobal::displayInfo(MString("applyCommandMirror "));
    std::map<int, std::pair<float, float>> mirroredJoinedArrayOrdered(mirroredJoinedArray.begin(),
                                                                      mirroredJoinedArray.end());

    ModifierCommands theCommandIndex = getCommandIndexModifiers();
    double multiplier = 1.0;

    int influence = this->influenceIndex;
    int influenceMirror = this->mirrorInfluences[this->influenceIndex];

    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices))
        return MStatus::kSuccess;

    MDoubleArray theWeights((int)this->nbJoints * mirroredJoinedArrayOrdered.size(), 0.0);
    int repeatLimit = 1;
    if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen) {
        repeatLimit = this->smoothRepeat;
    }

    MIntArray objVertices;
    for (int repeat = 0; repeat < repeatLimit; ++repeat) {
        if (theCommandIndex == ModifierCommands::Smooth) {
            int indexCurrVert = 0;
            for (const auto &elem : mirroredJoinedArrayOrdered) {
                int theVert = elem.first;
                if (repeat == 0) objVertices.append(theVert);
                float valueBase = elem.second.first;
                float valueMirror = elem.second.second;
                float biggestValue = std::max(valueBase, valueMirror);

                double theWeight = (double)biggestValue;
                std::vector<int> vertsAround = getSurroundingVerticesPerVert(theVert);
                status = setAverageWeight(vertsAround, theVert, indexCurrVert, this->nbJoints,
                                          this->lockJoints, this->skinWeightList, theWeights,
                                          this->smoothStrengthVal * theWeight);
                indexCurrVert++;
            }
        } else {
            if (this->ignoreLockVal) {
                status = editArrayMirror(theCommandIndex, influence, influenceMirror,
                                         this->nbJoints, this->ignoreLockJoints,
                                         this->skinWeightList, mirroredJoinedArrayOrdered,
                                         theWeights, this->doNormalize, multiplier);
            } else {
                if (this->lockJoints[influence] == 1 && theCommandIndex != ModifierCommands::Sharpen) {
                    return status;  //  if locked and it's not sharpen --> do nothing
                }
                status = editArrayMirror(theCommandIndex, influence, influenceMirror,
                                         this->nbJoints, this->lockJoints, this->skinWeightList,
                                         mirroredJoinedArrayOrdered, theWeights, this->doNormalize,
                                         multiplier);
            }
        }
        if (status == MStatus::kFailure) {
            return status;
        }
        // here we should normalize -----------------------------------------------------
        int i = 0;
        for (const auto &elem : mirroredJoinedArrayOrdered) {
            int theVert = elem.first;
            if (repeat == 0) objVertices.append(theVert);

            for (int j = 0; j < this->nbJoints; ++j) {
                int ind_swl = theVert * this->nbJoints + j;
                if (ind_swl >= this->skinWeightList.length())
                    this->skinWeightList.setLength(ind_swl + 1);
                double val = 0.0;
                int ind_tw = i * this->nbJoints + j;
                if (ind_tw < theWeights.length())
                    val = theWeights[ind_tw];
                this->skinWeightList[ind_swl] = val;
            }
            i++;
        }
    }

    MFnSingleIndexedComponent compFn;
    MObject weightsObj = compFn.create(MFn::kMeshVertComponent);
    compFn.addElements(objVertices);
    MFnSkinCluster skinFn(skinObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    this->skinWeightsForUndo.clear();
    if (!isNurbs) {
        skinFn.setWeights(meshDag, weightsObj, influenceIndices, theWeights, normalize,
                          &this->skinWeightsForUndo);
    } else {
        MFnDoubleIndexedComponent doubleFn;
        MObject weightsObjNurbs = doubleFn.create(MFn::kSurfaceCVComponent);
        int uVal, vVal;
        for (int vert : objVertices) {
            vVal = (int)vert % (int)numCVsInV_;
            uVal = (int)vert / (int)numCVsInV_;
            doubleFn.addElement(uVal, vVal);
        }
        skinFn.setWeights(nurbsDag, weightsObjNurbs, influenceIndices, theWeights, normalize,
                          &this->skinWeightsForUndo);
        transferPointNurbsToMesh(meshFn, nurbsFn);  // we transfer the points postions
        meshFn.updateSurface();
    }
    refreshPointsNormals();
    return status;
}

MStatus SkinBrushContext::applyCommand(int influence, std::unordered_map<int, float> &valuesToSet) {
    MStatus status;
    // we need to sort all of that one way or another ---------------- here it is ------
    std::map<int, double> valuesToSetOrdered(valuesToSet.begin(), valuesToSet.end());

    ModifierCommands theCommandIndex = getCommandIndexModifiers();
    double multiplier = 1.0;

    if ((theCommandIndex != ModifierCommands::LockVertices) && (theCommandIndex != ModifierCommands::UnlockVertices)) {
        MDoubleArray theWeights((int)this->nbJoints * valuesToSetOrdered.size(), 0.0);
        int repeatLimit = 1;
        if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen)
            repeatLimit = this->smoothRepeat;

        for (int repeat = 0; repeat < repeatLimit; ++repeat) {
            if (theCommandIndex == ModifierCommands::Smooth) {
                int i = 0;
                for (const auto &elem : valuesToSetOrdered) {
                    int theVert = elem.first;
                    double theWeight = elem.second;
                    std::vector<int> vertsAround = getSurroundingVerticesPerVert(theVert);

                    status = setAverageWeight(vertsAround, theVert, i, this->nbJoints,
                                              this->lockJoints, this->skinWeightList, theWeights,
                                              this->smoothStrengthVal * theWeight);
                    i++;
                }
            } else {
                if (this->ignoreLockVal) {
                    status =
                        editArray(theCommandIndex, influence, this->nbJoints,
                                  this->ignoreLockJoints, this->skinWeightList, valuesToSetOrdered,
                                  theWeights, this->doNormalize, multiplier);
                } else {
                    if (this->lockJoints[influence] == 1 && theCommandIndex != ModifierCommands::Sharpen)
                        return status;  //  if locked and it's not sharpen --> do nothing
                    status = editArray(theCommandIndex, influence, this->nbJoints, this->lockJoints,
                                       this->skinWeightList, valuesToSetOrdered, theWeights,
                                       this->doNormalize, multiplier);
                }
                if (status == MStatus::kFailure) {
                    return status;
                }
            }
            // now set the weights -----------------------------------------------------
            // here we should normalize -----------------------------------------------------
            int i = 0;
            // int prevVert = -1;
            for (const auto &elem : valuesToSetOrdered) {
                int theVert = elem.first;
                for (int j = 0; j < this->nbJoints; ++j) {
                    int ind_swl = theVert * this->nbJoints + j;
                    if (ind_swl >= this->skinWeightList.length())
                        this->skinWeightList.setLength(ind_swl + 1);
                    double val = 0.0;
                    int ind_tw = i * this->nbJoints + j;
                    if (ind_tw < theWeights.length())
                        val = theWeights[ind_tw];
                    this->skinWeightList[ind_swl] = val;
                }
                i++;
            }
        }
        MIntArray objVertices;
        for (const auto &elem : valuesToSetOrdered) {
            int theVert = elem.first;
            objVertices.append(theVert);
        }

        MFnSingleIndexedComponent compFn;
        MObject weightsObj = compFn.create(MFn::kMeshVertComponent);
        compFn.addElements(objVertices);

        // Set the new weights.
        // Initialize the skin cluster.
        MFnSkinCluster skinFn(skinObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        this->skinWeightsForUndo.clear();
        if (!isNurbs) {
            skinFn.setWeights(meshDag, weightsObj, influenceIndices, theWeights, normalize,
                              &this->skinWeightsForUndo);
        } else {
            MFnDoubleIndexedComponent doubleFn;
            MObject weightsObjNurbs = doubleFn.create(MFn::kSurfaceCVComponent);
            int uVal, vVal;
            for (int vert : objVertices) {

                vVal = (int)vert % (int)numCVsInV_;
                uVal = (int)vert / (int)numCVsInV_;
                doubleFn.addElement(uVal, vVal);
            }
            skinFn.setWeights(nurbsDag, weightsObjNurbs, influenceIndices, theWeights, normalize,
                              &this->skinWeightsForUndo);
            transferPointNurbsToMesh(meshFn, nurbsFn);  // we transfer the points postions
        }
        // in do press common
        // update values ---------------
        refreshPointsNormals();
    }
    return status;
}
// ---------------------------------------------------------------------
// COLORS
// ---------------------------------------------------------------------

MStatus SkinBrushContext::editSoloColorSet(bool doBlack) {
    MStatus status;

    MColorArray colToSet;
    MIntArray vtxToSet;
    for (unsigned int theVert = 0; theVert < this->numVertices; ++theVert) {
        double val = 0.0;
        int ind_swl = theVert * this->nbJoints + this->influenceIndex;
        if (ind_swl < this->skinWeightList.length())
            val = this->skinWeightList[ind_swl];
        bool isVtxLocked = this->lockVertices[theVert] == 1;
        bool update = doBlack || !(this->soloColorsValues[theVert] == 0 && val == 0);
        if (update) {  // dont update the black
            MColor soloColor = getASoloColor(val);
            this->soloCurrentColors[theVert] = soloColor;
            this->soloColorsValues[theVert] = val;
            if (isVtxLocked)
                colToSet.append(this->lockVertColor);
            else
                colToSet.append(soloColor);
            vtxToSet.append(theVert);
        }
    }
    meshFn.setSomeColors(vtxToSet, colToSet, &this->soloColorSet);
    meshFn.setSomeColors(vtxToSet, colToSet, &this->soloColorSet2);

    return status;
}

MStatus SkinBrushContext::refreshColors(MIntArray &editVertsIndices, MColorArray &multiEditColors,
                                        MColorArray &soloEditColors) {
    MStatus status = MS::kSuccess;
    if (multiEditColors.length() != editVertsIndices.length())
        multiEditColors.setLength(editVertsIndices.length());
    if (soloEditColors.length() != editVertsIndices.length())
        soloEditColors.setLength(editVertsIndices.length());

    for (unsigned int i = 0; i < editVertsIndices.length(); ++i) {
        int theVert = editVertsIndices[i];
        MColor multiColor, soloColor;
        bool isVtxLocked = this->lockVertices[theVert] == 1;

        for (int j = 0; j < this->nbJoints; ++j) {  // for each joint
            int ind_swl = theVert * this->nbJoints + j;
            if (ind_swl < this->skinWeightList.length()) {
                double val = this->skinWeightList[ind_swl];
                if (this->lockJoints[j] == 1)
                    multiColor += lockJntColor * val;
                else
                    multiColor += jointsColors[j] * val;
                if (j == this->influenceIndex) {
                    this->soloColorsValues[theVert] = val;
                    soloColor = getASoloColor(val);
                }
            }
        }
        this->multiCurrentColors[theVert] = multiColor;
        this->soloCurrentColors[theVert] = soloColor;
        if (isVtxLocked) {
            multiEditColors[i] = this->lockVertColor;
            soloEditColors[i] = this->lockVertColor;
        } else {
            multiEditColors[i] = multiColor;
            soloEditColors[i] = soloColor;
        }
    }
    return status;
}

MColor SkinBrushContext::getASoloColor(double val) const {
    if (val == 0) return MColor(0, 0, 0);
    val = (this->maxSoloColor - this->minSoloColor) * val + this->minSoloColor;
    MColor soloColor;
    if (this->soloColorTypeVal == 0) {  // black and white
        soloColor = MColor(val, val, val);
    } else if (this->soloColorTypeVal == 1) {  // lava
        val *= 2;
        if (val > 1)
            soloColor = MColor(val, (val - 1), 0);
        else
            soloColor = MColor(val, 0, 0);
    } else {  // influence
        soloColor = val * this->jointsColors[this->influenceIndex];
    }
    return soloColor;
}


void copyToFloatMatrix(const MMatrix& src, MFloatMatrix& dst) {
    dst[0][0] = (float)src[0][0];
    dst[0][1] = (float)src[0][1];
    dst[0][2] = (float)src[0][2];
    dst[0][3] = (float)src[0][3];
    dst[1][0] = (float)src[1][0];
    dst[1][1] = (float)src[1][1];
    dst[1][2] = (float)src[1][2];
    dst[1][3] = (float)src[1][3];
    dst[2][0] = (float)src[2][0];
    dst[2][1] = (float)src[2][1];
    dst[2][2] = (float)src[2][2];
    dst[2][3] = (float)src[2][3];
    dst[3][0] = (float)src[3][0];
    dst[3][1] = (float)src[3][1];
    dst[3][2] = (float)src[3][2];
    dst[3][3] = (float)src[3][3];
}




// ---------------------------------------------------------------------
// brush methods
// ---------------------------------------------------------------------
MStatus SkinBrushContext::getMesh() {
    MStatus status = MStatus::kSuccess;
    // Clear the previous data.
    this->meshDag = MDagPath();
    this->nurbsDag = MDagPath();
    this->skinObj = MObject();
    // -----------------------------------------------------------------
    // mesh
    // -----------------------------------------------------------------
    MDagPath dagPath;
    status = getSelection(meshDag);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    if (meshDag.apiType() == MFn::kNurbsSurface) {  // if is nurbs
        isNurbs = true;
        MObject foundMesh;
        findNurbsTesselate(meshDag, foundMesh);
        // nurbsDag = meshDag;
        status = MDagPath::getAPathTo(foundMesh, meshDag);
        // MFnNurbsSurface MfnSurface(dagPath);
        status = getSelection(nurbsDag);
        nurbsFn.setObject(nurbsDag);

        numCVsInV_ = nurbsFn.numCVsInV();
        numCVsInU_ = nurbsFn.numCVsInU();
        UIsPeriodic_ = nurbsFn.formInU() == MFnNurbsSurface::kPeriodic;
        VIsPeriodic_ = nurbsFn.formInV() == MFnNurbsSurface::kPeriodic;
        UDeg_ = nurbsFn.degreeU();
        VDeg_ = nurbsFn.degreeV();
        // int vertInd;
        if (VIsPeriodic_) numCVsInV_ -= VDeg_;
        if (UIsPeriodic_) numCVsInU_ -= UDeg_;
    } else {
        isNurbs = false;
    }
    // get the matrix
    MMatrix MIM = meshDag.inclusiveMatrix();
    MMatrix MIMI = meshDag.inclusiveMatrixInverse();

    copyToFloatMatrix(MIM, this->inclusiveMatrix);
    copyToFloatMatrix(MIMI, this->inclusiveMatrixInverse);

    // Set the mesh.
    meshFn.setObject(this->meshDag);
    numVertices = (unsigned)meshFn.numVertices();
    numFaces = (unsigned)meshFn.numPolygons();
    numEdges = (unsigned)meshFn.numEdges();
    meshFn.freeCachedIntersectionAccelerator();

    // I dont know why, but '33' seems to work well
    this->accelParams = meshFn.uniformGridParams(33, 33, 33);

    // getConnected vertices Guillaume function
    getConnectedVertices();
    getConnectedVerticesSecond();
    getConnectedVerticesThird();
    getFromMeshNormals();
    getConnectedVerticesFlatten(
        this->perVertexVerticesSetFLAT,
        this->perVertexVerticesSetINDEX,
        this->perFaceVerticesSetFLAT,
        this->perFaceVerticesSetINDEX
    );
    this->mayaRawPoints = meshFn.getRawPoints(&status);
    this->lockVertices = MIntArray(this->numVertices, 0);

    // -----------------------------------------------------------------
    // skin cluster
    // -----------------------------------------------------------------
    // Get the skin cluster node from the history of the mesh.
    MObject skinClusterObj;
    if (isNurbs) {
        status = getSkinCluster(nurbsDag, skinClusterObj);
    } else {
        status = getSkinCluster(meshDag, skinClusterObj);
    }
    CHECK_MSTATUS_AND_RETURN_IT(status);
    // Store the skin cluster for undo.
    skinObj = skinClusterObj;

    // Create a component object representing all vertices of the mesh.
    allVtxCompObj = allVertexComponents();
    MFnSingleIndexedComponent compFn;
    // Get the indices of all influences.
    influenceIndices = getInfluenceIndices();  // this->skinObj, this->inflDagPaths);

    // Get the skin cluster settings.
    unsigned int normalizeValue;
    getSkinClusterAttributes(skinClusterObj, maxInfluences, maintainMaxInfluences, normalizeValue);
    normalize = (normalizeValue > 0);
    getTheOrigMeshForMirror();
    return status;
}

MStatus SkinBrushContext::getTheOrigMeshForMirror() {
    MStatus status;

    // get the origMesh ----------------------------------------
    MObject origObj;
    if (this->isNurbs) {
        findNurbsTesselateOrig(this->meshDag, origObj);
    } else {
        findOrigMesh(skinObj, origObj);
    }
    status = MDagPath::getAPathTo(origObj, origMeshDag);
    // get the orgi vertices -----------------------------------
    meshOrigFn.setObject(origMeshDag);
    mayaOrigRawPoints = meshOrigFn.getRawPoints(&status);
    // I dont know why, but '33' seems to work well
    this->accelParamsOrigMesh = meshOrigFn.uniformGridParams(33, 33, 33);

    MObject origMeshNode = origMeshDag.node();
    status = intersectorOrigShape.create(origMeshNode);  // , matrix);

    // Create the intersector for the closest point operation for
    // keeping the shells together.
    MObject meshObj = meshDag.node();
    status = intersector.create(meshObj, meshDag.inclusiveMatrix());
    CHECK_MSTATUS_AND_RETURN_IT(status);  // only returns if bad
    return status;
}
/*
store vertices connections
*/


void  getAllConnections(
    MFnMesh &meshFn,  // For getting the mesh data
    MDagPath &meshDag,  // So I can get the "maya canonical" edges

    std::vector<std::vector<int>> &perVertexFaces,  // x[vertIdx] -> [list-of-faceIdxs]
    std::vector<std::vector<int>> &perVertexEdges,  // x[vertIdx] -> [list-of-edgeIdxs]
    std::vector<std::vector<int>> &perVertexVertices, // x[vertIdx] -> [list-of-vertIdxs that share a face with the input]
    std::vector<std::vector<int>> &perFaceVertices,  // x[faceIdx] -> [list-of-vertIdxs]
    std::vector<std::array<int, 2>> &perEdgeVertices, // x[edgeIdx] -> [pair-of-vertIdxs]
    std::vector<std::vector<std::array<int, 3>>> &perFaceTriangleVertices // x[face][triIdx] -> [list_of_vertIdxs]
){
    // Get the data from the mesh and dag path
    MIntArray counts, flatFaces, triangleCounts, triangleVertices;
    meshFn.getVertices(counts, flatFaces);
    meshFn.getTriangles(triangleCounts, triangleVertices);
    int numVertices = meshFn.numVertices();
    int numFaces = meshFn.numPolygons();
    int numEdges = meshFn.numEdges();

    // Reset all the output vectors
    perVertexFaces.clear();
    perVertexFaces.resize(numVertices);
    perVertexEdges.clear();
    perVertexEdges.resize(numVertices);
    perVertexVertices.clear();
    perVertexVertices.resize(numVertices);

    perFaceVertices.clear();
    perFaceVertices.resize(numFaces);
    perFaceTriangleVertices.clear();
    perFaceTriangleVertices.resize(numFaces);

    perEdgeVertices.clear();
    perEdgeVertices.resize(numEdges);


    // Get the face/vert correlations
    unsigned int iter = 0, triIter = 0;
    for (unsigned int faceId = 0; faceId < numFaces; ++faceId) {
        for (int i = 0; i < counts[faceId]; ++i, ++iter) {
            int indVertex = flatFaces[iter];
            perFaceVertices[faceId].push_back(indVertex);
            perVertexFaces[indVertex].push_back(faceId);
        }
        perFaceTriangleVertices[faceId].resize(triangleCounts[faceId]);
        for (int triId = 0; triId < triangleCounts[faceId]; ++triId) {
            perFaceTriangleVertices[faceId][triId][0] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][1] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][2] = triangleVertices[triIter++];
        }
    }

    // Get the edge/vert correlations
    MItMeshEdge edgeIter(meshDag);
    for (unsigned i = 0; !edgeIter.isDone(); edgeIter.next(), ++i) {
        int pt0Index = edgeIter.index(0);
        int pt1Index = edgeIter.index(1);
        perVertexEdges[pt0Index].push_back(i);
        perVertexEdges[pt1Index].push_back(i);
        perEdgeVertices[i][0] = pt0Index;
        perEdgeVertices[i][1] = pt1Index;
    }

    // Build the face-growing neighbors
#pragma omp parallel for
    for (int vertIdx = 0; vertIdx < numVertices; ++vertIdx) {
        std::vector<int> &toAdd = perVertexVertices[vertIdx];
        for (int faceIdx: perVertexFaces[vertIdx])
            std::vector<int> &faceVerts = perFaceVerticesSet[faceIdx];
            toAdd.insert(toAdd.end(),faceVerts.begin(),faceVerts.end());
        }
        // For such a short vec, sorting then erasing is the fastest
        std::sort(toAdd.begin(), toAdd.end());
        toAdd.erase(std::unique(toAdd.begin(), toAdd.end()), toAdd.end());
    }

    // TODO: Flatten all connections
    // And make a class type that provides easy access to count/connect data

}




void getConnectedVertices(
    int numFaces,
    int numVertices,
    const MDagPath &meshDag,
    const MFnMesh &meshFn,

    int &fullVertexListLength,
    std::vector<MIntArray>& perFaceVertices,
    std::vector<MIntArray>& perVertexFaces,
    std::vector<std::vector<MIntArray>>& perFaceTriangleVertices,
    std::vector<MIntArray>& perVertexEdges,
    std::vector<std::pair<int, int>>& perEdgeVertices,
    MIntArray &VertexCountPerPolygon,
    MIntArray &fullVertexList
    ) {

    // MIntArray vertexCount, vertexList;
    meshFn.getVertices(VertexCountPerPolygon, fullVertexList);
    fullVertexListLength = fullVertexList.length();

    MIntArray triangleCounts, triangleVertices;  // get the triangles to draw the mesh
    meshFn.getTriangles(triangleCounts, triangleVertices);

    // First set array sizes ----------------------------------------------
    perFaceVertices.clear();
    perVertexFaces.clear();
    perFaceTriangleVertices.clear();

    perFaceVertices.resize(numFaces);
    perVertexFaces.resize(numVertices);
    perFaceTriangleVertices.resize(numFaces);

    // end set array sizes ----------------------------------------------
    // First run --------------------------------------------------------
    for (unsigned int faceId = 0, iter = 0, triIter = 0; faceId < VertexCountPerPolygon.length();
        ++faceId) {
        perFaceVertices[faceId].clear();
        for (int i = 0; i < VertexCountPerPolygon[faceId]; ++i, ++iter) {
            int indVertex = fullVertexList[iter];
            perFaceVertices[faceId].append(indVertex);
            perVertexFaces[indVertex].append(faceId);
        }
        perFaceTriangleVertices[faceId].resize(triangleCounts[faceId]);
        for (int triId = 0; triId < triangleCounts[faceId]; ++triId) {
            perFaceTriangleVertices[faceId][triId].setLength(3);
            perFaceTriangleVertices[faceId][triId][0] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][1] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][2] = triangleVertices[triIter++];
        }
    }
    // get the edgesIndices to draw the wireframe --------------------
    MItMeshEdge edgeIter(meshDag);

    perEdgeVertices.clear();
    perVertexEdges.clear();
    perEdgeVertices.resize(edgeIter.count());
    perVertexEdges.resize(numVertices);

    unsigned int i = 0;
    for (; !edgeIter.isDone(); edgeIter.next()) {
        int pt0Index = edgeIter.index(0);
        int pt1Index = edgeIter.index(1);
        perVertexEdges[pt0Index].append(i);
        perVertexEdges[pt1Index].append(i);
        perEdgeVertices[i++] = std::make_pair(pt0Index, pt1Index);
    }
}

void getConnectedVerticesSecond(
    const MIntArray &perFaceVertices,
    int numFaces,

    std::vector<std::vector<int>> &perFaceVerticesSet
) {
    // Second run --------------------------------------------------------
    perFaceVerticesSet.clear();
    perFaceVerticesSet.resize(numFaces);
    for (int faceTmp = 0; faceTmp < numFaces; ++faceTmp) {
        std::vector<int> tmpSet;
        MIntArray surroundingVertices = perFaceVertices[faceTmp];

        tmpSet.resize(surroundingVertices.length());
        surroundingVertices.get(&tmpSet[0]);

        std::sort(tmpSet.begin(), tmpSet.end());
        perFaceVerticesSet[faceTmp] = tmpSet;
    }
}

void getConnectedVerticesThird(
    std::vector<std::vector<int>> &perVertexVerticesSet,
    std::vector<MIntArray> &perVertexFaces,
    std::vector<std::vector<int>> &perFaceVerticesSet,
    int numVertices
) {
    // fill the std_array connectedSetVertices ------------------------
    perVertexVerticesSet.clear();
    perVertexVerticesSet.resize(numVertices);

#pragma omp parallel for
    for (int vtxTmp = 0; vtxTmp < numVertices; ++vtxTmp) {
        std::vector<int> connVetsSet2;
        MIntArray connectFaces = perVertexFaces[vtxTmp];
        for (int fct = 0; fct < connectFaces.length(); ++fct) {
            auto surroundingVertices = perFaceVerticesSet[connectFaces[fct]];
            std::vector<int> connVetsSetTMP;

            std::set_union(connVetsSet2.begin(), connVetsSet2.end(), surroundingVertices.begin(),
                           surroundingVertices.end(), std::back_inserter(connVetsSetTMP));
            connVetsSet2 = connVetsSetTMP;
        }
        auto it = std::find(connVetsSet2.begin(), connVetsSet2.end(), vtxTmp);
        if (it != std::end(connVetsSet2)) {
            connVetsSet2.erase(it);
            perVertexVerticesSet[vtxTmp] = connVetsSet2;
        }
    }
}







void SkinBrushContext::getConnectedVertices() {
    MStatus status;

    // MIntArray vertexCount, vertexList;
    status = meshFn.getVertices(VertexCountPerPolygon, fullVertexList);
    this->fullVertexListLength = fullVertexList.length();

    MIntArray triangleCounts, triangleVertices;  // get the triangles to draw the mesh
    status = meshFn.getTriangles(triangleCounts, triangleVertices);

    // First set array sizes ----------------------------------------------
    this->perFaceVertices.clear();
    this->perVertexFaces.clear();
    this->perFaceTriangleVertices.clear();

    this->perFaceVertices.resize(this->numFaces);
    this->perVertexFaces.resize(this->numVertices);
    this->perFaceTriangleVertices.resize(this->numFaces);

    // end set array sizes ----------------------------------------------
    // First run --------------------------------------------------------
    for (unsigned int faceId = 0, iter = 0, triIter = 0; faceId < VertexCountPerPolygon.length();
         ++faceId) {
        perFaceVertices[faceId].clear();
        for (int i = 0; i < VertexCountPerPolygon[faceId]; ++i, ++iter) {
            int indVertex = fullVertexList[iter];
            perFaceVertices[faceId].append(indVertex);
            perVertexFaces[indVertex].append(faceId);
        }
        perFaceTriangleVertices[faceId].resize(triangleCounts[faceId]);
        for (int triId = 0; triId < triangleCounts[faceId]; ++triId) {
            perFaceTriangleVertices[faceId][triId].setLength(3);
            perFaceTriangleVertices[faceId][triId][0] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][1] = triangleVertices[triIter++];
            perFaceTriangleVertices[faceId][triId][2] = triangleVertices[triIter++];
        }
    }
    // get the edgesIndices to draw the wireframe --------------------
    MItMeshEdge edgeIter(meshDag);

    this->perEdgeVertices.clear();
    this->perVertexEdges.clear();
    this->perEdgeVertices.resize(edgeIter.count());
    this->perVertexEdges.resize(this->numVertices);

    unsigned int i = 0;
    for (; !edgeIter.isDone(); edgeIter.next()) {
        int pt0Index = edgeIter.index(0);
        int pt1Index = edgeIter.index(1);
        this->perVertexEdges[pt0Index].append(i);
        this->perVertexEdges[pt1Index].append(i);
        this->perEdgeVertices[i++] = std::make_pair(pt0Index, pt1Index);
    }
}
void SkinBrushContext::getConnectedVerticesSecond() {
    // Second run --------------------------------------------------------
    this->perFaceVerticesSet.clear();
    perFaceVerticesSet.resize(this->numFaces);
    for (int faceTmp = 0; faceTmp < numFaces; ++faceTmp) {
        std::vector<int> tmpSet;
        MIntArray surroundingVertices = this->perFaceVertices[faceTmp];

        tmpSet.resize(surroundingVertices.length());
        surroundingVertices.get(&tmpSet[0]);

        std::sort(tmpSet.begin(), tmpSet.end());
        perFaceVerticesSet[faceTmp] = tmpSet;
    }
}
void SkinBrushContext::getConnectedVerticesThird() {
    // fill the std_array connectedSetVertices ------------------------
    this->perVertexVerticesSet.clear();
    this->perVertexVerticesSet.resize(this->numVertices);

#pragma omp parallel for
    for (int vtxTmp = 0; vtxTmp < this->numVertices; ++vtxTmp) {
        std::vector<int> connVetsSet2;
        MIntArray connectFaces = perVertexFaces[vtxTmp];
        for (int fct = 0; fct < connectFaces.length(); ++fct) {
            auto surroundingVertices = perFaceVerticesSet[connectFaces[fct]];
            std::vector<int> connVetsSetTMP;

            std::set_union(connVetsSet2.begin(), connVetsSet2.end(), surroundingVertices.begin(),
                           surroundingVertices.end(), std::back_inserter(connVetsSetTMP));
            connVetsSet2 = connVetsSetTMP;
        }
        auto it = std::find(connVetsSet2.begin(), connVetsSet2.end(), vtxTmp);
        if (it != std::end(connVetsSet2)) {
            connVetsSet2.erase(it);
            this->perVertexVerticesSet[vtxTmp] = connVetsSet2;
        }
    }
}

void SkinBrushContext::getConnectedVerticesTyler() {
    std::vector<std::unordered_set<int>> faceNeighbors, edgeNeighbors;
    std::vector<int> fCounts, fIndices, eCounts, eIndices;

    getRawNeighbors(this->VertexCountPerPolygon, this->fullVertexList, this->numVertices,
                    faceNeighbors, edgeNeighbors);
    convertToCountIndex(faceNeighbors, fCounts, fIndices);
    convertToCountIndex(edgeNeighbors, eCounts, eIndices);
}

void SkinBrushContext::getFromMeshNormals() {
    this->verticesNormals.clear();
    this->verticesNormals.setLength(this->numVertices);
    // fill the normals ----------------------------------------------------
    this->normalsIds.clear();
    this->normalsIds.resize(this->numFaces);
    MIntArray normalCounts, normals;
    this->meshFn.getNormalIds(normalCounts, normals);

    int startIndex = 0;
#pragma omp parallel for
    for (int faceTmp = 0; faceTmp < normalCounts.length(); ++faceTmp) {
        int nbNormals = normalCounts[faceTmp];
        std::vector<int> tmpNormalsIds(nbNormals, 0);
        for (unsigned int k = 0; k < nbNormals; ++k) {
            int normalId = normals[k + startIndex];
            tmpNormalsIds[k] = normalId;
        }
        this->normalsIds[faceTmp] = tmpNormalsIds;
        startIndex += nbNormals;
    }
    MStatus stat;
    this->rawNormals = this->meshFn.getRawNormals(&stat);

    // get vertexNormalIndex --------------------------------------------------
    this->verticesNormalsIndices.clear();
    this->verticesNormalsIndices.setLength(numVertices);
#pragma omp parallel for
    for (int vertexInd = 0; vertexInd < this->numVertices; vertexInd++) {
        auto vertToFace = this->perVertexFaces[vertexInd];
        if (vertToFace.length() > 0) {
            int indFace = vertToFace[0];
            MIntArray surroundingVertices = this->perFaceVertices[indFace];
            int indNormal = -1;
            for (unsigned int j = 0; j < surroundingVertices.length(); ++j) {
                if (surroundingVertices[j] == vertexInd) {
                    indNormal = this->normalsIds[indFace][0];
                }
            }
            if (indNormal == -1) {
                MGlobal::displayInfo(
                    MString("cant find vertex [") + vertexInd +
                    MString("] in face [") + indFace + MString("] ;")
                );
            }
            this->verticesNormalsIndices.set(indNormal, vertexInd);
        }
    }
}
void SkinBrushContext::getConnectedVerticesFlatten(
    std::vector<int> &perVertexVerticesSetFLAT,
    std::vector<int> &perVertexVerticesSetINDEX,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX
) const {
    perVertexVerticesSetFLAT.clear();
    perVertexVerticesSetINDEX.clear();
    int sum = 0;
    for (auto surroundingVtices : this->perVertexVerticesSet) {
        perVertexVerticesSetINDEX.push_back(sum);
        for (int vtx : surroundingVtices) {
            perVertexVerticesSetFLAT.push_back(vtx);
            sum++;
        }
    }
    perVertexVerticesSetINDEX.push_back(sum);  // one extra for easy access
    //------------------------------------------------------------------------
    perFaceVerticesSetFLAT.clear();
    perFaceVerticesSetINDEX.clear();
    sum = 0;
    for (auto surroundingVtices : this->perFaceVerticesSet) {
        perFaceVerticesSetINDEX.push_back(sum);
        for (int vtx : surroundingVtices) {
            perFaceVerticesSetFLAT.push_back(vtx);
            sum++;
        }
    }
    perFaceVerticesSetINDEX.push_back(sum);  // one extra for easy access
}
std::vector<int> SkinBrushContext::getSurroundingVerticesPerVert(int vertexIndex) const {
    auto first = perVertexVerticesSetFLAT.begin() + perVertexVerticesSetINDEX[vertexIndex];
    auto last = perVertexVerticesSetFLAT.begin() + perVertexVerticesSetINDEX[vertexIndex + (int)1];
    std::vector<int> newVec(first, last);
    return newVec;
};

std::vector<int> SkinBrushContext::getSurroundingVerticesPerFace(int vertexIndex) const {
    auto first = perFaceVerticesSetFLAT.begin() + perFaceVerticesSetINDEX[vertexIndex];
    auto last = perFaceVerticesSetFLAT.begin() + perFaceVerticesSetINDEX[vertexIndex + (int)1];
    std::vector<int> newVec(first, last);
    return newVec;
};

//
// Description:
//      Get the dagPath of the currently selected object's shape node.
//      If there are multiple shape nodes return the first
//      non-intermediate shape. Return kNotFound if the object is not a
//      mesh.
//
// Input Arguments:
//      dagPath             The MDagPath of the selected mesh.
//
// Return Value:
//      MStatus             Return kNotFound if nothing is selected or
//                          the selection is not a mesh.
//
MStatus SkinBrushContext::getSelection(MDagPath &dagPath) {
    MStatus status = MStatus::kSuccess;

    unsigned int i;

    MSelectionList sel;
    status = MGlobal::getActiveSelectionList(sel);

    if (sel.isEmpty()) return MStatus::kNotFound;

    // Get the dagPath of the mesh before evaluating any selected
    // components. If there are no components selected the dagPath would
    // be empty and the command would fail to apply the smoothing to the
    // entire mesh.
    sel.getDagPath(0, dagPath);
    status = dagPath.extendToShape();

    // If there is more than one shape node extend to shape will fail.
    // In this case the shape node needs to be found differently.
    if (status != MStatus::kSuccess) {
        unsigned int numShapes;
        dagPath.numberOfShapesDirectlyBelow(numShapes);
        for (i = 0; i < numShapes; i++) {
            status = dagPath.extendToShapeDirectlyBelow(i);
            if (status == MStatus::kSuccess) {
                MFnDagNode shapeDag(dagPath);
                if (!shapeDag.isIntermediateObject()) break;
            }
        }
    }

    if (!(dagPath.hasFn(MFn::kMesh) || dagPath.hasFn(MFn::kNurbsSurface))) {
        dagPath = MDagPath();
        MGlobal::displayWarning("Only mesh and nurbs objects are supported.");
        return MStatus::kNotFound;
    }

    if (!status) {
        MGlobal::clearSelectionList();
        dagPath = MDagPath();
    }

    return status;
}

//
// Description:
//      Parse the history of the mesh at the given dagPath and return
//      MObject of the skin cluster node.
//
// Input Arguments:
//      dagPath             The MDagPath of the selected mesh.
//      skinClusterObj      The MObject of the found skin cluster node.
//
// Return Value:
//      MStatus             The MStatus for the setting up the
//                          dependency graph iterator.
//
MStatus SkinBrushContext::getSkinCluster(MDagPath &meshDag, MObject &skinClusterObj) {
    MStatus status;

    MObject meshObj = meshDag.node();

    MItDependencyGraph dependIter(meshObj, MFn::kSkinClusterFilter, MItDependencyGraph::kUpstream,
                                  MItDependencyGraph::kDepthFirst, MItDependencyGraph::kPlugLevel,
                                  &status);
    if (!status) {
        MGlobal::displayError("Failed setting up the dependency graph iterator.");
        return status;
    }

    if (!dependIter.isDone()) {
        skinClusterObj = dependIter.currentItem();
        MFnDependencyNode skinDep(skinClusterObj);
    }

    // Make sure that the mesh is bound to a skin cluster.
    if (skinClusterObj.isNull()) {
        MGlobal::displayWarning("The selected mesh is not bound to a skin cluster.");
        return MStatus::kNotFound;
    }

    return status;
}

MStatus SkinBrushContext::fillArrayValues(MObject &skinCluster, bool doColors) {
    MStatus status = MS::kSuccess;
    MFnSkinCluster skinFn(skinCluster, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    unsigned int infCount;

    if (!isNurbs) {
        status = skinFn.getWeights(meshDag, allVtxCompObj, this->skinWeightList, infCount);
    } else {
        status = skinFn.getWeights(nurbsDag, allVtxCompObj, this->skinWeightList, infCount);
    }
    CHECK_MSTATUS_AND_RETURN_IT(status);
    this->nbJoints = infCount;

    // quickly the ignore locks
    this->ignoreLockJoints.clear();
    this->ignoreLockJoints = MIntArray(this->nbJoints, 0);

    if (doColors) {
        skin_weights_.resize(this->numVertices);

        this->multiCurrentColors.clear();
        this->multiCurrentColors.setLength(this->numVertices);
        // get values for array --
        for (unsigned int vertexIndex = 0; vertexIndex < this->numVertices; ++vertexIndex) {
            MColor theColor(0.0, 0.0, 0.0);
            for (unsigned int indexInfluence = 0; indexInfluence < infCount;
                 indexInfluence++) {  // for each joint

                int ind_swl = vertexIndex * infCount + indexInfluence;
                double theWeight = 0.0;
                if (ind_swl < this->skinWeightList.length())
                    theWeight = this->skinWeightList[ind_swl];

                if (doColors) {
                    if (lockJoints[indexInfluence] == 1)
                        theColor += lockJntColor * theWeight;
                    else
                        theColor += this->jointsColors[indexInfluence] * theWeight;
                }
            }
            if (doColors) {  // not store lock vert color
                this->multiCurrentColors[vertexIndex] = theColor;
            }
        }
    }

    return status;
}

MStatus SkinBrushContext::displayWeightValue(int vertexIndex, bool displayZero) const {
    MString toDisplay = MString("weigth of vtx (") + vertexIndex + MString(") : ");
    for (unsigned int indexInfluence = 0; indexInfluence < this->nbJoints;
         indexInfluence++) {  // for each joint
        double theWeight = 0.0;
        int ind_swl = vertexIndex * this->nbJoints + indexInfluence;
        if (ind_swl < this->skinWeightList.length())
            theWeight = this->skinWeightList[ind_swl];
        if (theWeight == 0 && !displayZero) continue;
        toDisplay += MString("[") + indexInfluence + MString(": ") + theWeight + MString("] ");
    }
    MGlobal::displayInfo(toDisplay);
    return MS::kSuccess;
}

MStatus SkinBrushContext::fillArrayValuesDEP(MObject &skinCluster, bool doColors) {
    MStatus status = MS::kSuccess;

    MFnDependencyNode skinClusterDep(skinCluster);

    MPlug weight_list_plug = skinClusterDep.findPlug("weightList", false);
    MPlug matrix_plug = skinClusterDep.findPlug("matrix", false);
    // MGlobal::displayInfo(weight_list_plug.name());
    int nbElements = weight_list_plug.numElements();
    unsigned int infCount = matrix_plug.numElements();

    matrix_plug.getExistingArrayAttributeIndices(this->deformersIndices);

    this->nbJointsBig = 0;
    for (int el : this->deformersIndices) {
        if (el > this->nbJointsBig) this->nbJointsBig = el;
    }
    this->nbJointsBig += 1;
    this->nbJoints = infCount;

    // For the first component, the weights are ordered by influence object in the same order that
    // is returned by the MFnSkinCluster::influenceObjects method.
    // use influenceIndices
    skin_weights_.resize(nbElements);
    if (doColors) {
        this->multiCurrentColors.clear();
        this->multiCurrentColors.setLength(nbElements);
    }
    this->skinWeightList = MDoubleArray(nbElements * this->nbJoints, 0.0);

// MThreadUtils::syncNumOpenMPThreads();
// in option C/C++ turn omp on !!!!
#pragma omp parallel for  // collapse(2)
    for (int i = 0; i < nbElements; ++i) {
        // weightList[i]
        MPlug ith_weights_plug = weight_list_plug.elementByPhysicalIndex(i);
        int vertexIndex = ith_weights_plug.logicalIndex();

        // weightList[i].weight
        MPlug plug_weights = ith_weights_plug.child(0);  // access first compound child
        int nb_weights = plug_weights.numElements();

        MColor theColor(0, 0, 0, 1);
        for (int j = 0; j < nb_weights; j++) {  // for each joint
            MPlug weight_plug = plug_weights.elementByPhysicalIndex(j);
            // weightList[i].weight[j]
            int indexInfluence = weight_plug.logicalIndex();
            double theWeight = weight_plug.asDouble();
            // store in the correct Spot --
            indexInfluence = this->indicesForInfluenceObjects[indexInfluence];
            int ind_swl = vertexIndex * this->nbJoints + indexInfluence;
            if (ind_swl >= this->skinWeightList.length())
                this->skinWeightList.setLength(ind_swl + 1);
            this->skinWeightList[ind_swl] = theWeight;
            if (doColors) {  // and not locked
                if (this->lockJoints[indexInfluence] == 1)
                    theColor += lockJntColor * theWeight;
                else
                    theColor += this->jointsColors[indexInfluence] * theWeight;
            }
        }
        if (doColors) {  // not store lock vert color
            this->multiCurrentColors[vertexIndex] = theColor;
        }
    }
    return status;
}
//
// Description:
//      Return the influence indices of all influences of the given
//      skin cluster node.
//
// Input Arguments:
//      skinCluster         The MObject of the skin cluster node.
//
// Return Value:
//      int array           The array of all influence indices.
//
MIntArray SkinBrushContext::getInfluenceIndices(){
    unsigned int i;
    MFnSkinCluster skinFn(this->skinObj);

    MIntArray influenceIndices;

    this->inflDagPaths.clear();
    skinFn.influenceObjects(this->inflDagPaths);
    int lent = this->inflDagPaths.length();
    // first clear --------------------------
    this->inflNames.clear();
    this->inflNamePixelSize.clear();
    this->indicesForInfluenceObjects.clear();

    this->inflNames.setLength(lent);
    this->inflNamePixelSize.setLength(2 * lent);
    this->indicesForInfluenceObjects.setLength(lent);
    MStatus stat;
    this->nbJoints = lent;

    QFontMetrics fontMetrics(QFont("MS Shell Dlg 2", 14));

    for (i = 0; i < lent; i++) {
        influenceIndices.append((int)i);
        MFnDependencyNode influenceFn(this->inflDagPaths[i].node(), &stat);
        if (stat != MS::kSuccess) {
            MGlobal::displayError(MString("Crashing query influence ") + i);
        }
        MString iname = influenceFn.name();
        this->inflNames[i] = iname;

        QRect sz = fontMetrics.boundingRect(iname.asChar());
        int wid = sz.width() + 2;
        int height = sz.height() + 2;

        wid = wid > 5 ? wid : 5;
        height = height > 5 ? height : 5;

        this->inflNamePixelSize[2 * i] = wid;
        this->inflNamePixelSize[2 * i + 1] = height;

        int indexLogical = skinFn.indexForInfluenceObject(this->inflDagPaths[i]);
        this->indicesForInfluenceObjects[indexLogical] = i;
    }
    return influenceIndices;
}

MStatus SkinBrushContext::querySkinClusterValues(MObject &skinCluster, MIntArray &verticesIndices, MDoubleArray &theSkinWeightList,
                                                 bool doColors) const {
    MStatus status = MS::kSuccess;

    if (meshDag.node().isNull()) return MStatus::kNotFound;

    MFnSkinCluster skinFn(skinCluster, &status);
    MDoubleArray weightsVertices;
    unsigned int infCount;

    if (!isNurbs) {
        MFnSingleIndexedComponent compFn;
        MObject weightsObj = compFn.create(MFn::kMeshVertComponent);
        compFn.addElements(verticesIndices);
        status = skinFn.getWeights(meshDag, weightsObj, weightsVertices, infCount);
    } else {
        MFnDoubleIndexedComponent doubleFn;
        MObject weightsObjNurbs = doubleFn.create(MFn::kSurfaceCVComponent);
        int uVal, vVal;
        for (int vert : verticesIndices) {
            vVal = (int)vert % (int)numCVsInV_;
            uVal = (int)vert / (int)numCVsInV_;
            doubleFn.addElement(uVal, vVal);
        }
        status = skinFn.getWeights(nurbsDag, weightsObjNurbs, weightsVertices, infCount);
    }
    if (status != MS::kSuccess) {
        MGlobal::displayError("querySkinClusterValues | can't Query skin values \n");
    }

    for (unsigned int i = 0; i < verticesIndices.length(); ++i) {
        int vertexIndex = verticesIndices[i];

        MColor theColor;
        for (unsigned int j = 0; j < infCount; j++) {  // for each joint
            double theWeight = weightsVertices[i * infCount + j];
            int ind_swl = vertexIndex * this->nbJoints + j;
            if (ind_swl >= this->skinWeightList.length())
                theSkinWeightList.setLength(ind_swl + 1);
            theSkinWeightList[ind_swl] = theWeight;
            if (doColors) {
                if (lockJoints[j] == 1)
                    theColor += lockJntColor * theWeight;
                else
                    theColor += this->jointsColors[j] * theWeight;
            }
        }
    }
    return status;
}

//
// Description:
//      Get the influence attributes from the given skin cluster object.
//
// Input Arguments:
//      skinCluster             The MObject of the skin cluster node.
//      maxInfluences           The max number of influences per vertex.
//      maintainMaxInfluences   True, if the max influences count should
//                              be maintained.
//      normalize               True, if the weights are normalized.
//
// Return Value:
//      None
//
void SkinBrushContext::getSkinClusterAttributes(MObject &skinCluster, unsigned int &maxInfluences,
                                                bool &maintainMaxInfluences,
                                                unsigned int &normalize) const {
    // Get the settings from the skin cluster node.
    MFnDependencyNode skinMFn(skinCluster);

    MPlug maxInflPlug = skinMFn.findPlug("maxInfluences", false);
    maxInfluences = (unsigned)maxInflPlug.asInt();

    MPlug maintainInflPlug = skinMFn.findPlug("maintainMaxInfluences", false);
    maintainMaxInfluences = maintainInflPlug.asBool();

    MPlug normalizePlug = skinMFn.findPlug("normalizeWeights", false);
    normalize = (unsigned)normalizePlug.asInt();
}

bool SkinBrushContext::getMirrorHit(int &faceHit, MFloatPoint &hitPoint) const {
    MStatus stat;

    MMatrix mirrorMatrix;
    double XVal = 1., YVal = 1.0, ZVal = 1.0;
    if ((paintMirror == 1) || (paintMirror == 4) || (paintMirror == 7)) XVal = -1.;
    if ((paintMirror == 2) || (paintMirror == 5) || (paintMirror == 8)) YVal = -1.;
    if ((paintMirror == 3) || (paintMirror == 6) || (paintMirror == 9)) ZVal = -1.;
    mirrorMatrix.matrix[0][0] = XVal;
    mirrorMatrix.matrix[0][1] = 0;
    mirrorMatrix.matrix[0][2] = 0;
    mirrorMatrix.matrix[1][0] = 0;
    mirrorMatrix.matrix[1][1] = YVal;
    mirrorMatrix.matrix[1][2] = 0;
    mirrorMatrix.matrix[2][0] = 0;
    mirrorMatrix.matrix[2][1] = 0;
    mirrorMatrix.matrix[2][2] = ZVal;

    // we're going to mirror by x -1'
    MPointOnMesh pointInfo;
    if (paintMirror > 0 && paintMirror < 4) {  // if we compute the orig mesh
        MPoint pointToMirror = MPoint(this->origHitPoint);
        MPoint mirrorPoint = pointToMirror * mirrorMatrix;

        stat = intersectorOrigShape.getClosestPoint(mirrorPoint, pointInfo, mirrorMinDist);
        if (MS::kSuccess != stat) return false;

        faceHit = pointInfo.faceIndex();
        int hitTriangle = pointInfo.triangleIndex();
        float hitBary1, hitBary2;
        pointInfo.getBarycentricCoords(hitBary1, hitBary2);

        MIntArray triangle = this->perFaceTriangleVertices[faceHit][hitTriangle];

        float hitBary3 = (1 - hitBary1 - hitBary2);
        float x = this->mayaRawPoints[triangle[0] * 3] * hitBary1 +
                  this->mayaRawPoints[triangle[1] * 3] * hitBary2 +
                  this->mayaRawPoints[triangle[2] * 3] * hitBary3;
        float y = this->mayaRawPoints[triangle[0] * 3 + 1] * hitBary1 +
                  this->mayaRawPoints[triangle[1] * 3 + 1] * hitBary2 +
                  this->mayaRawPoints[triangle[2] * 3 + 1] * hitBary3;
        float z = this->mayaRawPoints[triangle[0] * 3 + 2] * hitBary1 +
                  this->mayaRawPoints[triangle[1] * 3 + 2] * hitBary2 +
                  this->mayaRawPoints[triangle[2] * 3 + 2] * hitBary3;
        hitPoint = MFloatPoint(x, y, z) * this->inclusiveMatrix;
    } else {
        MPoint mirrorPoint = MPoint(this->centerOfBrush) * mirrorMatrix;
        stat = intersector.getClosestPoint(mirrorPoint, pointInfo, mirrorMinDist);
        if (MS::kSuccess != stat) return false;

        faceHit = pointInfo.faceIndex();
        hitPoint = MFloatPoint(mirrorPoint);
    }
    return true;
}

bool SkinBrushContext::computeHit(short screenPixelX, short screenPixelY, bool getNormal,
                                  int &faceHit, MFloatPoint &hitPoint){
    MStatus stat;

    view.viewToWorld(screenPixelX, screenPixelY, worldPoint, worldVector);

    // float hitRayParam;
    float hitBary1;
    float hitBary2;
    int hitTriangle;
    // If v1, v2, and v3 vertices of that triangle,
    // then the barycentric coordinates are such that
    // hitPoint = (*hitBary1)*v1 + (*hitBary2)*v2 + (1 - *hitBary1 - *hitBary2)*v3;
    // If no hit was found, the referenced value will not be modified,

    bool foundIntersect =
        meshFn.closestIntersection(worldPoint, worldVector, nullptr, nullptr, false, MSpace::kWorld,
                                   9999, false, &this->accelParams, hitPoint, &this->pressDistance,
                                   &faceHit, &hitTriangle, &hitBary1, &hitBary2, 0.0001f, &stat);

    if (!foundIntersect) return false;

    if (paintMirror > 0 && paintMirror < 4) {  // if we compute the orig
        MIntArray triangle = this->perFaceTriangleVertices[faceHit][hitTriangle];
        float hitBary3 = (1 - hitBary1 - hitBary2);
        float x = this->mayaOrigRawPoints[triangle[0] * 3] * hitBary1 +
                  this->mayaOrigRawPoints[triangle[1] * 3] * hitBary2 +
                  this->mayaOrigRawPoints[triangle[2] * 3] * hitBary3;
        float y = this->mayaOrigRawPoints[triangle[0] * 3 + 1] * hitBary1 +
                  this->mayaOrigRawPoints[triangle[1] * 3 + 1] * hitBary2 +
                  this->mayaOrigRawPoints[triangle[2] * 3 + 1] * hitBary3;
        float z = this->mayaOrigRawPoints[triangle[0] * 3 + 2] * hitBary1 +
                  this->mayaOrigRawPoints[triangle[1] * 3 + 2] * hitBary2 +
                  this->mayaOrigRawPoints[triangle[2] * 3 + 2] * hitBary3;
        origHitPoint = MFloatPoint(x, y, z);
    }

    // ----------- get normal for display ---------------------
    if (getNormal){
        meshFn.getPolygonNormal(faceHit, this->normalVector, MSpace::kWorld);
    }
    return true;
}

bool SkinBrushContext::expandHit(int faceHit, MFloatPoint &hitPoint,
                                 std::unordered_map<int, float> &dicVertsDist) const {
    // ----------- compute the vertices around ---------------------
    auto verticesSet = getSurroundingVerticesPerFace(faceHit);
    bool foundHit = false;
    for (int ptIndex : verticesSet) {
        MFloatPoint posPoint(this->mayaRawPoints[ptIndex * 3], this->mayaRawPoints[ptIndex * 3 + 1],
                             this->mayaRawPoints[ptIndex * 3 + 2]);
        float dist = posPoint.distanceTo(hitPoint);
        if (dist <= this->sizeVal) {
            foundHit = true;
            auto ret = dicVertsDist.insert(std::make_pair(ptIndex, dist));
            if (!ret.second) ret.first->second = std::min(dist, ret.first->second);
        }
    }
    return foundHit;
}

void SkinBrushContext::addBrushShapeFallof(std::unordered_map<int, float> &dicVertsDist) const {
    double valueStrength = strengthVal;
    if (this->modifierNoneShiftControl == ModifierKeys::ControlShift || this->commandIndex == ModifierCommands::Smooth) {
        valueStrength = smoothStrengthVal;  // smooth always we use the smooth value different of
                                            // the regular value
    }

    if (fractionOversamplingVal) valueStrength /= oversamplingVal;

    for (auto &element : dicVertsDist) {
        float value = 1.0 - (element.second / this->sizeVal);
        value = (float)getFalloffValue(value, valueStrength);
        element.second = value;
    }
}

void SkinBrushContext::getColorWithMirror(int vertexIndex, float valueBase, float valueMirror,
                                          MColorArray &multiEditColors, MColorArray &soloEditColors,
                                          MColor &multColor, MColor &soloColor) const {
    MColor white(1, 1, 1, 1);
    MColor black(0, 0, 0, 1);
    ModifierCommands theCommandIndex = getCommandIndexModifiers();

    float sumValue = valueBase + valueMirror;
    sumValue = std::min(float(1.0), sumValue);
    float biggestValue = std::max(valueBase, valueMirror);

    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        if (theCommandIndex == ModifierCommands::LockVertices) {  // lock verts if not already locked
            soloColor = this->lockVertColor;
            multColor = this->lockVertColor;
        } else {  // unlock verts
            multColor = this->multiCurrentColors[vertexIndex];
            soloColor = this->soloCurrentColors[vertexIndex];
        }
    } else if (!this->lockVertices[vertexIndex]) {
        MColor currentColor = this->multiCurrentColors[vertexIndex];
        int influenceMirrorColorIndex = this->mirrorInfluences[this->influenceIndex];
        MColor jntColor = this->jointsColors[this->influenceIndex];
        MColor jntMirrorColor = this->jointsColors[influenceMirrorColorIndex];
        if (lockJoints[this->influenceIndex] == 1) jntColor = lockJntColor;
        if (lockJoints[influenceMirrorColorIndex] == 1) jntMirrorColor = lockJntColor;
        // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
        // UnLockVertices

        if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen) {
            soloColor = biggestValue * white + (1.0 - biggestValue) * this->soloCurrentColors[vertexIndex];
            multColor = biggestValue * white + (1.0 - biggestValue) * this->multiCurrentColors[vertexIndex];
        } else {
            double newW = 0.0;
            int ind_swl = vertexIndex * nbJoints + this->influenceIndex;
            if (ind_swl < this->skinWeightList.length())
                newW = this->skinWeightList[ind_swl];
            double newWMirror = 0.0;
            int ind_swlM = vertexIndex * nbJoints + influenceMirrorColorIndex;
            if (ind_swlM < this->skinWeightList.length())
                newWMirror = this->skinWeightList[ind_swlM];
            double sumNewWs = newW + newWMirror;

            if (theCommandIndex == ModifierCommands::Remove) {
                newW -= biggestValue;
                newW = std::max(0.0, newW);
                multColor = currentColor * (1.0 - biggestValue) + black * biggestValue;  // white

            } else {
                if (theCommandIndex == ModifierCommands::Add) {
                    newW += double(valueBase);
                    newWMirror += double(valueMirror);
                } else if (theCommandIndex == ModifierCommands::AddPercent) {
                    newW += valueBase * newW;
                    newWMirror += valueMirror * newWMirror;

                } else if (theCommandIndex == ModifierCommands::Absolute) {
                    newW = valueBase;
                    newWMirror = valueMirror;
                }

                newW = std::min(1.0, newW);
                newWMirror = std::min(1.0, newWMirror);
                sumNewWs = newW + newWMirror;
                double currentColorVal = 1.0 - sumNewWs;
                if (sumNewWs > 1.0) {
                    newW /= sumNewWs;
                    newWMirror /= sumNewWs;
                    currentColorVal = 0.0;
                }
                multColor = currentColor * currentColorVal + jntColor * newW + jntMirrorColor * newWMirror;  // white
            }
            soloColor = getASoloColor(newW);
        }
    }
}


void SkinBrushContext::preparePaint(std::unordered_map<int, float> &dicVertsDist,
                                       std::unordered_map<int, float> &dicVertsDistPrevPaint,
                                       std::vector<float> &intensityValues,
                                       std::unordered_map<int, float> &skinValToSet,
                                       std::set<int> &theVerticesPainted, bool mirror) {

    // MGlobal::displayInfo("perform Paint");
    double multiplier = 1.0;
    if (!postSetting && commandIndex != ModifierCommands::Smooth)
        multiplier = .1;  // less applying if dragging paint

    bool isCommandLock =
        ((commandIndex == ModifierCommands::LockVertices) || (commandIndex == ModifierCommands::UnlockVertices))
        && (this->modifierNoneShiftControl != ModifierKeys::Control);

    auto endOfFind = dicVertsDistPrevPaint.end();
    for (const auto &element : dicVertsDist) {
        int index = element.first;
        float value = element.second * multiplier;
        // check if need to set this color, we store in intensityValues to check if it's already at
        // 1 -------
        if ((this->lockVertices[index] == 1 && !isCommandLock) || intensityValues[index] == 1) {
            continue;
        }
        // get the correct value of paint by adding this value -----
        value += intensityValues[index];
        auto res = dicVertsDistPrevPaint.find(index);
        if (res != endOfFind) {  // we substract the smallest
            value -= std::min(res->second, element.second);
        }
        value = std::min(value, (float)1.0);
        intensityValues[index] = value;

        // add to array of values to set at the end---------------
        // we need to check if it is in the regular array and make adjustements
        auto ret = skinValToSet.insert(std::make_pair(index, value));
        if (!ret.second)
            ret.first->second = std::max(value, ret.first->second);
        else
            theVerticesPainted.insert(index);
        // end add to array of values to set at the end--------------------
    }
    dicVertsDistPrevPaint = dicVertsDist;

    if (!this->postSetting) {
        // MGlobal::displayInfo("apply the skin stuff");
        // still have to deal with the colors damn it
        if (skinValToSet.size() > 0) {
            int theInfluence = this->influenceIndex;
            if (mirror) theInfluence = this->mirrorInfluences[this->influenceIndex];
            applyCommand(theInfluence, skinValToSet);
            intensityValues = std::vector<float>(this->numVertices, 0);
            dicVertsDistPrevPaint.clear();
            skinValToSet.clear();
        }
    }
}
MStatus SkinBrushContext::doPerformPaint() {
    MStatus status = MStatus::kSuccess;

    MColorArray multiEditColors, soloEditColors;
    MIntArray editVertsIndices;

    for (const auto &pt : this->mirroredJoinedArray) {
        int ptIndex = pt.first;
        float weightBase = pt.second.first;
        float weightMirror = pt.second.second;
        MColor multColor, soloColor;
        this->getColorWithMirror(ptIndex, weightBase, weightMirror, multiEditColors, soloEditColors, multColor, soloColor);
        editVertsIndices.append(ptIndex);
        multiEditColors.append(multColor);
        soloEditColors.append(soloColor);

    }
    // do actually set colors -----------------------------------
    if (this->soloColorVal == 0) {
        meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet);
        meshFn.setSomeColors(editVertsIndices, multiEditColors, &this->fullColorSet2);
    } else {
        meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet);
        meshFn.setSomeColors(editVertsIndices, soloEditColors, &this->soloColorSet2);
    }

    if (this->useColorSetsWhilePainting || !this->postSetting) {
        if ((this->commandIndex == ModifierCommands::LockVertices) || (this->commandIndex == ModifierCommands::UnlockVertices)) {
            // without that it doesn't refresh because mesh is not invalidated, meaning the
            // skinCluster hasn't changed
            meshFn.updateSurface();
        }
        maya2019RefreshColors(true);
    }
    return status;
}

//
// Description:
//      Return a component MObject for all vertex components of the
//      given mesh.
//
// Input Arguments:
//      meshDag             The dagPath of the mesh object.
//
// Return Value:
//      MObject             The component object for all mesh vertices.
//
MObject SkinBrushContext::allVertexComponents(){
    MObject vtxComponents;
    if (!isNurbs) {
        MFnSingleIndexedComponent compFn;
        vtxComponents = compFn.create(MFn::kMeshVertComponent);
        compFn.setCompleteData((int)numVertices);
        numElements = numVertices;
    } else {
        MFnDoubleIndexedComponent allCVs;
        int sizeInV = nurbsFn.numCVsInV();
        int sizeInU = nurbsFn.numCVsInU();

        vtxComponents = allCVs.create(MFn::kSurfaceCVComponent);
        allCVs.setCompleteData(sizeInU, sizeInV);
        numElements = allCVs.elementCount();
    }
    return vtxComponents;
}

//
// Description:
//      Return the vertex indices of the brush volume which are within
//      the range of the current index.
//
// Input Arguments:
//      index               The vertex index.
//      volumeIndices       The array of all indices of the brush
//                          volume.
//      rangeIndices        The array of indices which are in range of
//                          the vertex.
//      values              The array of falloff values based on the
//                          distance to the center vertex.
//
// Return Value:
//      None
//

double getFalloffValue(int curveVal, double value, double strength) {
    switch (curveVal) {
        case 0: // no falloff
            return strength;
        case 1: // linear
            return value * strength;
        case 2: // smoothstep
            return (value * value * (3 - 2 * value)) * strength;
        case 3: // narrow - quadratic
            return (1 - pow((1 - value) / 1, 0.4)) * strength;
        default:
            return value;
    }
}




void getVerticesInVolumeRange(
    int index,
    int curveVal,
    double sizeVal,
    double rangeVal,
    double strengthVal,
    double fractionOversamplingVal,
    double oversamplingVal,
    const MDagPath meshDag,
    const MIntArray &volumeIndices,
    MIntArray &rangeIndices,
    MFloatArray &values
) {
    unsigned int i;

    double radius = sizeVal * rangeVal;
    radius *= radius;

    double smoothStrength = strengthVal;
    if (fractionOversamplingVal) smoothStrength /= oversamplingVal;

    MItMeshVertex vtxIter(meshDag);
    int prevIndex;
    vtxIter.setIndex(index, prevIndex);

    MPoint point = vtxIter.position(MSpace::kWorld);

    for (i = 0; i < volumeIndices.length(); i++) {
        int volumeIndex = volumeIndices[i];

        vtxIter.setIndex(volumeIndex, prevIndex);

        MPoint pnt = vtxIter.position(MSpace::kWorld);

        double x = pnt.x - point.x;
        double y = pnt.y - point.y;
        double z = pnt.z - point.z;
        double delta = x*x + y*y + z*z;

        if (volumeIndex != index && delta <= radius) {
            rangeIndices.append(volumeIndex);

            float value = (float)(1 - (delta / radius));
            value = (float)getFalloffValue(curveVal, value, smoothStrength);
            values.append(value);
        }

        vtxIter.next();
    }
}






void SkinBrushContext::getVerticesInVolumeRange(int index, MIntArray &volumeIndices,
                                                MIntArray &rangeIndices, MFloatArray &values) const {
    unsigned int i;

    double radius = sizeVal * rangeVal;
    radius *= radius;

    double smoothStrength = strengthVal;
    if (fractionOversamplingVal) smoothStrength /= oversamplingVal;

    MItMeshVertex vtxIter(meshDag);
    int prevIndex;
    vtxIter.setIndex(index, prevIndex);

    MPoint point = vtxIter.position(MSpace::kWorld);

    for (i = 0; i < volumeIndices.length(); i++) {
        int volumeIndex = volumeIndices[i];

        vtxIter.setIndex(volumeIndex, prevIndex);

        MPoint pnt = vtxIter.position(MSpace::kWorld);

        double x = pnt.x - point.x;
        double y = pnt.y - point.y;
        double z = pnt.z - point.z;
        double delta = x*x + y*y + z*z;

        if (volumeIndex != index && delta <= radius) {
            rangeIndices.append(volumeIndex);

            float value = (float)(1 - (delta / radius));
            value = (float)getFalloffValue(value, smoothStrength);
            values.append(value);
        }

        vtxIter.next();
    }
}

//
// Description:
//      Calculate the brush weight value based on the given linear
//      falloff value.
//
// Input Arguments:
//      value               The linear falloff value.
//      strength            The brush strength value.
//
// Return Value:
//      double              The brush curve-based falloff value.
//
double SkinBrushContext::getFalloffValue(double value, double strength) const {

    switch (curveVal) {
        case 0: // no falloff
            return strength;
        case 1: // linear
            return value * strength;
        case 2: // smoothstep
            return (value * value * (3 - 2 * value)) * strength;
        case 3: // narrow - quadratic
            return (1 - pow((1 - value) / 1, 0.4)) * strength;
        default:
            return value;
    }
}

void SkinBrushContext::setInViewMessage(bool display) const {
    // TODO: Update this dynamically based on hotkey prefs
    if (display && messageVal){
        MString cmd = "inViewMessage -position topCenter -statusMessage \""
            "<hl>LMB</hl> to add  |  "
            "<hl>MMB</hl> to adjust  |  ";
        if (smoothModifier == ModifierKeys::Shift){
            cmd += "<hl>Ctrl</hl> to remove  |  "
                "<hl>Shift</hl> to smooth"
                "\"";
        }
        else{
            cmd += "<hl>Ctrl</hl> to smooth  |  "
                "<hl>Shift</hl> to remove"
                "\"";
        }
        MGlobal::executeCommand(cmd);
    }
    else{
        MGlobal::executeCommand("inViewMessage -clear topCenter");
    }
}
