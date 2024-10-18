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

    int numFaces; // The number of faces on the current mesh
    int numEdges; // The number of edges on the current mesh
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


struct NurbsData {
    int numCVsInU_;  // The U and V dimensions of CVs when painting nurbs
    int numCVsInV_;
    MDagPath nurbsDag;
    MFnNurbsSurface nurbsFn;
};




struct InfluenceData {
    // Stores the joints (and mirror data)
    // including their colors and locks
    int nbJoints; // The number of influences

    MIntArray mirrorInfluences;

    MIntArray influenceIndices;

    MIntArray indicesForInfluenceObjects; // ??? what is this?
    MStringArray inflNames;
    MColorArray jointsColors;

    MColor lockJntColor;
    MIntArray ignoreLockJoints; // ??? what is this
    MIntArray lockJoints;
};


struct WeightData {
    // Stores the per-vertex weights, and relationships between the verts
    // As well as the colors
    // MeshData generally won't change, weightData will, that's why this is separate
    MIntArray lockVertices;
    MDoubleArray skinWeightList;

    MColorArray multiEditColors;
    MColorArray multiCurrentColors;
    MColorArray soloEditColors;
    MColorArray soloCurrentColors;
    MDoubleArray soloColorsValues; // The raw values to set when solo-coloring. Easier for the math

    MColor lockVertColor;

    MColor multColor;
    MColor soloColor;
    std::unordered_map<int, std::pair<float, float>> mirroredJoinedArray;

    MObject skinObj; // The skin cluster MObject

};

struct UserInputData {
    // The user options from the UI
    int influenceIndex;
    double maxSoloColor;
    double minSoloColor;
    int soloColorTypeVal;
    int paintMirror; // The mirror behavior index
    int soloColorVal; // The solo color index
    int curveVal;  // The falloff curve value

    bool drawTransparency; // Whether to draw transparency
    bool drawPoints;  // Whether to draw points
    bool drawTriangles;  // Whether to draw Triangles
    bool drawEdges;  // Whether to draw Edges

    bool coverageVal;
    double sizeVal;
    double smoothStrengthVal;

    double mirrorMinDist;
    int undersamplingVal;  // The number of drag-steps between evaluation when MMB dragging

    double rangeVal;
    double strengthVal;
    double fractionOversamplingVal;
    double oversamplingVal;
    int smoothRepeat;  // The number of iterations of smooth while dragging

    bool isNurbs; // whether we're painting on NURBS ... I think.  This may go in interaction data, or mesh data

    bool postSetting; // whether to set the weights every loop, or wait until mouse release

    bool ignoreLockVal;  // Whether to ignore the lock value when painting
    bool doNormalize;  // Whether to normalize the weights while painting??

    bool drawBrushVal; // Whether to draw the brush value to the screen for setting size/weight

    // Color set names (probably static, maybe unused)
    bool useColorSetsWhilePainting; // Whether to use colorsets while painting
    bool toggleColorState; // Whether to use the colorset or colorset2. Basically buffered rendering of the colors
    MString fullColorSet = MString("multiColorsSet");
    MString soloColorSet = MString("soloColorsSet");
    MString fullColorSet2 = MString("multiColorsSet2");
    MString soloColorSet2 = MString("soloColorsSet2");


};

struct InteractionData {
    // All of the current data for where the mouse is, and what its interacting with
    // and the current keyboard options

    // This chould probably be split into "data needed for current paint and can be discarded"
    // and "data needed for next paint and needs to be stored"

    // Also, could probably find the data that's mirrored, and split between mirrored/unmirrored

    // And this part of the keys and commands should probably be its own thing as well
    ModifierCommands commandIndex;
    ModifierKeys modifierNoneShiftControl;
    ModifierKeys smoothModifier; // Constant
    ModifierKeys removeModifier; // Constant
    bool shiftMiddleDrag; // whether we're holding shift when dragging for fine-adjust scaling

    MFloatPointArray AllHitPoints; // The currently hit points
    MFloatPointArray AllHitPointsMirror; // and the mirror

    std::unordered_map<int, float> dicVertsDist; // A dictionary of index and distance

    // From (pseudo-code) view.viewToWorld(event.getPosition())
    MVector worldVector; // The worldspace click ray direction
    MPoint worldPoint; // The worldspace click ray starting point

    int faceHit; // The index of the face that was hit
    int triHit; // The index of the triangle in the face that was hit ... (added by TFox)
    MFloatPoint hitPoint; // The 3d point that was hit by the click-ray
    MFloatPoint origHitPoint;  // Translate the barycentric coordinates of the deformed hit into the rest-space hit for mirroring
    MVector normalVector;  // The normal at the hit
    MMeshIntersector intersectorOrigShape;
    MMeshIntersector intersector;

    MFloatPoint centerOfBrush; // Where we should draw the brush in worldspace
    MFloatPoint centerOfMirrorBrush; // Where we should draw the mirror brush in worldspace

    MFloatPoint inMatrixHit; // Where we should apply the brush in localspace
    MFloatPoint inMatrixHitMirror; // and for the mirror

    float pressDistance; // The parametric distance of the hit... hitPoint = raySource + (hitRayParam * rayDirection)

    bool getNormal;  // Whether to get the normal of the hit. This changes over interaction
    MDoubleArray skinWeightsForUndo; // An array to hold the original skin weights for undoing ... I think

    std::unordered_map<int, float> skinValuesToSet;  // A map of [vert index: weight value] of the weights we're currently setting
    std::unordered_map<int, float> skinValuesMirrorToSet;  // The same as above for mirroring
    std::set<int> verticesPainted; // The full set of vertices that are currently being painted

    std::unordered_map<int, float> previousPaint;  // storage for the previous paint dist-from-hit-per-vertex
    std::unordered_map<int, float> previousMirrorPaint;  // and for the mirror paint

    std::vector<float> intensityValuesOrig;  // The intensity values for the non-mirrored paint. This is the intensity of the brush stroke
    std::vector<float> intensityValuesMirror;  // and for the mirror

    // Not *quite* sure what these are for. I think it stores the previous paint distances as some sort of optimization?
    // I may have to ask about these
    std::unordered_map<int, float> dicVertsDistSTART;
    std::unordered_map<int, float> dicVertsMirrorDistSTART;

    // Whether the press hit was successful or not (and mirror)
    bool successfullHit;
    bool successfullMirrorHit;

    // Whether the drag had a successful hit or not (and mirror
    bool successFullDragHit;
    bool successFullDragMirrorHit;

    int previousfaceHit; //The index of the face that was hit previously
    bool performBrush; // Whether we should actually `doTheAction()` on drag ... ie, whether we actually hit on the drag


    // When MMB dragging to change size/strangth, don't actually draw every frame. Draw every N frames. This value keeps track of where we are in that loop
    // Look at undersamplingVal for "N"
    int undersamplingSteps;

    // The X and Y screen positions where the drag started for MMB dragging
    short startScreenX;
    short startScreenY;

    // The center of the view (informs where we draw the size on screen)
    short viewCenterX;
    short viewCenterY;

    // Switch if the size should get adjusted or the strength based on the drag direction. A drag along the x axis defines size
    // and a drag along the y axis defines strength. InitAdjust makes sure that direction gets set on the first
    // drag event and gets reset the next time a mouse button is pressed.
    bool initAdjust;
    bool sizeAdjust; // Whether we're adjusting size (true) or strength (false)

    // Store the current drag distance when shift is prssed while MMB Dragging, That way we can fine-adjust from the current value
    // and not some global value that snaps when we press/release shift
    double storedDistance;

    // Store the modified value for drawing and for setting the values when releasing the mouse button.
    double adjustValue;

    // The name of the currently picked influence
    MString pickedInfluence;

};

struct ProvidedByMaya {
    // Storage for the stuff provided by maya ... just a temp storage place, not really for *real* use later
    MHWRender::MUIDrawManager drawManager;
    M3dView view;
    MEvent event;
};












MStatus doPressCommon(
    bool pickMaxInfluenceVal,
    bool pickInfluenceVal,
    std::vector<drawingDeformers> BBoxOfDeformers,
    int biggestInfluence,
    MDoubleArray &fullUndoSkinWeightList,
    MDoubleArray &paintArrayValues,
    short screenX,
    short screenY,
    unsigned int width,
    unsigned int height,
    MVector &normalMirroredVector,
    MFloatPoint surfacePointAdjust,
    MVector worldVectorAdjust,
    MString &pickedInfluence,
);

MStatus doReleaseCommon(
    bool pickMaxInfluenceVal,
    bool pickInfluenceVal,
    bool refreshDone,
    MStatus &pressStatus,
);

MStatus getListLockJoints(
    MObject& skinCluster,
    MIntArray& jointsLocks
);

void doTheAction(
    MDoubleArray &fullUndoSkinWeightList,
    bool firstPaintDone
);
