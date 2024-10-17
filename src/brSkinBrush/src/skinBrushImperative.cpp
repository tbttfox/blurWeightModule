
/*
#computeHit
#drawMeshWhileDrag
#expandHit
#getASoloColor
#getColorWithMirror
#getCommandIndexModifiers
#getFalloffValue
#getMirrorHit
#getSurroundingVerticesPerVert
#growArrayOfHitsFromCenters
#addBrushShapeFallof
#mergeMirrorArray
#preparePaint
#refreshPointsNormals
#refreshColors
#applyCommand
#applyCommandMirror
#editArrayMirror
#doDrag
#doDragCommon
#doPerformPaint
#maya2019RefreshColors

#doPress
#doPressCommon

#doRelease
#doReleaseCommon

doTheAction

*/



#include "enums.h"
#include <math.h>


#include <maya/M3dView.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MCursor.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MDoubleArray.h>
#include <maya/MEulerRotation.h>
#include <maya/MEvent.h>
#include <maya/MFloatMatrix.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFnCamera.h>
#include <maya/MFnDoubleArrayData.h>
#include <maya/MFnDoubleIndexedComponent.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNurbsSurface.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFrameContext.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MItMeshVertex.h>
#include <maya/MItSelectionList.h>
#include <maya/MMatrix.h>
#include <maya/MMeshIntersector.h>
#include <maya/MPointArray.h>
#include <maya/MPxContext.h>
#include <maya/MPxContextCommand.h>
#include <maya/MPxToolCommand.h>
#include <maya/MSelectionList.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MStatus.h>
#include <maya/MSyntax.h>
#include <maya/MThreadUtils.h>
#include <maya/MToolsInfo.h>
#include <maya/MUIDrawManager.h>
#include <maya/MUintArray.h>
#include <maya/MUserEventMessage.h>

#include <algorithm>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <span>
#include <sstream>
#include <iomanip>

#define CHECK_MSTATUS_AND_RETURN_SILENT(status) \
    if (status != MStatus::kSuccess) return MStatus::kSuccess;

typedef float coord_t;
typedef std::array<coord_t, 3> point_t;


struct drawingDeformers {
    MMatrix mat;
    MPoint center;
    MPoint minPt;
    MPoint maxPt;
    MVector up, right;
    double width, height, depth;
};





MColor getASoloColor(
    double val,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    int influenceIndex,
    MColorArray &jointsColors
) {
    if (val == 0) return MColor(0, 0, 0);
    val = (maxSoloColor - minSoloColor) * val + minSoloColor;
    MColor soloColor;
    if (soloColorTypeVal == 0) {  // black and white
        soloColor = MColor(val, val, val);
    } else if (soloColorTypeVal == 1) {  // lava
        val *= 2;
        if (val > 1)
            soloColor = MColor(val, (val - 1), 0);
        else
            soloColor = MColor(val, 0, 0);
    } else {  // influence
        soloColor = val * jointsColors[influenceIndex];
    }
    return soloColor;
}


ModifierCommands getCommandIndexModifiers(
    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier  // Constant
) {
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // unlockVertices
    ModifierCommands theCommandIndex = commandIndex;

    if (commandIndex == ModifierCommands::Add){
        if (modifierNoneShiftControl == smoothModifier){
            theCommandIndex = ModifierCommands::Smooth;
        }
        else if (modifierNoneShiftControl == removeModifier){
            theCommandIndex = ModifierCommands::Remove;
        }
    }
    else if (commandIndex == ModifierCommands::LockVertices){
        if (modifierNoneShiftControl == ModifierKeys::Shift){
            theCommandIndex = ModifierCommands::UnlockVertices;
        }
    }
    if (modifierNoneShiftControl == ModifierKeys::ControlShift){
        theCommandIndex = ModifierCommands::Sharpen;
    }

    return theCommandIndex;
}

void getColorWithMirror(
    int vertexIndex,
    int influenceIndex,
    float valueBase,
    float valueMirror,
    int nbJoints,

    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,

    MColorArray &multiEditColors,
    MColorArray &multiCurrentColors,
    MColorArray &soloEditColors,
    MColorArray &soloCurrentColors,
    MColorArray &jointsColors,
    MColor &lockVertColor,
    MColor &lockJntColor,
    MColor &multColor,
    MColor &soloColor,

    MIntArray &lockVertices,
    MIntArray &lockJoints,
    MIntArray &mirrorInfluences,
    MDoubleArray &skinWeightList,

    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier  // Constant

) {
    MColor white(1, 1, 1, 1);
    MColor black(0, 0, 0, 1);
    ModifierCommands theCommandIndex = getCommandIndexModifiers(commandIndex, modifierNoneShiftControl, smoothModifier, removeModifier);

    float sumValue = valueBase + valueMirror;
    sumValue = std::min(float(1.0), sumValue);
    float biggestValue = std::max(valueBase, valueMirror);

    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        if (theCommandIndex == ModifierCommands::LockVertices) {  // lock verts if not already locked
            soloColor = lockVertColor;
            multColor = lockVertColor;
        } else {  // unlock verts
            multColor = multiCurrentColors[vertexIndex];
            soloColor = soloCurrentColors[vertexIndex];
        }
    } else if (!lockVertices[vertexIndex]) {
        MColor currentColor = multiCurrentColors[vertexIndex];
        int influenceMirrorColorIndex = mirrorInfluences[influenceIndex];
        MColor jntColor = jointsColors[influenceIndex];
        MColor jntMirrorColor = jointsColors[influenceMirrorColorIndex];
        if (lockJoints[influenceIndex] == 1) jntColor = lockJntColor;
        if (lockJoints[influenceMirrorColorIndex] == 1) jntMirrorColor = lockJntColor;
        // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
        // UnLockVertices

        if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen) {
            soloColor = biggestValue * white + (1.0 - biggestValue) * soloCurrentColors[vertexIndex];
            multColor = biggestValue * white + (1.0 - biggestValue) * multiCurrentColors[vertexIndex];
        } else {
            double newW = 0.0;
            int ind_swl = vertexIndex * nbJoints + influenceIndex;
            if (ind_swl < skinWeightList.length())
                newW = skinWeightList[ind_swl];
            double newWMirror = 0.0;
            int ind_swlM = vertexIndex * nbJoints + influenceMirrorColorIndex;
            if (ind_swlM < skinWeightList.length())
                newWMirror = skinWeightList[ind_swlM];
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
            soloColor = getASoloColor(
                newW,
                maxSoloColor,
                minSoloColor,
                soloColorTypeVal,
                influenceIndex,
                jointsColors
            );


        }
    }
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
        switch (theCommandIndex) {
            case ModifierCommands::LockVertices:
                baseColor = lockVertColor;
                break;
            case ModifierCommands::Remove:
                baseColor = black;
                break;
            case ModifierCommands::UnlockVertices:
                baseColor = white;
                break;
            case ModifierCommands::Smooth:
                baseColor = white;
                break;
            case ModifierCommands::Sharpen:
                baseColor = white;
                break;
            default:
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

coord_t distance_sq(const point_t& a, const point_t& b) {
    coord_t x = a[0] - b[0];
    coord_t y = a[1] - b[1];
    coord_t z = a[2] - b[2];
    return x * x + y * y + z * z;
}

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
    if (AllHitPoints.length() == 0) return;

    // set of visited vertices
    std::unordered_set<int> vertsVisited, vertsWithinDistance;

    for (const auto& element : dicVertsDist) {
        vertsVisited.insert(element.first);
    }
    vertsWithinDistance = vertsVisited;

    // start of growth
    std::unordered_set<int> borderOfGrowth;
    borderOfGrowth = vertsVisited;

    // make the std vector points for faster sorting
    std::vector<point_t> points;
    for (auto hitPt : AllHitPoints) {
        point_t tmp = {hitPt.x, hitPt.y, hitPt.z};
        points.push_back(tmp);
    }

    bool keepGoing = true;
    while (keepGoing) {
        keepGoing = false;

        // grow the vertices
        //std::vector<int> setOfVertsGrow;
        std::unordered_set<int> setOfVertsGrow;
        for (const int &vertexIndex : borderOfGrowth) {
            std::vector<int> ttt = getSurroundingVerticesPerVert(
                vertexIndex, perVertexVerticesSetFLAT, perVertexVerticesSetINDEX
            );
            setOfVertsGrow.insert(ttt.begin(), ttt.end());
        }

        // get the vertices that are grown
        std::vector<int> verticesontheborder;
        std::set_difference(
            setOfVertsGrow.begin(), setOfVertsGrow.end(),
            vertsVisited.begin(), vertsVisited.end(),
            std::inserter(verticesontheborder, verticesontheborder.end())
        );

        std::unordered_set<int> foundGrowVertsWithinDistance;

        // for all vertices grown
        for (int vertexBorder : verticesontheborder) {
            // First check the normal
            if (!coverageVal) {
                MVector vertexBorderNormal = verticesNormals[vertexBorder];
                double multVal = worldVector * vertexBorderNormal;
                if (multVal > 0.0) continue;
            }
            float closestDist = -1;
            // find the closestDistance and closest Vertex from visited vertices
            point_t thisPoint;
            thisPoint[0] = mayaRawPoints[vertexBorder * 3];
            thisPoint[1] = mayaRawPoints[vertexBorder * 3 + 1];
            thisPoint[2] = mayaRawPoints[vertexBorder * 3 + 2];

            auto glambda = [&thisPoint](const point_t &a, const point_t &b) {
                float aRes = distance_sq(a, thisPoint);
                float bRes = distance_sq(b, thisPoint);
                return aRes < bRes;
            };
            std::partial_sort(points.begin(), points.begin() + 1, points.end(), glambda);
            auto closestPoint = points.front();
            closestDist = std::sqrt(distance_sq(closestPoint, thisPoint));
            // get the new distance between the closest visited vertex and the grow vertex
            if (closestDist <= sizeVal) {  // if in radius of the brush
                // we found a vertex in the radius
                // now add to the visited and add the distance to the dictionnary
                keepGoing = true;
                foundGrowVertsWithinDistance.insert(vertexBorder);
                auto ret = dicVertsDist.insert(std::make_pair(vertexBorder, closestDist));
                if (!ret.second) ret.first->second = std::min(closestDist, ret.first->second);
            }
        }
        // this vertices has been visited, let's not consider them anymore

        vertsVisited.insert(verticesontheborder.begin(), verticesontheborder.end());
        vertsWithinDistance.insert(foundGrowVertsWithinDistance.begin(), foundGrowVertsWithinDistance.end());
        borderOfGrowth = foundGrowVertsWithinDistance;
    }
}

void copyToFloatMatrix(const MMatrix& src, MFloatMatrix& dst) {
    for (unsigned i = 0; i < 4; ++i){
        for (unsigned j = 0; j < 4; ++j){
            dst[i][j] = (float)src[i][j];
        }
    }
}

template <typename T=int>
class FlatCounts {
private:
    std::vector<size_t> offsets;  // actually offsets
    std::vector<T> values;

public:
    // Setter from counts/vals pair of vectors
    void set(const std::vector<T> &inCounts, const std::vector<T> &inVals){
        offsets.clear();
        values.clear();
        values = inVals;

        offsets.resize(inCounts.size() + 1);
        offsets[0] = 0;
        size_t i = 1, v = 0;
        for (const auto& c : inCounts) {
            v += c;
            offsets[i++] = v;
        }
    }

    // Setter from vector-of-vectors
    void set(const std::vector<std::vector<T>> &inVals){
        offsets.clear();
        values.clear();
        offsets.reserve(inVals.size() + 1);
        offsets.push_back(0);
        size_t i = 1, v = 0;
        for (const auto& sub : inVals) {
            v += sub.size();
            offsets[i++] = v;
            values.insert(values.end(), sub.begin(), sub.end());
        }
    }

    // Setter from unordered_map of vectors
    template <typename K>
    void set(const std::unordered_map<K, std::vector<T>> &inVals){
        offsets.clear();
        values.clear();

        const auto maxit = std::max_element(inVals.begin(), inVals.end());
        K maxkey = maxit->first;

        offsets.reserve(maxkey + 1);
        offsets.push_back(0);
        size_t offset = 0;
        for (size_t i=0; i<maxkey; ++i){
            auto search = inVals.find(i);
            if (search != inVals.end()){
                offset += search->second.size();
                values.insert(values.end(), search->second.begin(), search->second.end());
            }
            offsets.push_back(offset);
        }
    }

    size_t length() const{
        return offsets.size() - 1;
    }

    const std::span<const T> operator [](size_t i) const {
        return std::span<const T>(values.begin() + offsets[i], values.begin() + offsets[i + 1]);
    }
};

template <typename T=int, size_t C = 2>
class FlatChunks {
private:
    std::vector<T> values;

public:
    void set(const std::vector<std::array<int, C>> &invals){
        values.reserve(invals.size() * C);
        for (const auto & ev : invals){
            for (const auto & v : ev){
                values.push_back(v);
            }
        }
    }

    void set(const T* invals, size_t length){
        values.clear();
        values.insert(values.end(), invals, invals + (length * C));
    }

    size_t length() const{
        return values.size() / 2;
    }

    const std::span<const T> operator [](size_t i) const {
        return std::span<const T>(values.begin() + (C * i), values.begin() + (C * (i + 1)));
    }
};

template <typename T=int, size_t C = 3>
class DoubleChunks {
private:
    std::vector<size_t> offsets;
    std::vector<T> values;

public:
    void set(const std::vector<std::vector<std::array<T, C>>> &perEdgeVertices){
        values.clear();
        offsets.clear();
        offsets.push_back(0);
        size_t offset = 0;
        values.reserve(perEdgeVertices.size() * C);
        for (const auto & eev : perEdgeVertices){
            offset += eev.size();
            offsets.push_back(offset);
            for (const auto & ev : eev){
                for (const auto & v : ev){
                    values.push_back(v);
                }
            }
        }
    }

    size_t length() const{
        return offsets.size() - 1;
    }

    size_t length2(size_t i) const {
        return offsets[i + 1] - offsets[i];
    }

    const std::span<const T> operator ()(size_t i, size_t j) const {
        return std::span<const T>(values.begin() + offsets[i] + (C * j), values.begin() + offsets[i] + (C * (j + 1)));
    }
};

void  getAllConnections(
    MFnMesh &meshFn,  // For getting the mesh data
    MDagPath &meshDag,  // So I can get the "maya canonical" edges

    FlatCounts<int> &pvFaces,  // x[vertIdx] -> [list-of-faceIdxs]
    FlatCounts<int> &pvEdges,  // x[vertIdx] -> [list-of-edgeIdxs]
    FlatCounts<int> &pvVerts,  // x[vertIdx] -> [list-of-vertIdxs that share a face with the input]
    FlatCounts<int> &pfVerts,  // x[faceIdx] -> [list-of-vertIdxs]
    FlatChunks<int> &peVerts,  // x[edgeIdx] -> [pair-of-vertIdxs]
    DoubleChunks<int> &pftVerts  // x(face, triIdx) -> [list_of_vertIdxs]
){
    // Get the data from the mesh and dag path
    MIntArray counts, flatFaces, triangleCounts, triangleVertices;
    meshFn.getVertices(counts, flatFaces);
    meshFn.getTriangles(triangleCounts, triangleVertices);
    int numVertices = meshFn.numVertices();
    int numFaces = meshFn.numPolygons();
    int numEdges = meshFn.numEdges();

    std::vector<std::vector<int>> perVertexFaces;
    std::vector<std::vector<int>> perVertexEdges;
    std::vector<std::vector<int>> perVertexVertices;
    std::vector<std::vector<int>> perFaceVertices;
    std::vector<std::array<int, 2>> perEdgeVertices;
    std::vector<std::vector<std::array<int, 3>>> perFaceTriangleVertices;

    // Reset all the output vectors
    perVertexFaces.resize(numVertices);
    perVertexEdges.resize(numVertices);
    perVertexVertices.resize(numVertices);
    perFaceVertices.resize(numFaces);
    perFaceTriangleVertices.resize(numFaces);
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
        for (int faceIdx: perVertexFaces[vertIdx]){
            std::vector<int> &faceVerts = perFaceVertices[faceIdx];
            toAdd.insert(toAdd.end(), faceVerts.begin(), faceVerts.end());
        }
        // For such a short vec, sorting then erasing is the fastest
        std::sort(toAdd.begin(), toAdd.end());
        toAdd.erase(std::unique(toAdd.begin(), toAdd.end()), toAdd.end());
    }

    // Put all the data in flattened arrays
    pvFaces.set(perVertexFaces);
    pvEdges.set(perVertexEdges);
    pvVerts.set(perVertexVertices);
    pfVerts.set(perFaceVertices);
    peVerts.set(perEdgeVertices);
    pftVerts.set(perFaceTriangleVertices);
}

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

bool getMirrorHit(
    int paintMirror,
    MFloatPoint &origHitPoint,
    MMeshIntersector &intersectorOrigShape,
    MMeshIntersector &intersector,
    double mirrorMinDist,
    std::vector<std::vector<MIntArray>> &perFaceTriangleVertices,
    const float *mayaRawPoints,
    MFloatMatrix &inclusiveMatrix,
    MFloatPoint &centerOfBrush,

    int &faceHit,
    MFloatPoint &hitPoint
) {
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
        MPoint pointToMirror = MPoint(origHitPoint);
        MPoint mirrorPoint = pointToMirror * mirrorMatrix;

        stat = intersectorOrigShape.getClosestPoint(mirrorPoint, pointInfo, mirrorMinDist);
        if (MS::kSuccess != stat) return false;

        faceHit = pointInfo.faceIndex();
        int hitTriangle = pointInfo.triangleIndex();
        float hitBary1, hitBary2;
        pointInfo.getBarycentricCoords(hitBary1, hitBary2);

        MIntArray triangle = perFaceTriangleVertices[faceHit][hitTriangle];

        float hitBary3 = (1 - hitBary1 - hitBary2);
        float x = mayaRawPoints[triangle[0] * 3] * hitBary1 +
                  mayaRawPoints[triangle[1] * 3] * hitBary2 +
                  mayaRawPoints[triangle[2] * 3] * hitBary3;
        float y = mayaRawPoints[triangle[0] * 3 + 1] * hitBary1 +
                  mayaRawPoints[triangle[1] * 3 + 1] * hitBary2 +
                  mayaRawPoints[triangle[2] * 3 + 1] * hitBary3;
        float z = mayaRawPoints[triangle[0] * 3 + 2] * hitBary1 +
                  mayaRawPoints[triangle[1] * 3 + 2] * hitBary2 +
                  mayaRawPoints[triangle[2] * 3 + 2] * hitBary3;
        hitPoint = MFloatPoint(x, y, z) * inclusiveMatrix;
    } else {
        MPoint mirrorPoint = MPoint(centerOfBrush) * mirrorMatrix;
        stat = intersector.getClosestPoint(mirrorPoint, pointInfo, mirrorMinDist);
        if (MS::kSuccess != stat) return false;

        faceHit = pointInfo.faceIndex();
        hitPoint = MFloatPoint(mirrorPoint);
    }
    return true;
}

bool computeHit(
    short screenPixelX,
    short screenPixelY,
    bool getNormal,
    M3dView &view,
    MPoint &worldPoint,
    MVector &worldVector,
    MFnMesh &meshFn,
    MMeshIsectAccelParams &accelParams,
    float pressDistance,
    int paintMirror,
    std::vector<std::vector<MIntArray>> &perFaceTriangleVertices,
    const float *mayaOrigRawPoints,
    MFloatPoint &origHitPoint,
    MVector &normalVector,

    int &faceHit,
    MFloatPoint &hitPoint
){
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
                                   9999, false, &accelParams, hitPoint, &pressDistance,
                                   &faceHit, &hitTriangle, &hitBary1, &hitBary2, 0.0001f, &stat);

    if (!foundIntersect) return false;

    if (paintMirror > 0 && paintMirror < 4) {  // if we compute the orig
        MIntArray triangle = perFaceTriangleVertices[faceHit][hitTriangle];
        float hitBary3 = (1 - hitBary1 - hitBary2);
        float x = mayaOrigRawPoints[triangle[0] * 3] * hitBary1 +
                  mayaOrigRawPoints[triangle[1] * 3] * hitBary2 +
                  mayaOrigRawPoints[triangle[2] * 3] * hitBary3;
        float y = mayaOrigRawPoints[triangle[0] * 3 + 1] * hitBary1 +
                  mayaOrigRawPoints[triangle[1] * 3 + 1] * hitBary2 +
                  mayaOrigRawPoints[triangle[2] * 3 + 1] * hitBary3;
        float z = mayaOrigRawPoints[triangle[0] * 3 + 2] * hitBary1 +
                  mayaOrigRawPoints[triangle[1] * 3 + 2] * hitBary2 +
                  mayaOrigRawPoints[triangle[2] * 3 + 2] * hitBary3;
        origHitPoint = MFloatPoint(x, y, z);
    }

    // ----------- get normal for display ---------------------
    if (getNormal){
        meshFn.getPolygonNormal(faceHit, normalVector, MSpace::kWorld);
    }
    return true;
}

std::vector<int> getSurroundingVerticesPerFace(
    int vertexIndex,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX
) {
    auto first = perFaceVerticesSetFLAT.begin() + perFaceVerticesSetINDEX[vertexIndex];
    auto last = perFaceVerticesSetFLAT.begin() + perFaceVerticesSetINDEX[vertexIndex + (int)1];
    std::vector<int> newVec(first, last);
    return newVec;
}

bool expandHit(
    int faceHit,
    const float *mayaRawPoints,
    double sizeVal,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX,

    MFloatPoint &hitPoint,
    std::unordered_map<int, float> &dicVertsDist
) {
    // ----------- compute the vertices around ---------------------
    std::vector<int> verticesSet = getSurroundingVerticesPerFace(faceHit, perFaceVerticesSetFLAT, perFaceVerticesSetINDEX);
    bool foundHit = false;
    for (int ptIndex : verticesSet) {
        MFloatPoint posPoint(
            mayaRawPoints[ptIndex * 3],
            mayaRawPoints[ptIndex * 3 + 1],
            mayaRawPoints[ptIndex * 3 + 2]
        );
        float dist = posPoint.distanceTo(hitPoint);
        if (dist <= sizeVal) {
            foundHit = true;
            auto ret = dicVertsDist.insert(std::make_pair(ptIndex, dist));
            if (!ret.second) ret.first->second = std::min(dist, ret.first->second);
        }
    }
    return foundHit;
}

void addBrushShapeFallof(
    double strengthVal,
    double smoothStrengthVal,
    ModifierKeys modifierNoneShiftControl,
    ModifierCommands commandIndex,
    bool fractionOversamplingVal,
    int oversamplingVal,
    double sizeVal,
    int curveVal,

    std::unordered_map<int, float> &dicVertsDist
) {
    double valueStrength = strengthVal;
    if (modifierNoneShiftControl == ModifierKeys::ControlShift || commandIndex == ModifierCommands::Smooth) {
        valueStrength = smoothStrengthVal;  // smooth always we use the smooth value different of
                                            // the regular value
    }

    if (fractionOversamplingVal) valueStrength /= oversamplingVal;

    for (auto &element : dicVertsDist) {
        float value = 1.0 - (element.second / sizeVal);
        value = (float)getFalloffValue(curveVal, value, valueStrength);

        element.second = value;
    }
}

void mergeMirrorArray(
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,
    std::unordered_map<int, float> &valuesBase,
    std::unordered_map<int, float> &valuesMirrored
) {
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

MStatus setAverageWeight(std::vector<int>& verticesAround, int currentVertex, int indexCurrVert,
                         int nbJoints, MIntArray& lockJoints, MDoubleArray& fullWeightArray,
                         MDoubleArray& theWeights, double strengthVal) {
    MStatus stat;
    int sizeVertices = verticesAround.size();
    unsigned int jnt, posi;

    MDoubleArray sumWeigths(nbJoints, 0.0);
    // compute sum weights
    for (int vertIndex : verticesAround) {
        for (jnt = 0; jnt < nbJoints; jnt++) {
            posi = vertIndex * nbJoints + jnt;
            sumWeigths[jnt] += fullWeightArray[posi];
        }
    }
    double totalBaseVtxLock = 0.0;
    double totalVtxUnlock = 0.0;

    for (jnt = 0; jnt < nbJoints; jnt++) {
        // get if jnt is locked
        bool isLockJnt = lockJoints[jnt] == 1;
        int posi = currentVertex * nbJoints + jnt;
        // get currentWeight of currentVtx
        double currentW = fullWeightArray[posi];

        sumWeigths[jnt] /= sizeVertices;
        sumWeigths[jnt] = strengthVal * sumWeigths[jnt] + (1.0 - strengthVal) * currentW;  // add with strength
        double targetW = sumWeigths[jnt];

        // sum it all
        if (!isLockJnt) {
            totalVtxUnlock += targetW;
        } else {
            totalBaseVtxLock += currentW;
        }
    }
    // setting part ---------------
    double normalizedValueAvailable = 1.0 - totalBaseVtxLock;

    if (normalizedValueAvailable > 0.0 && totalVtxUnlock > 0.0) {  // we have room to set weights
        double mult = normalizedValueAvailable / totalVtxUnlock;
        for (jnt = 0; jnt < nbJoints; jnt++) {
            bool isLockJnt = lockJoints[jnt] == 1;
            int posiToSet = indexCurrVert * nbJoints + jnt;
            int posi = currentVertex * nbJoints + jnt;

            double currentW = fullWeightArray[posi];
            double targetW = sumWeigths[jnt];

            if (isLockJnt) {
                theWeights[posiToSet] = currentW;
            } else {
                targetW *= mult;  // normalement divide par 1, sauf cas lock joints
                theWeights[posiToSet] = targetW;
            }
        }
    } else {  // normalize problem let's revert
        for (jnt = 0; jnt < nbJoints; jnt++) {
            int posiToSet = indexCurrVert * nbJoints + jnt;
            int posi = currentVertex * nbJoints + jnt;

            double currentW = fullWeightArray[posi];
            theWeights[posiToSet] = currentW;  // set the base Weight
        }
    }
    return MS::kSuccess;
}

MStatus editArray(ModifierCommands command, int influence, int nbJoints, MIntArray& lockJoints,
                  MDoubleArray& fullWeightArray, std::map<int, double>& valuesToSet,
                  MDoubleArray& theWeights, bool normalize, double mutliplier) {
    MStatus stat;
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // UnLockVertices
    //
    if (lockJoints.length() < nbJoints) {
        MGlobal::displayInfo(MString("-> editArray FAILED | nbJoints ") + nbJoints +
                             MString(" | lockJoints ") + lockJoints.length());
        return MStatus::kFailure;
    }
    if (command == ModifierCommands::Sharpen) {
        int i = 0;
        for (const auto& elem : valuesToSet) {
            int theVert = elem.first;
            double theVal = mutliplier * elem.second + 1.0;
            double substract = theVal / nbJoints;
            MDoubleArray producedWeigths(nbJoints, 0.0);
            double totalBaseVtxLock = 0.0;
            double totalVtxUnlock = 0.0;
            for (int j = 0; j < nbJoints; ++j) {
                // check the zero val ----------
                double currentW = fullWeightArray[theVert * nbJoints + j];
                double targetW = (currentW * theVal) - substract;
                targetW = std::max(0.0, std::min(targetW, 1.0));  // clamp
                producedWeigths.set(targetW, j);

                if (lockJoints[j] == 0) {  // unlock
                    totalVtxUnlock += targetW;
                } else {
                    totalBaseVtxLock += currentW;
                }
            }
            // now normalize for lockJoints
            double normalizedValueAvailable = 1.0 - totalBaseVtxLock;
            if (normalizedValueAvailable > 0.0 &&
                totalVtxUnlock > 0.0) {  // we have room to set weights
                double mult = normalizedValueAvailable / totalVtxUnlock;
                for (unsigned int j = 0; j < nbJoints; ++j) {
                    double currentW = fullWeightArray[theVert * nbJoints + j];
                    double targetW = producedWeigths[j];
                    if (lockJoints[j] == 0) {  // unlock
                        targetW *= mult;       // normalement divide par 1, sauf cas lock joints
                        theWeights[i * nbJoints + j] = targetW;
                    } else {
                        theWeights[i * nbJoints + j] = currentW;
                    }
                }
            } else {
                for (unsigned int j = 0; j < nbJoints; ++j) {
                    theWeights[i * nbJoints + j] = fullWeightArray[theVert * nbJoints + j];
                }
            }
            i++;
        }
    } else {
        // do the command --------------------------
        int i = -1;  // i is a short index instead of theVert
        for (const auto& elem : valuesToSet) {
            i++;
            int theVert = elem.first;
            double theVal = mutliplier * elem.second;
            // get the sum of weights

            double sumUnlockWeights = 0.0;
            for (int jnt = 0; jnt < nbJoints; ++jnt) {
                int indexArray_theWeight = i * nbJoints + jnt;
                int indexArray_fullWeightArray = theVert * nbJoints + jnt;

                if (indexArray_theWeight > theWeights.length()) {
                    MGlobal::displayInfo(
                        MString(
                            "-> editArray FAILED | indexArray_theWeight  > theWeights.length()") +
                        indexArray_theWeight + MString(" > ") + theWeights.length());
                    return MStatus::kFailure;
                }
                if (indexArray_fullWeightArray > fullWeightArray.length()) {
                    MGlobal::displayInfo(MString("-> editArray FAILED | indexArray_fullWeightArray "
                                                 " > fullWeightArray.length()") +
                                         indexArray_fullWeightArray + MString(" > ") +
                                         fullWeightArray.length());
                    return MStatus::kFailure;
                }

                if (lockJoints[jnt] == 0) {  // not locked
                    sumUnlockWeights += fullWeightArray[indexArray_fullWeightArray];
                }
                theWeights[indexArray_theWeight] =
                    fullWeightArray[indexArray_fullWeightArray];  // preset array
            }
            double currentW = fullWeightArray[theVert * nbJoints + influence];

            if (((command == ModifierCommands::Remove) || (command == ModifierCommands::Absolute)) &&
                (currentW > (sumUnlockWeights - .0001))) {  // value is 1(max) we cant do anything
                continue;                                   // we pass to next vertex
            }

            double newW = currentW;
            if (command == ModifierCommands::Add)
                newW += theVal;
            else if (command == ModifierCommands::Remove)
                newW -= theVal;
            else if (command == ModifierCommands::AddPercent)
                newW += theVal * newW;
            else if (command == ModifierCommands::Absolute)
                newW = theVal;

            newW = std::max(0.0, std::min(newW, sumUnlockWeights));  // clamp

            double newRest = sumUnlockWeights - newW;
            double oldRest = sumUnlockWeights - currentW;
            double div = sumUnlockWeights;

            if (newRest != 0.0) div = oldRest / newRest;  // produit en croix

            // do the locks !!
            double sum = 0.0;
            for (int jnt = 0; jnt < nbJoints; ++jnt) {
                if (lockJoints[jnt] == 1) {
                    continue;
                }
                // check the zero val ----------
                double weightValue = fullWeightArray[theVert * nbJoints + jnt];
                if (jnt == influence) {
                    weightValue = newW;
                } else {
                    if (newW == sumUnlockWeights) {
                        weightValue = 0.0;
                    } else {
                        weightValue /= div;
                    }
                }
                if (normalize) {
                    weightValue = std::max(0.0, std::min(weightValue, sumUnlockWeights));  // clamp
                }
                sum += weightValue;
                theWeights[i * nbJoints + jnt] = weightValue;
            }

            if ((sum == 0) ||
                (sum <
                 0.5 * sumUnlockWeights)) {  // zero problem revert weights ----------------------
                for (int jnt = 0; jnt < nbJoints; ++jnt) {
                    theWeights[i * nbJoints + jnt] = fullWeightArray[theVert * nbJoints + jnt];
                }
            } else if (normalize && (sum != sumUnlockWeights)) {  // normalize ---------------
                for (int jnt = 0; jnt < nbJoints; ++jnt)
                    if (lockJoints[jnt] == 0) {
                        theWeights[i * nbJoints + jnt] /= sum;               // to 1
                        theWeights[i * nbJoints + jnt] *= sumUnlockWeights;  // to sum weights
                    }
            }
        }
    }
    return stat;
}

MStatus editArrayMirror(ModifierCommands command, int influence, int influenceMirror, int nbJoints,
                        MIntArray& lockJoints, MDoubleArray& fullWeightArray,
                        std::map<int, std::pair<float, float>>& valuesToSetMirror,
                        MDoubleArray& theWeights, bool normalize, double mutliplier) {
    MStatus stat;
    // 0 Add - 1 Remove - 2 AddPercent - 3 Absolute - 4 Smooth - 5 Sharpen - 6 LockVertices - 7
    // UnLockVertices
    //
    if (lockJoints.length() < nbJoints) {
        MGlobal::displayInfo(MString("-> editArrayMirror FAILED | nbJoints ") + nbJoints +
                             MString(" | lockJoints ") + lockJoints.length());
        return MStatus::kFailure;
    }
    if (command == ModifierCommands::Sharpen) {
        int i = 0;
        for (const auto& elem : valuesToSetMirror) {
            int theVert = elem.first;
            float valueBase = elem.second.first;
            float valueMirror = elem.second.second;

            float biggestValue = std::max(valueBase, valueMirror);

            double theVal = mutliplier * (double)biggestValue + 1.0;
            double substract = theVal / nbJoints;

            MDoubleArray producedWeigths(nbJoints, 0.0);
            double totalBaseVtxLock = 0.0;
            double totalVtxUnlock = 0.0;
            for (int j = 0; j < nbJoints; ++j) {
                double currentW = fullWeightArray[theVert * nbJoints + j];
                double targetW = (currentW * theVal) - substract;
                targetW = std::max(0.0, std::min(targetW, 1.0));  // clamp
                producedWeigths.set(targetW, j);
                if (lockJoints[j] == 0) {  // unlock
                    totalVtxUnlock += targetW;
                } else {
                    totalBaseVtxLock += currentW;
                }
            }
            // now normalize
            double normalizedValueAvailable = 1.0 - totalBaseVtxLock;
            if (normalizedValueAvailable > 0.0 &&
                totalVtxUnlock > 0.0) {  // we have room to set weights
                double mult = normalizedValueAvailable / totalVtxUnlock;
                for (unsigned int j = 0; j < nbJoints; ++j) {
                    double currentW = fullWeightArray[theVert * nbJoints + j];
                    double targetW = producedWeigths[j];
                    if (lockJoints[j] == 0) {  // unlock
                        targetW *= mult;       // normalement divide par 1, sauf cas lock joints
                        theWeights[i * nbJoints + j] = targetW;
                    } else {
                        theWeights[i * nbJoints + j] = currentW;
                    }
                }
            } else {
                for (unsigned int j = 0; j < nbJoints; ++j) {
                    theWeights[i * nbJoints + j] = fullWeightArray[theVert * nbJoints + j];
                }
            }
            i++;
        }
    } else {
        // do the other command --------------------------
        int i = -1;  // i is a short index instead of theVert
        for (const auto& elem : valuesToSetMirror) {
            i++;
            int theVert = elem.first;
            double valueBase = mutliplier * (double)elem.second.first;
            double valueMirror = mutliplier * (double)elem.second.second;

            if (influenceMirror == influence) {
                valueBase = std::max(valueBase, valueMirror);
                valueMirror = 0.0;
            }

            double sumUnlockWeights = 0.0;
            for (int jnt = 0; jnt < nbJoints; ++jnt) {
                int indexArray_theWeight = i * nbJoints + jnt;
                int indexArray_fullWeightArray = theVert * nbJoints + jnt;
                if (lockJoints[jnt] == 0) {  // not locked
                    sumUnlockWeights += fullWeightArray[indexArray_fullWeightArray];
                }
                theWeights[indexArray_theWeight] =
                    fullWeightArray[indexArray_fullWeightArray];  // preset array
            }

            double currentW = fullWeightArray[theVert * nbJoints + influence];
            double currentWMirror = fullWeightArray[theVert * nbJoints + influenceMirror];
            // 1 Remove 3 Absolute
            double newW = currentW;
            double newWMirror = currentWMirror;
            double sumNewWs = newW + newWMirror;

            if (command == ModifierCommands::Add) {
                newW = std::min(1.0, newW + valueBase);
                newWMirror = std::min(1.0, newWMirror + valueMirror);
                sumNewWs = newW + newWMirror;

                if (sumNewWs > 1.0) {
                    newW /= sumNewWs;
                    newWMirror /= sumNewWs;
                }
            } else if (command == ModifierCommands::Remove) {
                newW = std::max(0.0, newW - valueBase);
                newWMirror = std::max(0.0, newWMirror - valueMirror);
            } else if (command == ModifierCommands::AddPercent) {
                newW += valueBase * newW;
                newW = std::min(1.0, newW);
                newWMirror += valueMirror * newWMirror;
                newWMirror = std::min(1.0, newWMirror);
                sumNewWs = newW + newWMirror;
                if (sumNewWs > 1.0) {
                    newW /= sumNewWs;
                    newWMirror /= sumNewWs;
                }
            } else if (command == ModifierCommands::Absolute) {
                newW = valueBase;
                newWMirror = valueMirror;
            }
            newW = std::min(newW, sumUnlockWeights);              // clamp to max sumUnlockWeights
            newWMirror = std::min(newWMirror, sumUnlockWeights);  // clamp to max sumUnlockWeights

            double newRest = sumUnlockWeights - newW - newWMirror;
            double oldRest = sumUnlockWeights - currentW - currentWMirror;
            double div = sumUnlockWeights;

            if (newRest != 0.0) {  // produit en croix
                div = oldRest / newRest;
            }
            // do the locks !!
            double sum = 0.0;
            for (int jnt = 0; jnt < nbJoints; ++jnt) {
                if (lockJoints[jnt] == 1) {
                    continue;
                }
                // check the zero val ----------
                double weightValue = fullWeightArray[theVert * nbJoints + jnt];
                if (jnt == influence) {
                    weightValue = newW;
                } else if (jnt == influenceMirror) {
                    weightValue = newWMirror;
                } else {
                    if ((newW + newWMirror) == sumUnlockWeights) {
                        weightValue = 0.0;
                    } else {
                        weightValue /= div;
                    }
                }
                if (normalize) {
                    weightValue = std::max(0.0, std::min(weightValue, sumUnlockWeights));  // clamp
                }
                sum += weightValue;
                theWeights[i * nbJoints + jnt] = weightValue;
            }
            if ((sum == 0) || (sum < 0.5 * sumUnlockWeights)) {  // zero problem revert weights
                for (int jnt = 0; jnt < nbJoints; ++jnt) {
                    theWeights[i * nbJoints + jnt] = fullWeightArray[theVert * nbJoints + jnt];
                }
            } else if (normalize && (sum != sumUnlockWeights)) {  // normalize
                for (int jnt = 0; jnt < nbJoints; ++jnt)
                    if (lockJoints[jnt] == 0) {
                        theWeights[i * nbJoints + jnt] /= sum;               // to 1
                        theWeights[i * nbJoints + jnt] *= sumUnlockWeights;  // to sum weights
                    }
            }
        }
    }
    return stat;
}

MStatus transferPointNurbsToMesh(MFnMesh& msh, MFnNurbsSurface& nurbsFn) {
    MStatus stat = MS::kSuccess;
    MPlug mshPnts = msh.findPlug("pnts", false, &stat);
    MPointArray allpts;

    bool VIsPeriodic_ = nurbsFn.formInV() == MFnNurbsSurface::kPeriodic;
    bool UIsPeriodic_ = nurbsFn.formInU() == MFnNurbsSurface::kPeriodic;
    if (VIsPeriodic_ || UIsPeriodic_) {
        int numCVsInV_ = nurbsFn.numCVsInV();
        int numCVsInU_ = nurbsFn.numCVsInU();
        int UDeg_ = nurbsFn.degreeU();
        int VDeg_ = nurbsFn.degreeV();
        if (VIsPeriodic_) numCVsInV_ -= VDeg_;
        if (UIsPeriodic_) numCVsInU_ -= UDeg_;
        for (int uIndex = 0; uIndex < numCVsInU_; uIndex++) {
            for (int vIndex = 0; vIndex < numCVsInV_; vIndex++) {
                MPoint pt;
                nurbsFn.getCV(uIndex, vIndex, pt);
                allpts.append(pt);
            }
        }
    } else {
        stat = nurbsFn.getCVs(allpts);
    }
    msh.setPoints(allpts);
    return stat;
}

MStatus refreshPointsNormals(
    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    int numVertices,
    MFnMesh &meshFn,
    MDagPath &meshDag,
    MObject &skinObj
) {
    MStatus status = MStatus::kSuccess;

    if (!skinObj.isNull() && meshDag.isValid(&status)) {
        meshFn.freeCachedIntersectionAccelerator();  // yes ?
        mayaRawPoints = meshFn.getRawPoints(&status);
        rawNormals = meshFn.getRawNormals(&status);
        int rawNormalsLength = sizeof(rawNormals);

#pragma omp parallel for
        for (int vertexInd = 0; vertexInd < numVertices; vertexInd++) {
            int indNormal = verticesNormalsIndices[vertexInd];
            int rawIndNormal = indNormal * 3 + 2;
            if (rawIndNormal < rawNormalsLength) {
                MVector theNormal(
                    rawNormals[indNormal * 3],
                    rawNormals[indNormal * 3 + 1],
                    rawNormals[indNormal * 3 + 2]
                );
                verticesNormals.set(theNormal, vertexInd);
            }
        }
    }
    return status;
}

MStatus applyCommand(
    int influence,
    int nbJoints,
    int smoothRepeat,
    std::unordered_map<int, float> &valuesToSet,
    bool ignoreLockVal,
    MDoubleArray &skinWeightList,
    MIntArray &lockJoints,
    double smoothStrengthVal,
    MIntArray &ignoreLockJoints,
    bool doNormalize,
    MObject &skinObj,
    MDoubleArray &skinWeightsForUndo,
    bool isNurbs,
    MDagPath &meshDag,
    MIntArray &influenceIndices,
    int numCVsInV_,
    MDagPath &nurbsDag,
    bool normalize,
    MFnMesh &meshFn,
    MFnNurbsSurface &nurbsFn,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,
    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    int numVertices,
    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier  // Constant
) {
    MStatus status;
    // we need to sort all of that one way or another ---------------- here it is ------
    std::map<int, double> valuesToSetOrdered(valuesToSet.begin(), valuesToSet.end());

    ModifierCommands theCommandIndex = getCommandIndexModifiers(commandIndex, modifierNoneShiftControl, smoothModifier, removeModifier);

    double multiplier = 1.0;

    if ((theCommandIndex != ModifierCommands::LockVertices) && (theCommandIndex != ModifierCommands::UnlockVertices)) {
        MDoubleArray theWeights((int)nbJoints * valuesToSetOrdered.size(), 0.0); int repeatLimit = 1;
        if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen)
            repeatLimit = smoothRepeat;

        for (int repeat = 0; repeat < repeatLimit; ++repeat) {
            if (theCommandIndex == ModifierCommands::Smooth) {
                int i = 0;
                for (const auto &elem : valuesToSetOrdered) {
                    int theVert = elem.first;
                    double theWeight = elem.second;
                    std::vector<int> vertsAround = getSurroundingVerticesPerVert(theVert, perVertexVerticesSetFLAT, perVertexVerticesSetINDEX);

                    status = setAverageWeight(vertsAround, theVert, i, nbJoints, lockJoints, skinWeightList, theWeights, smoothStrengthVal * theWeight);
                    i++;
                }
            } else {
                if (ignoreLockVal) {
                    status =
                        editArray(theCommandIndex, influence, nbJoints, ignoreLockJoints, skinWeightList, valuesToSetOrdered, theWeights, doNormalize, multiplier);
                } else {
                    if (lockJoints[influence] == 1 && theCommandIndex != ModifierCommands::Sharpen)
                        return status;  //  if locked and it's not sharpen --> do nothing
                    status = editArray(theCommandIndex, influence, nbJoints, lockJoints, skinWeightList, valuesToSetOrdered, theWeights, doNormalize, multiplier);
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
                for (int j = 0; j < nbJoints; ++j) {
                    int ind_swl = theVert * nbJoints + j;
                    if (ind_swl >= skinWeightList.length())
                        skinWeightList.setLength(ind_swl + 1);
                    double val = 0.0;
                    int ind_tw = i * nbJoints + j;
                    if (ind_tw < theWeights.length())
                        val = theWeights[ind_tw];
                    skinWeightList[ind_swl] = val;
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
        skinWeightsForUndo.clear();
        if (!isNurbs) {
            skinFn.setWeights(meshDag, weightsObj, influenceIndices, theWeights, normalize, &skinWeightsForUndo);
        } else {
            MFnDoubleIndexedComponent doubleFn;
            MObject weightsObjNurbs = doubleFn.create(MFn::kSurfaceCVComponent);
            int uVal, vVal;
            for (int vert : objVertices) {

                vVal = (int)vert % (int)numCVsInV_;
                uVal = (int)vert / (int)numCVsInV_;
                doubleFn.addElement(uVal, vVal);
            }
            skinFn.setWeights(nurbsDag, weightsObjNurbs, influenceIndices, theWeights, normalize, &skinWeightsForUndo);
            transferPointNurbsToMesh(meshFn, nurbsFn);  // we transfer the points postions
        }
        // in do press common
        // update values ---------------
        refreshPointsNormals(
            mayaRawPoints,
            rawNormals,
            verticesNormalsIndices,
            verticesNormals,
            numVertices,
            meshFn,
            meshDag,
            skinObj
        );
    }
    return status;
}

void preparePaint(
    bool postSetting,
    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    MIntArray &lockVertices,
    MIntArray &mirrorInfluences,
    int influenceIndex,
    int numVertices,


    int nbJoints,
    int smoothRepeat,
    bool ignoreLockVal,
    MDoubleArray &skinWeightList,
    MIntArray &lockJoints,
    double smoothStrengthVal,
    MIntArray &ignoreLockJoints,
    bool doNormalize,
    MObject &skinObj,
    MDoubleArray &skinWeightsForUndo,
    bool isNurbs,
    MDagPath &meshDag,
    MIntArray &influenceIndices,
    int numCVsInV_,
    MDagPath &nurbsDag,
    bool normalize,
    MFnMesh &meshFn,
    MFnNurbsSurface &nurbsFn,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,
    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier,  // Constant

    std::unordered_map<int, float> &dicVertsDist,
    std::unordered_map<int, float> &dicVertsDistPrevPaint,
    std::vector<float> &intensityValues,
    std::unordered_map<int, float> &skinValToSet,
    std::set<int> &theVerticesPainted,
    bool mirror
) {

    // MGlobal::displayInfo("perform Paint");
    double multiplier = 1.0;
    if (!postSetting && commandIndex != ModifierCommands::Smooth)
        multiplier = .1;  // less applying if dragging paint

    bool isCommandLock =
        ((commandIndex == ModifierCommands::LockVertices) || (commandIndex == ModifierCommands::UnlockVertices))
        && (modifierNoneShiftControl != ModifierKeys::Control);

    auto endOfFind = dicVertsDistPrevPaint.end();
    for (const auto &element : dicVertsDist) {
        int index = element.first;
        float value = element.second * multiplier;
        // check if need to set this color, we store in intensityValues to check if it's already at
        // 1 -------
        if ((lockVertices[index] == 1 && !isCommandLock) || intensityValues[index] == 1) {
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

    if (!postSetting) {
        // MGlobal::displayInfo("apply the skin stuff");
        // still have to deal with the colors damn it
        if (skinValToSet.size() > 0) {
            int theInfluence = influenceIndex;
            if (mirror) theInfluence = mirrorInfluences[influenceIndex];
            applyCommand(
                theInfluence,
                nbJoints,
                smoothRepeat,
                skinValToSet,
                ignoreLockVal,
                skinWeightList,
                lockJoints,
                smoothStrengthVal,
                ignoreLockJoints,
                doNormalize,
                skinObj,
                skinWeightsForUndo,
                isNurbs,
                meshDag,
                influenceIndices,
                numCVsInV_,
                nurbsDag,
                normalize,
                meshFn,
                nurbsFn,
                perVertexVerticesSetFLAT,
                perVertexVerticesSetINDEX,
                mayaRawPoints,
                rawNormals,
                verticesNormalsIndices,
                verticesNormals,
                numVertices,
                commandIndex,
                modifierNoneShiftControl,
                smoothModifier,
                removeModifier
            );

            intensityValues = std::vector<float>(numVertices, 0);
            dicVertsDistPrevPaint.clear();
            skinValToSet.clear();
        }
    }
}

MStatus refreshColors(
    int influenceIndex,
    int nbJoints,
    int soloColorTypeVal,
    MColor &lockVertColor,
    MColorArray &soloCurrentColors,
    MColorArray &multiCurrentColors,
    double maxSoloColor,
    double minSoloColor,
    MColorArray &jointsColors,
    MColor &soloColor,
    MColor &lockJntColor,
    MIntArray &lockJoints,
    MDoubleArray &soloColorsValues,
    MDoubleArray &skinWeightList,
    MIntArray &lockVertices,
    MIntArray &editVertsIndices,
    MColorArray &multiEditColors,
    MColorArray &soloEditColors
) {
    MStatus status = MS::kSuccess;
    if (multiEditColors.length() != editVertsIndices.length())
        multiEditColors.setLength(editVertsIndices.length());
    if (soloEditColors.length() != editVertsIndices.length())
        soloEditColors.setLength(editVertsIndices.length());

    for (unsigned int i = 0; i < editVertsIndices.length(); ++i) {
        int theVert = editVertsIndices[i];
        MColor multiColor, soloColor;
        bool isVtxLocked = lockVertices[theVert] == 1;

        for (int j = 0; j < nbJoints; ++j) {  // for each joint
            int ind_swl = theVert * nbJoints + j;
            if (ind_swl < skinWeightList.length()) {
                double val = skinWeightList[ind_swl];
                if (lockJoints[j] == 1)
                    multiColor += lockJntColor * val;
                else
                    multiColor += jointsColors[j] * val;
                if (j == influenceIndex) {
                    soloColorsValues[theVert] = val;
                    soloColor = getASoloColor(
                        val,
                        maxSoloColor,
                        minSoloColor,
                        soloColorTypeVal,
                        influenceIndex,
                        jointsColors
                    );
                }
            }
        }
        multiCurrentColors[theVert] = multiColor;
        soloCurrentColors[theVert] = soloColor;
        if (isVtxLocked) {
            multiEditColors[i] = lockVertColor;
            soloEditColors[i] = lockVertColor;
        } else {
            multiEditColors[i] = multiColor;
            soloEditColors[i] = soloColor;
        }
    }
    return status;
}

MStatus applyCommandMirror(
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,
    MIntArray &mirrorInfluences,
    int nbJoints,
    int influenceIndex,
    int smoothRepeat,
    MIntArray &lockJoints,
    MDoubleArray &skinWeightList,
    double smoothStrengthVal,
    bool ignoreLockVal,
    MIntArray &ignoreLockJoints,
    bool doNormalize,
    MObject &skinObj,
    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    int numVertices,
    MFnNurbsSurface &nurbsFn,
    MDagPath &nurbsDag,
    int numCVsInV_,
    bool normalize,
    MIntArray &influenceIndices,
    MDagPath &meshDag,  // So I can get the "maya canonical" edges
    bool isNurbs,
    MDoubleArray &skinWeightsForUndo,
    MFnMesh &meshFn,

    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,

    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier  // Constant
) {
    MStatus status;
    MGlobal::displayInfo(MString("applyCommandMirror "));
    std::map<int, std::pair<float, float>> mirroredJoinedArrayOrdered(mirroredJoinedArray.begin(),
                                                                      mirroredJoinedArray.end());
    ModifierCommands theCommandIndex = getCommandIndexModifiers(
        commandIndex,
        modifierNoneShiftControl,
        smoothModifier,
        removeModifier
    );

    double multiplier = 1.0;

    int influence = influenceIndex;
    int influenceMirror = mirrorInfluences[influenceIndex];

    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices))
        return MStatus::kSuccess;

    MDoubleArray theWeights((int)nbJoints * mirroredJoinedArrayOrdered.size(), 0.0);
    int repeatLimit = 1;
    if (theCommandIndex == ModifierCommands::Smooth || theCommandIndex == ModifierCommands::Sharpen) {
        repeatLimit = smoothRepeat;
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
                std::vector<int> vertsAround = getSurroundingVerticesPerVert(
                    theVert,
                    perVertexVerticesSetFLAT,
                    perVertexVerticesSetINDEX
                );

                status = setAverageWeight(vertsAround, theVert, indexCurrVert, nbJoints,
                                          lockJoints, skinWeightList, theWeights,
                                          smoothStrengthVal * theWeight);
                indexCurrVert++;
            }
        } else {
            if (ignoreLockVal) {
                status = editArrayMirror(theCommandIndex, influence, influenceMirror,
                                         nbJoints, ignoreLockJoints,
                                         skinWeightList, mirroredJoinedArrayOrdered,
                                         theWeights, doNormalize, multiplier);
            } else {
                if (lockJoints[influence] == 1 && theCommandIndex != ModifierCommands::Sharpen) {
                    return status;  //  if locked and it's not sharpen --> do nothing
                }
                status = editArrayMirror(theCommandIndex, influence, influenceMirror,
                                         nbJoints, lockJoints, skinWeightList,
                                         mirroredJoinedArrayOrdered, theWeights, doNormalize,
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

            for (int j = 0; j < nbJoints; ++j) {
                int ind_swl = theVert * nbJoints + j;
                if (ind_swl >= skinWeightList.length())
                    skinWeightList.setLength(ind_swl + 1);
                double val = 0.0;
                int ind_tw = i * nbJoints + j;
                if (ind_tw < theWeights.length())
                    val = theWeights[ind_tw];
                skinWeightList[ind_swl] = val;
            }
            i++;
        }
    }

    MFnSingleIndexedComponent compFn;
    MObject weightsObj = compFn.create(MFn::kMeshVertComponent);
    compFn.addElements(objVertices);
    MFnSkinCluster skinFn(skinObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    skinWeightsForUndo.clear();
    if (!isNurbs) {
        skinFn.setWeights(meshDag, weightsObj, influenceIndices, theWeights, normalize,
                          &skinWeightsForUndo);
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
                          &skinWeightsForUndo);
        transferPointNurbsToMesh(meshFn, nurbsFn);  // we transfer the points postions
        meshFn.updateSurface();
    }
    refreshPointsNormals(
        mayaRawPoints, // A C-style array pointing to the mesh vertex positions
        rawNormals, // A C-style array pointing to the mesh vertex positions
        verticesNormalsIndices,
        verticesNormals, // The per-vertex local space normals of the mesh
        numVertices,
        meshFn,
        meshDag,
        skinObj
    );
    return status;
}

void lineC(short x0, short y0, short x1, short y1, std::vector<std::pair<short, short>>& posi) {
    short dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    short dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    short err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        // setPixel(x0, y0);
        posi.push_back(std::make_pair(x0, y0));

        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

MString fullColorSet = MString("multiColorsSet");
MString soloColorSet = MString("soloColorsSet");
MString fullColorSet2 = MString("multiColorsSet2");
MString soloColorSet2 = MString("soloColorsSet2");

void maya2019RefreshColors(
    bool toggle,
    M3dView &view,
    bool toggleColorState,
    int soloColorVal,
    MFnMesh &meshFn
) {
    meshFn.updateSurface();
    view = M3dView::active3dView();
    // first swap
    if (toggle) toggleColorState = !toggleColorState;

    if (!toggle || toggleColorState) {
        if (soloColorVal == 1) {
            meshFn.setCurrentColorSetName(soloColorSet2);
        } else {
            meshFn.setCurrentColorSetName(fullColorSet2);
        }
        view.refresh(false, true);
    }
    if (!toggle || !toggleColorState) {
        if (soloColorVal == 1) {
            meshFn.setCurrentColorSetName(soloColorSet);
        } else {
            meshFn.setCurrentColorSetName(fullColorSet);
        }
        view.refresh(false, true);
    }
}

MStatus doPerformPaint(
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,
    int soloColorVal,
    bool useColorSetsWhilePainting,
    bool postSetting,
    ModifierCommands commandIndex,
    int influenceIndex,
    int nbJoints,

    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,

    MColorArray &multiCurrentColors,
    MColorArray &soloCurrentColors,
    MColorArray &jointsColors,
    MColor &lockVertColor,
    MColor &lockJntColor,

    MIntArray &lockVertices,
    MIntArray &lockJoints,
    MIntArray &mirrorInfluences,
    MDoubleArray &skinWeightList,

    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,
    ModifierKeys removeModifier,

    M3dView &view,
    bool toggleColorState,

    MFnMesh &meshFn
) {
    MStatus status = MStatus::kSuccess;

    MColorArray multiEditColors, soloEditColors;
    MIntArray editVertsIndices;

    for (const auto &pt : mirroredJoinedArray) {
        int ptIndex = pt.first;
        float weightBase = pt.second.first;
        float weightMirror = pt.second.second;
        MColor multColor, soloColor;
        getColorWithMirror(
            ptIndex,
            influenceIndex,
            weightBase,
            weightMirror,
            nbJoints,

            maxSoloColor,
            minSoloColor,
            soloColorTypeVal,

            multiEditColors,
            multiCurrentColors,
            soloEditColors,
            soloCurrentColors,
            jointsColors,
            lockVertColor,
            lockJntColor,
            multColor,
            soloColor,

            lockVertices,
            lockJoints,
            mirrorInfluences,
            skinWeightList,

            commandIndex,
            modifierNoneShiftControl,
            smoothModifier,
            removeModifier
        );

        editVertsIndices.append(ptIndex);
        multiEditColors.append(multColor);
        soloEditColors.append(soloColor);
    }

    // do actually set colors -----------------------------------
    if (soloColorVal == 0) {
        meshFn.setSomeColors(editVertsIndices, multiEditColors, &fullColorSet);
        meshFn.setSomeColors(editVertsIndices, multiEditColors, &fullColorSet2);
    } else {
        meshFn.setSomeColors(editVertsIndices, soloEditColors, &soloColorSet);
        meshFn.setSomeColors(editVertsIndices, soloEditColors, &soloColorSet2);
    }

    if (useColorSetsWhilePainting || !postSetting) {
        if ((commandIndex == ModifierCommands::LockVertices) || (commandIndex == ModifierCommands::UnlockVertices)) {
            // without that it doesn't refresh because mesh is not invalidated, meaning the
            // skinCluster hasn't changed
            meshFn.updateSurface();
        }
        maya2019RefreshColors(
            true,
            view,
            toggleColorState,
            soloColorVal,
            meshFn
        );
    }
    return status;
}

MStatus doDragCommon(
    short screenX,
    short screenY,
    std::unordered_map<int, float> &dicVertsDistSTART,
    std::unordered_map<int, float> &previousPaint,
    std::unordered_map<int, float> &previousMirrorPaint,
    std::unordered_map<int, float> &dicVertsMirrorDistSTART,
    std::unordered_map<int, float> &skinValuesToSet,
    std::unordered_map<int, float> &skinValuesMirrorToSet,
    std::set<int> &verticesPainted,
    MFloatPoint &inMatrixHit,
    MFloatPoint &inMatrixHitMirror,
    int paintMirror,
    bool successFullMirrorHit,
    bool drawBrushVal,

    M3dView &view,
    MPoint &worldPoint,
    MVector &worldVector,
    MFnMesh &meshFn,
    MMeshIsectAccelParams &accelParams,
    float pressDistance,
    std::vector<std::vector<MIntArray>> &perFaceTriangleVertices,
    const float *mayaOrigRawPoints,
    MFloatPoint &origHitPoint,
    MVector &normalVector,
    int previousfaceHit,
    MFloatMatrix &inclusiveMatrixInverse,
    bool successFullDragHit,
    bool successFullDragMirrorHit,
    MFloatPoint &centerOfBrush,
    MFloatPoint &centerOfMirrorBrush,
    ModifierKeys modifierNoneShiftControl,
    MFloatPointArray &AllHitPoints,
    MFloatPointArray &AllHitPointsMirror,
    std::vector<float> &intensityValuesOrig,
    std::vector<float> &intensityValuesMirror,
    bool useColorSetsWhilePainting,
    bool postSetting,
    bool performBrush,
    int &undersamplingSteps,
    int &undersamplingVal,
    short startScreenX,
    short startScreenY,
    bool initAdjust,
    bool sizeAdjust,
    double sizeVal,
    double smoothStrengthVal,
    double strengthVal,
    bool shiftMiddleDrag,
    double storedDistance,
    double adjustValue,
    short viewCenterX,
    short viewCenterY,
    MFloatMatrix &inclusiveMatrix,

    const float *mayaRawPoints,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX,

    MMeshIntersector &intersectorOrigShape,
    MMeshIntersector &intersector,
    double mirrorMinDist,

    bool coverageVal,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,
    const MVectorArray &verticesNormals,
    ModifierCommands commandIndex,
    bool fractionOversamplingVal,
    int oversamplingVal,
    int curveVal,

    MIntArray &lockVertices,
    MIntArray &mirrorInfluences,
    int influenceIndex,
    int numVertices,
    int nbJoints,
    int smoothRepeat,
    bool ignoreLockVal,
    MDoubleArray &skinWeightList,
    MIntArray &lockJoints,
    MIntArray &ignoreLockJoints,
    bool doNormalize,
    MObject &skinObj,
    MDoubleArray &skinWeightsForUndo,
    bool isNurbs,
    MDagPath &meshDag,
    MIntArray &influenceIndices,
    int numCVsInV_,
    MDagPath &nurbsDag,
    bool normalize,
    MFnNurbsSurface &nurbsFn,
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier,  // Constant
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,

    int soloColorVal,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    MColorArray &multiCurrentColors,
    MColorArray &soloCurrentColors,
    MColorArray &jointsColors,
    MColor &lockVertColor,
    MColor &lockJntColor,
    bool toggleColorState,

    MEvent &event
) {
    MStatus status = MStatus::kSuccess;

    // -----------------------------------------------------------------
    // Dragging with the left mouse button performs the painting.
    // -----------------------------------------------------------------
    if (event.mouseButton() == MEvent::kLeftMouse) {
        // from previous hit get a line----------
        short previousX = screenX;
        short previousY = screenY;
        event.getPosition(screenX, screenY);

        // dictionnary of visited vertices and distances --- prefill it with the previous hit ---
        std::unordered_map<int, float> dicVertsDistToGrow = dicVertsDistSTART;
        std::unordered_map<int, float> dicVertsDistToGrowMirror = dicVertsMirrorDistSTART;

        // for linear growth ----------------------------------
        MFloatPointArray lineHitPoints, lineHitPointsMirror;
        lineHitPoints.append(inMatrixHit);
        if (paintMirror != 0 && successFullMirrorHit) {  // if mirror is not OFf
            lineHitPointsMirror.append(inMatrixHitMirror);
        }
        // --------- LINE OF PIXELS --------------------
        std::vector<std::pair<short, short>> line2dOfPixels;
        // get pixels of the line of pixels
        lineC(previousX, previousY, screenX, screenY, line2dOfPixels);
        int nbPixelsOfLine = (int)line2dOfPixels.size();

        MFloatPoint hitPoint, hitMirrorPoint;
        MFloatPoint hitPointIM, hitMirrorPointIM;
        int faceHit, faceMirrorHit;

        bool successFullHit2 = computeHit(
            screenX,
            screenY,
            drawBrushVal,
            view,
            worldPoint,
            worldVector,
            meshFn,
            accelParams,
            pressDistance,
            paintMirror,
            perFaceTriangleVertices,
            mayaOrigRawPoints,
            origHitPoint,
            normalVector,
            faceHit,
            hitPoint
        );

        bool successFullMirrorHit2 = false;
        if (successFullHit2) {
            // stored in start dic for next call of drag function
            previousfaceHit = faceHit;
            dicVertsDistSTART.clear();
            hitPointIM = hitPoint * inclusiveMatrixInverse;
            expandHit(
                faceHit,
                mayaRawPoints,
                sizeVal,
                perFaceVerticesSetFLAT,
                perFaceVerticesSetINDEX,
                hitPointIM,
                dicVertsDistSTART
            );

            // If the mirror happens -------------------------
            if (paintMirror != 0) {  // if mirror is not OFf
                successFullMirrorHit2 = //getMirrorHit(faceMirrorHit, hitMirrorPoint);

                getMirrorHit(
                    paintMirror,
                    origHitPoint,
                    intersectorOrigShape,
                    intersector,
                    mirrorMinDist,
                    perFaceTriangleVertices,
                    mayaRawPoints,
                    inclusiveMatrix,
                    centerOfBrush,
                    faceMirrorHit,
                    hitMirrorPoint
                );


                if (successFullMirrorHit2) {
                    hitMirrorPointIM = hitMirrorPoint * inclusiveMatrixInverse;
                    expandHit(
                        faceMirrorHit,
                        mayaRawPoints,
                        sizeVal,
                        perFaceVerticesSetFLAT,
                        perFaceVerticesSetINDEX,
                        hitMirrorPointIM,
                        dicVertsMirrorDistSTART
                    );
                }
            }
        }
        if (!successFullDragHit && !successFullHit2)  // moving in empty zone
            return MStatus::kNotFound;
        //////////////////////////////////////////////////////////////////////////////
        successFullDragHit = successFullHit2;
        successFullDragMirrorHit = successFullMirrorHit2;

        if (successFullDragHit) {
            centerOfBrush = hitPoint;
            inMatrixHit = hitPointIM;
            if (paintMirror != 0 && successFullDragMirrorHit) {
                centerOfMirrorBrush = hitMirrorPoint;
                inMatrixHitMirror = hitMirrorPointIM;
            }
        }
        int incrementValue = 1;
        if (incrementValue < nbPixelsOfLine) {
            for (int i = incrementValue; i < nbPixelsOfLine; i += incrementValue) {
                auto myPair = line2dOfPixels[i];
                short x = myPair.first;
                short y = myPair.second;

                bool successFullHit2 = computeHit(
                    x,
                    y,
                    false,
                    view,
                    worldPoint,
                    worldVector,
                    meshFn,
                    accelParams,
                    pressDistance,
                    paintMirror,
                    perFaceTriangleVertices,
                    mayaOrigRawPoints,
                    origHitPoint,
                    normalVector,
                    faceHit,
                    hitPoint
                );
                if (successFullHit2) {
                    hitPointIM = hitPoint * inclusiveMatrixInverse;
                    lineHitPoints.append(hitPointIM);
                    successFullHit2 = expandHit(
                        faceHit,
                        mayaRawPoints,
                        sizeVal,
                        perFaceVerticesSetFLAT,
                        perFaceVerticesSetINDEX,
                        hitPointIM,
                        dicVertsDistToGrow
                    );
                    // mirror part -------------------
                    if (paintMirror != 0) {  // if mirror is not OFf
                        successFullMirrorHit2 = getMirrorHit(
                            paintMirror,
                            origHitPoint,
                            intersectorOrigShape,
                            intersector,
                            mirrorMinDist,
                            perFaceTriangleVertices,
                            mayaRawPoints,
                            inclusiveMatrix,
                            centerOfBrush,
                            faceMirrorHit,
                            hitMirrorPoint
                        );

                        if (successFullMirrorHit2) {
                            hitMirrorPointIM = hitMirrorPoint * inclusiveMatrixInverse;
                            lineHitPointsMirror.append(hitMirrorPointIM);
                            expandHit(
                                faceMirrorHit,
                                mayaRawPoints,
                                sizeVal,
                                perFaceVerticesSetFLAT,
                                perFaceVerticesSetINDEX,
                                hitMirrorPointIM,
                                dicVertsDistToGrowMirror
                            );
                        }
                    }
                }
            }
        }
        // only now add last hit -------------------------
        if (successFullDragHit) {
            lineHitPoints.append(inMatrixHit);
            expandHit(
                faceHit,
                mayaRawPoints,
                sizeVal,
                perFaceVerticesSetFLAT,
                perFaceVerticesSetINDEX,
                inMatrixHit,
                dicVertsDistToGrow
            );  // to get closest hit
            if (paintMirror != 0 && successFullDragMirrorHit) {   // if mirror is not OFf
                lineHitPointsMirror.append(inMatrixHitMirror);
                expandHit(
                    faceMirrorHit,
                    mayaRawPoints,
                    sizeVal,
                    perFaceVerticesSetFLAT,
                    perFaceVerticesSetINDEX,
                    inMatrixHitMirror,
                    dicVertsDistToGrowMirror
                );
            }
        }

        modifierNoneShiftControl = ModifierKeys::NoModifier;
        if (event.isModifierShift()) {
            if (event.isModifierControl()) {
                modifierNoneShiftControl = ModifierKeys::ControlShift;
            } else {
                modifierNoneShiftControl = ModifierKeys::Shift;
            }
        }
        else if (event.isModifierControl()) {
            modifierNoneShiftControl = ModifierKeys::Control;
        }

        // let's expand these arrays to the outer part of the brush----------------
        for (auto hitPoint : lineHitPoints) AllHitPoints.append(hitPoint);
        for (auto hitPoint : lineHitPointsMirror) AllHitPointsMirror.append(hitPoint);

        growArrayOfHitsFromCenters(
            coverageVal,
            sizeVal,
            perVertexVerticesSetFLAT,
            perVertexVerticesSetINDEX,
            mayaRawPoints,
            verticesNormals,
            worldVector,
            lineHitPoints,
            dicVertsDistToGrow
        );

        addBrushShapeFallof(
            strengthVal,
            smoothStrengthVal,
            modifierNoneShiftControl,
            commandIndex,
            fractionOversamplingVal,
            oversamplingVal,
            sizeVal,
            curveVal,
            dicVertsDistToGrow
        );

        preparePaint(
            postSetting,
            commandIndex,
            modifierNoneShiftControl,
            lockVertices,
            mirrorInfluences,
            influenceIndex,
            numVertices,
            nbJoints,
            smoothRepeat,
            ignoreLockVal,
            skinWeightList,
            lockJoints,
            smoothStrengthVal,
            ignoreLockJoints,
            doNormalize,
            skinObj,
            skinWeightsForUndo,
            isNurbs,
            meshDag,
            influenceIndices,
            numCVsInV_,
            nurbsDag,
            normalize,
            meshFn,
            nurbsFn,
            perVertexVerticesSetFLAT,
            perVertexVerticesSetINDEX,
            mayaRawPoints, // A C-style array pointing to the mesh vertex positions
            rawNormals, // A C-style array pointing to the mesh vertex positions
            verticesNormalsIndices,
            verticesNormals, // The per-vertex local space normals of the mesh
            smoothModifier,  // Constant
            removeModifier,  // Constant

            dicVertsDistToGrow,
            previousPaint,
            intensityValuesOrig,
            skinValuesToSet,
            verticesPainted,
            false
        );

        if (paintMirror != 0) {  // mirror
            growArrayOfHitsFromCenters(
                coverageVal,
                sizeVal,
                perVertexVerticesSetFLAT,
                perVertexVerticesSetINDEX,
                mayaRawPoints,
                verticesNormals,
                worldVector,
                lineHitPointsMirror,
                dicVertsDistToGrowMirror
            );
            addBrushShapeFallof(
                strengthVal,
                smoothStrengthVal,
                modifierNoneShiftControl,
                commandIndex,
                fractionOversamplingVal,
                oversamplingVal,
                sizeVal,
                curveVal,
                dicVertsDistToGrowMirror
            );
            preparePaint(
                postSetting,
                commandIndex,
                modifierNoneShiftControl,
                lockVertices,
                mirrorInfluences,
                influenceIndex,
                numVertices,
                nbJoints,
                smoothRepeat,
                ignoreLockVal,
                skinWeightList,
                lockJoints,
                smoothStrengthVal,
                ignoreLockJoints,
                doNormalize,
                skinObj,
                skinWeightsForUndo,
                isNurbs,
                meshDag,
                influenceIndices,
                numCVsInV_,
                nurbsDag,
                normalize,
                meshFn,
                nurbsFn,
                perVertexVerticesSetFLAT,
                perVertexVerticesSetINDEX,
                mayaRawPoints,
                rawNormals,
                verticesNormalsIndices,
                verticesNormals,
                smoothModifier,
                removeModifier,
                dicVertsDistToGrowMirror,
                previousMirrorPaint,
                intensityValuesMirror,
                skinValuesMirrorToSet,
                verticesPainted,
                true
            );
        }
        mergeMirrorArray(mirroredJoinedArray, skinValuesToSet, skinValuesMirrorToSet);
    
        if (useColorSetsWhilePainting || !postSetting) {
            doPerformPaint(
                mirroredJoinedArray,
                soloColorVal,
                useColorSetsWhilePainting,
                postSetting,
                commandIndex,
                influenceIndex,
                nbJoints,
                maxSoloColor,
                minSoloColor,
                soloColorTypeVal,
                multiCurrentColors,
                soloCurrentColors,
                jointsColors,
                lockVertColor,
                lockJntColor,
                lockVertices,
                lockJoints,
                mirrorInfluences,
                skinWeightList,
                modifierNoneShiftControl,
                smoothModifier,
                removeModifier,
                view,
                toggleColorState,
                meshFn
            );
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
        event.getPosition(screenX, screenY);
        // Get the current and initial cursor position and calculate the
        // delta movement from them.
        MPoint currentPos(screenX, screenY);
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

MStatus doDrag(
    bool postSetting,
    bool useColorSetsWhilePainting,
    bool pickMaxInfluenceVal,
    bool drawBrushVal,
    bool pickInfluenceVal,
    MStatus &pressStatus,
    MColor &colorVal,
    int lineWidthVal,
    bool successFullDragHit,
    MFloatPoint &centerOfBrush,
    MVector &normalVector,
    double sizeVal,
    bool sizeAdjust,
    MFloatPoint surfacePointAdjust,
    MVector worldVectorAdjust,
    double adjustValue,
    bool volumeVal,
    bool drawRangeVal,
    double rangeVal,
    short startScreenX,
    short startScreenY,

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

    MEvent &event,
    MHWRender::MUIDrawManager &drawManager,
    const MHWRender::MFrameContext &context,

    short screenX,
    short screenY,
    std::unordered_map<int, float> &dicVertsDistSTART,
    std::unordered_map<int, float> &previousPaint,
    std::unordered_map<int, float> &previousMirrorPaint,
    std::unordered_map<int, float> &dicVertsMirrorDistSTART,
    std::unordered_map<int, float> &skinValuesToSet,
    std::unordered_map<int, float> &skinValuesMirrorToSet,
    MFloatPoint &inMatrixHit,
    MFloatPoint &inMatrixHitMirror,
    bool successFullMirrorHit,

    M3dView &view,
    MPoint &worldPoint,
    MVector &worldVector,
    MFnMesh &meshFn,
    MMeshIsectAccelParams &accelParams,
    float pressDistance,
    const float *mayaOrigRawPoints,
    MFloatPoint &origHitPoint,
    int previousfaceHit,
    MFloatMatrix &inclusiveMatrixInverse,
    bool successFullDragMirrorHit,
    MFloatPoint &centerOfMirrorBrush,
    ModifierKeys modifierNoneShiftControl,
    MFloatPointArray &AllHitPoints,
    MFloatPointArray &AllHitPointsMirror,
    std::vector<float> &intensityValuesOrig,
    std::vector<float> &intensityValuesMirror,
    bool performBrush,
    int &undersamplingSteps,
    int &undersamplingVal,
    bool initAdjust,
    double smoothStrengthVal,
    double strengthVal,
    bool shiftMiddleDrag,
    double storedDistance,
    short viewCenterX,
    short viewCenterY,

    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX,

    MMeshIntersector &intersectorOrigShape,
    MMeshIntersector &intersector,
    double mirrorMinDist,

    bool coverageVal,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX,
    ModifierCommands commandIndex,
    bool fractionOversamplingVal,
    int oversamplingVal,
    int curveVal,

    MIntArray &lockVertices,
    int nbJoints,
    int smoothRepeat,
    bool ignoreLockVal,
    MDoubleArray &skinWeightList,
    MIntArray &lockJoints,
    MIntArray &ignoreLockJoints,
    bool doNormalize,
    MObject &skinObj,
    MDoubleArray &skinWeightsForUndo,
    bool isNurbs,
    MDagPath &meshDag,
    MIntArray &influenceIndices,
    int numCVsInV_,
    MDagPath &nurbsDag,
    bool normalize,
    MFnNurbsSurface &nurbsFn,
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier,  // Constant

    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    MColor &lockJntColor,
    bool toggleColorState
) {
    MStatus status = MStatus::kSuccess;
    if (pickMaxInfluenceVal || pickInfluenceVal) {
        return MS::kFailure;
    }

    status = doDragCommon(
        screenX,
        screenY,
        dicVertsDistSTART,
        previousPaint,
        previousMirrorPaint,
        dicVertsMirrorDistSTART,
        skinValuesToSet,
        skinValuesMirrorToSet,
        verticesPainted,
        inMatrixHit,
        inMatrixHitMirror,
        paintMirror,
        successFullMirrorHit,
        drawBrushVal,
        view,
        worldPoint,
        worldVector,
        meshFn,
        accelParams,
        pressDistance,
        perFaceTriangleVertices,
        mayaOrigRawPoints,
        origHitPoint,
        normalVector,
        previousfaceHit,
        inclusiveMatrixInverse,
        successFullDragHit,
        successFullDragMirrorHit,
        centerOfBrush,
        centerOfMirrorBrush,
        modifierNoneShiftControl,
        AllHitPoints,
        AllHitPointsMirror,
        intensityValuesOrig,
        intensityValuesMirror,
        useColorSetsWhilePainting,
        postSetting,
        performBrush,
        undersamplingSteps,
        undersamplingVal,
        startScreenX,
        startScreenY,
        initAdjust,
        sizeAdjust,
        sizeVal,
        smoothStrengthVal,
        strengthVal,
        shiftMiddleDrag,
        storedDistance,
        adjustValue,
        viewCenterX,
        viewCenterY,
        inclusiveMatrix,

        mayaRawPoints,
        perFaceVerticesSetFLAT,
        perFaceVerticesSetINDEX,

        intersectorOrigShape,
        intersector,
        mirrorMinDist,

        coverageVal,
        perVertexVerticesSetFLAT,
        perVertexVerticesSetINDEX,
        verticesNormals,
        commandIndex,
        fractionOversamplingVal,
        oversamplingVal,
        curveVal,

        lockVertices,
        mirrorInfluences,
        influenceIndex,
        numVertices,
        nbJoints,
        smoothRepeat,
        ignoreLockVal,
        skinWeightList,
        lockJoints,
        ignoreLockJoints,
        doNormalize,
        skinObj,
        skinWeightsForUndo,
        isNurbs,
        meshDag,
        influenceIndices,
        numCVsInV_,
        nurbsDag,
        normalize,
        nurbsFn,
        rawNormals, // A C-style array pointing to the mesh vertex positions
        verticesNormalsIndices,
        smoothModifier,  // Constant
        removeModifier,  // Constant
        mirroredJoinedArray,

        soloColorVal,
        maxSoloColor,
        minSoloColor,
        soloColorTypeVal,
        multiCurrentColors,
        soloCurrentColors,
        jointsColors,
        lockVertColor,
        lockJntColor,
        toggleColorState,
        event

    );



    if (postSetting && !useColorSetsWhilePainting) {
        drawManager.beginDrawable();
        drawMeshWhileDrag(
            verticesPainted,
            mirroredJoinedArray,
            numFaces,
            numEdges,
            numVertices,
            influenceIndex,
            paintMirror,
            soloColorVal,
            drawTransparency,
            drawPoints, 
            drawTriangles, 
            drawEdges, 
            lockVertColor, 
            jointsColors,
            soloCurrentColors,
            multiCurrentColors,
            mayaRawPoints,
            theCommandIndex, 
            mirrorInfluences, 
            inclusiveMatrix, 
            verticesNormals,
            perVertexFaces,
            perVertexEdges,
            perFaceTriangleVertices,
            perEdgeVertices,
            drawManager
        );
        drawManager.endDrawable();
    }
    CHECK_MSTATUS_AND_RETURN_SILENT(status);

    // -----------------------------------------------------------------
    // display when painting or setting the brush size
    // -----------------------------------------------------------------
    if (drawBrushVal || (event.mouseButton() == MEvent::kMiddleMouse)) {
        CHECK_MSTATUS_AND_RETURN_SILENT(pressStatus);
        drawManager.beginDrawable();

        drawManager.setColor(MColor((pow(colorVal.r, 0.454f)), (pow(colorVal.g, 0.454f)),
                                    (pow(colorVal.b, 0.454f))));
        drawManager.setLineWidth((float)lineWidthVal);
        // Draw the circle in regular paint mode.
        // The range circle doens't get drawn here to avoid visual
        // clutter.
        if (event.mouseButton() == MEvent::kLeftMouse) {
            if (successFullDragHit)
                drawManager.circle(centerOfBrush, normalVector, sizeVal);
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

void setInfluenceIndex(
    int influenceIndex,
    MStringArray &inflNames,
    MString &pickedInfluence,
    int soloColorVal, // The solo color index
    MFnMesh &meshFn,  // For getting the mesh data
    int value,
    bool selectInUI
) {
    if (value != influenceIndex) {
        if (value < inflNames.length()) {
            influenceIndex = value;
            pickedInfluence = inflNames[value];
            if (selectInUI) {
                MUserEventMessage::postUserEvent("brSkinBrush_pickedInfluence");
            }
        }

        if (soloColorVal == 1) {  // solo IF NOT IT CRASHES on a first pick before paint
            MString currentColorSet = meshFn.currentColorSetName();  // get current soloColor
            if (currentColorSet != soloColorSet)
                meshFn.setCurrentColorSetName(soloColorSet);
            editSoloColorSet(false);
        }
        meshFn.updateSurface();  // for proper redraw hopefully
    }
}

MStatus doPressCommon(
    M3dView &view,
    MDagPath &meshDag,
    bool pickMaxInfluenceVal,
    bool pickInfluenceVal,
    std::vector<drawingDeformers> BBoxOfDeformers,
    int biggestInfluence,
    int influenceIndex,
    bool postSetting,
    int paintMirror,
    MDoubleArray &fullUndoSkinWeightList,
    MDoubleArray &skinWeightList,
    MDoubleArray &paintArrayValues,
    int numVertices,
    std::unordered_map<int, float> &skinValuesToSet,
    std::unordered_map<int, float> &skinValuesMirrorToSet,
    std::set<int> &verticesPainted,
    std::vector<float> &intensityValuesOrig,
    std::vector<float> &intensityValuesMirror,
    int &undersamplingSteps,
    bool performBrush,
    short screenX,
    short screenY,
    unsigned int width,
    unsigned int height,
    short viewCenterX,
    short viewCenterY,
    short startScreenX,
    short startScreenY,
    double storedDistance,
    bool initAdjust,
    bool sizeAdjust,
    double adjustValue,
    bool successFullDragHit,
    bool successFullDragMirrorHit,
    std::unordered_map<int, float> &dicVertsDistSTART,
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,
    bool successFullHit,
    int previousfaceHit,
    MFloatPoint &centerOfBrush,
    MFloatPointArray &AllHitPoints,
    MFloatPointArray &AllHitPointsMirror,
    MFloatPoint &inMatrixHit,
    MFloatMatrix &inclusiveMatrixInverse,
    std::unordered_map<int, float> &dicVertsMirrorDistSTART,
    bool successFullMirrorHit,
    MFnMesh &meshFn,
    MFloatPoint &centerOfMirrorBrush,
    MVector &normalMirroredVector,
    MFloatPoint &inMatrixHitMirror,
    MFloatPoint surfacePointAdjust,
    MVector worldVectorAdjust,
    const MVector &worldVector,
    MStringArray &inflNames,
    MString &pickedInfluence,
    int soloColorVal, // The solo color index

    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    MObject &skinObj,

    MEvent &event
) {
    MStatus status = MStatus::kSuccess;

    if (meshDag.node().isNull()) return MStatus::kNotFound;

    view = M3dView::active3dView();

    if (pickMaxInfluenceVal || pickInfluenceVal) {
        BBoxOfDeformers.clear();

        if (pickMaxInfluenceVal && biggestInfluence != -1) {
            MUserEventMessage::postUserEvent("brSkinBrush_influencesReordered");
        }

        if (biggestInfluence != influenceIndex && biggestInfluence != -1) {
            setInfluenceIndex(
                influenceIndex,
                inflNames,
                pickedInfluence,
                soloColorVal, // The solo color index
                meshFn,  // For getting the mesh data
                biggestInfluence,
                true // true for select in UI
            ); 
            maya2019RefreshColors();
        }

        return MStatus::kNotFound;
    }

    // store for undo purposes --------------------------------------------------------------
    // only if painting not after
    if (!postSetting || paintMirror != 0) {
        fullUndoSkinWeightList = MDoubleArray(skinWeightList);
    }
    // update values ------------------------------------------------------------------------
    refreshPointsNormals(
        mayaRawPoints,
        rawNormals,
        verticesNormalsIndices,
        verticesNormals,
        numVertices,
        meshFn,
        meshDag,
        skinObj
    );

    // first reset attribute to paint values off if we're doing that ------------------------
    paintArrayValues.copy(MDoubleArray(numVertices, 0.0));
    skinValuesToSet.clear();
    skinValuesMirrorToSet.clear();
    verticesPainted.clear();

    // reset values ---------------------------------
    intensityValuesOrig = std::vector<float>(numVertices, 0);
    intensityValuesMirror = std::vector<float>(numVertices, 0);
    // initialize --
    undersamplingSteps = 0;
    performBrush = false;

    event.getPosition(screenX, screenY);

    // Get the size of the viewport and calculate the center for placing
    // the value messages when adjusting the brush settings.
    unsigned int x;
    unsigned int y;
    view.viewport(x, y, width, height);
    viewCenterX = (short)width / 2;
    viewCenterY = (short)height / 2;

    // Store the initial mouse position. These get used when adjusting
    // the brush size and strength values.
    startScreenX = screenX;
    startScreenY = screenY;
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
        dicVertsDistSTART.clear();
        mirroredJoinedArray.clear();
        successFullHit =
            computeHit(screenX, screenY, false, previousfaceHit, centerOfBrush);
        if (!successFullHit) {
            return MStatus::kNotFound;
        }
        AllHitPoints.clear();
        AllHitPointsMirror.clear();

        // we put it inside our world matrix
        inMatrixHit = centerOfBrush * inclusiveMatrixInverse;
        successFullHit =
            expandHit(previousfaceHit, inMatrixHit, dicVertsDistSTART);

        // mirror part -------------------
        if (paintMirror != 0) {  // if mirror is not OFf
            dicVertsMirrorDistSTART.clear();
            int faceMirrorHit;
            successFullMirrorHit = getMirrorHit(faceMirrorHit, centerOfMirrorBrush);
            meshFn.getPolygonNormal(faceMirrorHit, normalMirroredVector, MSpace::kWorld);

            inMatrixHitMirror = centerOfMirrorBrush * inclusiveMatrixInverse;
            if (successFullMirrorHit) {
                expandHit(faceMirrorHit, inMatrixHitMirror, dicVertsMirrorDistSTART);
            }
        }
        // Store the initial surface point and view vector to use when
        // the brush settings are adjusted because the brush circle
        // needs to be static during the adjustment.
        surfacePointAdjust = centerOfBrush;
        worldVectorAdjust = worldVector;
    }
    return status;
}

MStatus doPress(
    MEvent &event,
    MHWRender::MUIDrawManager &drawMgr,
    const MHWRender::MFrameContext &context
) {
    pressStatus = doPressCommon(event);
    CHECK_MSTATUS_AND_RETURN_SILENT(pressStatus);
    doDrag(event, drawMgr, context);
    return MStatus::kSuccess;
}

MStatus doReleaseCommon(
    MEvent &event
) {
    // Don't continue if no mesh has been set.
    if (meshFn.object().isNull()) return MS::kFailure;
    if (pickMaxInfluenceVal || pickInfluenceVal) {
        pickMaxInfluenceVal = false;
        pickInfluenceVal = false;
    }
    refreshDone = false;
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

MStatus doRelease(
    MEvent &event,
    MHWRender::MUIDrawManager &drawMgr,
    const MHWRender::MFrameContext &context
) {
    return doReleaseCommon(event);
}

void doTheAction() {
    // If the smoothing has been performed send the current values to
    // the tool command along with the necessary data for undo and redo.
    // The same goes for the select mode.
    MColorArray multiEditColors, soloEditColors;
    int nbVerticesPainted = (int)verticesPainted.size();
    MIntArray editVertsIndices(nbVerticesPainted, 0);
    MIntArray undoLocks, redoLocks;

    MStatus status;
    if (lockJoints.length() < nbJoints) {
        getListLockJoints(skinObj, nbJoints, indicesForInfluenceObjects, lockJoints);
        if (lockJoints.length() < nbJoints) {
            lockJoints = MIntArray(nbJoints, 0);
        }
    }
    MDoubleArray prevWeights((int)verticesPainted.size() * nbJoints, 0);

    std::vector<int> intArray;
    intArray.resize(verticesPainted.size());

    int i = 0;
    for (const auto &theVert : verticesPainted) {
        editVertsIndices[i] = theVert;
        i++;
    }

    ModifierCommands theCommandIndex = getCommandIndexModifiers();
    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        undoLocks.copy(lockVertices);
        bool addLocks = theCommandIndex == ModifierCommands::LockVertices;
        editLocks(skinObj, editVertsIndices, addLocks, lockVertices);
        redoLocks.copy(lockVertices);
    } else {
        if (paintMirror != 0) {
            int mirrorInfluenceIndex = mirrorInfluences[influenceIndex];
            mergeMirrorArray(skinValuesToSet, skinValuesMirrorToSet);

            if (mirrorInfluenceIndex != influenceIndex)
                status = applyCommandMirror();
            else {  // we merge in one array, it's easier
                for (const auto &element : skinValuesMirrorToSet) {
                    int index = element.first;
                    float value = element.second;

                    auto ret = skinValuesToSet.insert(std::make_pair(index, value));
                    if (!ret.second) ret.first->second = std::max(value, ret.first->second);
                }
                status = applyCommand(influenceIndex, skinValuesToSet);  //
            }
        } else if (skinValuesToSet.size() > 0) {
            status = applyCommand(influenceIndex, skinValuesToSet);  //
            if (status == MStatus::kFailure) {
                MGlobal::displayError(
                    MString("Something went wrong. EXIT the brush and RESTART it"));
                return;
            }
        }
        if (!postSetting) {  // only store if not constant setting
            int i = 0;
            for (const auto &theVert : verticesPainted) {
                for (int j = 0; j < nbJoints; ++j) {
                    prevWeights[i * nbJoints + j] =
                        fullUndoSkinWeightList[theVert * nbJoints + j];
                }
                i++;
            }
        }
    }
    refreshColors(editVertsIndices, multiEditColors, soloEditColors);
    meshFn.setSomeColors(editVertsIndices, multiEditColors, &fullColorSet);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &soloColorSet);

    meshFn.setSomeColors(editVertsIndices, multiEditColors, &fullColorSet2);
    meshFn.setSomeColors(editVertsIndices, soloEditColors, &soloColorSet2);
    if ((theCommandIndex == ModifierCommands::LockVertices) || (theCommandIndex == ModifierCommands::UnlockVertices)) {
        // without that it doesn't refresh because mesh is not invalidated, meaning the skinCluster
        // hasn't changed
        meshFn.updateSurface();
    }
    skinValuesToSet.clear();
    skinValuesMirrorToSet.clear();
    previousPaint.clear();
    previousMirrorPaint.clear();

    if (!firstPaintDone) {
        firstPaintDone = true;
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

    MFnDependencyNode skinDep(skinObj);
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
    if (!postSetting) {
        cmd->setWeights(prevWeights);
    } else {
        cmd->setWeights(skinWeightsForUndo);
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

