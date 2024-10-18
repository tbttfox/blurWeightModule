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

#include <set>
#include <map>
#include <unordered_map>
#include <vector>


struct drawingDeformers {
    MMatrix mat;
    MPoint center;
    MPoint minPt;
    MPoint maxPt;
    MVector up, right;
    double width, height, depth;
};



struct MeshData {
    // Holds unchanging variables related to the mesh
    // like the quick accessors and octree

    MDagPath meshDag;
    MFnMesh meshFn;
    MMeshIsectAccelParams accelParams;
    MFloatMatrix inclusiveMatrix;
    MFloatMatrix inclusiveMatrixInverse;

    int numVertices;
    float* mayaOrigRawPoints;
    float* mayaRawPoints;
    float* rawNormals;

    MIntArray verticesNormalsIndices;
    MVectorArray verticesNormals; // The per-vertex local space normals of the mesh

    std::vector<MIntArray> perVertexFaces; // The face indices for each vertex
    std::vector<MIntArray> perVertexEdges; // The edge indices for each vertex
    std::vector<std::vector<MIntArray>> perFaceTriangleVertices; // Somehow get the triangle indices per face
    std::vector<std::pair<int, int>> perEdgeVertices; // The endpoint vertex indices of each edge

    std::vector<int> perFaceVerticesSetFLAT;
    std::vector<int> perFaceVerticesSetINDEX;
    std::vector<int> perVertexVerticesSetFLAT;
    std::vector<int> perVertexVerticesSetINDEX;
};


struct InfluenceData {
    // Stores the joints (and mirror data)
    // including their colors and locks

};


struct WeightData {
    // Stores the per-vertex weights, and relationships between the verts
    // As well as the colors
    // MeshData generally won't change, weightData will, that's why this is separate
    MIntArray lockVertices;

};



struct UserInputData {
    // All of the current data for where the mouse is, and what its interacting with
    // and the current keyboard options
    // All of this will (hopefully) be pre-processed so we can just ask the struct
    // whether or not to do "a thing"
};






MColor getASoloColor(
    double val,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    int influenceIndex,
    MColorArray &jointsColors
);


ModifierCommands getCommandIndexModifiers(
    ModifierCommands commandIndex,
    ModifierKeys modifierNoneShiftControl,
    ModifierKeys smoothModifier,  // Constant
    ModifierKeys removeModifier  // Constant
);

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

);

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
);

std::vector<int> getSurroundingVerticesPerVert(
    int vertexIndex,
    const std::vector<int> &perVertexVerticesSetFLAT,
    const std::vector<int> &perVertexVerticesSetINDEX
);



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
);

void copyToFloatMatrix(const MMatrix& src, MFloatMatrix& dst);

double getFalloffValue(int curveVal, double value, double strength);

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
);

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
);

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
);

std::vector<int> getSurroundingVerticesPerFace(
    int vertexIndex,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX
);

bool expandHit(
    int faceHit,
    const float *mayaRawPoints,
    double sizeVal,
    std::vector<int> &perFaceVerticesSetFLAT,
    std::vector<int> &perFaceVerticesSetINDEX,

    MFloatPoint &hitPoint,
    std::unordered_map<int, float> &dicVertsDist
);

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
);

void mergeMirrorArray(
    std::unordered_map<int, std::pair<float, float>> &mirroredJoinedArray,
    std::unordered_map<int, float> &valuesBase,
    std::unordered_map<int, float> &valuesMirrored
);

MStatus setAverageWeight(
    std::vector<int>& verticesAround,
    int currentVertex,
    int indexCurrVert,
    int nbJoints,
    MIntArray& lockJoints,
    MDoubleArray& fullWeightArray,
    MDoubleArray& theWeights,
    double strengthVal
);

MStatus editArray(
    ModifierCommands command,
    int influence,
    int nbJoints,
    MIntArray& lockJoints,
    MDoubleArray& fullWeightArray,
    std::map<int, double>& valuesToSet,
    MDoubleArray& theWeights,
    bool normalize,
    double mutliplier
);

MStatus editArrayMirror(
    ModifierCommands command,
    int influence,
    int influenceMirror,
    int nbJoints,
    MIntArray& lockJoints,
    MDoubleArray& fullWeightArray,
    std::map<int, std::pair<float, float>>& valuesToSetMirror,
    MDoubleArray& theWeights,
    bool normalize,
    double mutliplier
);

MStatus transferPointNurbsToMesh(
    MFnMesh& msh,
    MFnNurbsSurface& nurbsFn
);

MStatus refreshPointsNormals(
    const float* mayaRawPoints, // A C-style array pointing to the mesh vertex positions
    const float* rawNormals, // A C-style array pointing to the mesh vertex positions
    MIntArray &verticesNormalsIndices,
    MVectorArray &verticesNormals, // The per-vertex local space normals of the mesh
    int numVertices,
    MFnMesh &meshFn,
    MDagPath &meshDag,
    MObject &skinObj
);

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
);

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
);

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
);

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
);

void lineC(
    short x0,
    short y0,
    short x1,
    short y1,
    std::vector<std::pair<short, short>>& posi
);

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
);

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
);

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
);


MStatus editSoloColorSet(
    int numVertices, // The number of vertices in the current mesh
    int nbJoints,
    int influenceIndex,
    MDoubleArray &skinWeightList,
    MIntArray &lockVertices,
    MDoubleArray &soloColorsValues,
    MColorArray &soloCurrentColors,
    MColor &lockVertColor,
    MFnMesh &meshFn,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    MColorArray &jointsColors,
    bool doBlack
);

void setInfluenceIndex(
    int influenceIndex,
    MStringArray &inflNames,
    MString &pickedInfluence,
    int soloColorVal, // The solo color index
    MFnMesh &meshFn,  // For getting the mesh data
    int value,
    int numVertices,
    int nbJoints,
    MDoubleArray &skinWeightList,
    MIntArray &lockVertices,
    MDoubleArray &soloColorsValues,
    MColorArray &soloCurrentColors,
    MColor &lockVertColor,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    MColorArray &jointsColors,
    bool selectInUI
);

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

    int nbJoints,
    MIntArray &lockVertices,
    MDoubleArray &soloColorsValues,
    MColorArray &soloCurrentColors,
    MColor &lockVertColor,
    double maxSoloColor,
    double minSoloColor,
    int soloColorTypeVal,
    MColorArray &jointsColors,
    bool toggleColorState,

    MEvent &event
);

MStatus doReleaseCommon(
    MFnMesh &meshFn,  // For getting the mesh data
    bool pickMaxInfluenceVal,
    bool pickInfluenceVal,
    bool refreshDone,
    bool initAdjust,
    MStatus &pressStatus,
    bool sizeAdjust,
    double sizeVal,
    double adjustValue,
    double smoothStrengthVal,
    double strengthVal,
    bool performBrush,

    MEvent &event
);



MStatus getListLockJoints(
    MObject& skinCluster,
    int nbJoints,
    MIntArray indicesForInfluenceObjects,
    MIntArray& jointsLocks)
;


void doTheAction(
    std::set<int> &verticesPainted,
    MIntArray &lockJoints,
    int nbJoints,
    MObject &skinObj,
    MIntArray indicesForInfluenceObjects,
    MIntArray &lockVertices,
    int paintMirror,
    MIntArray &mirrorInfluences,
    int influenceIndex,
    std::unordered_map<int, float> &skinValuesToSet,
    std::unordered_map<int, float> &skinValuesMirrorToSet,
    bool postSetting,
    MDoubleArray &fullUndoSkinWeightList,
    MFnMesh &meshFn,  // For getting the mesh data
    std::unordered_map<int, float> &previousPaint,
    std::unordered_map<int, float> &previousMirrorPaint,
    bool firstPaintDone
);
