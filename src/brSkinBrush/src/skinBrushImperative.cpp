

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
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <span>


typedef float coord_t;
typedef std::array<coord_t, 3> point_t;



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




