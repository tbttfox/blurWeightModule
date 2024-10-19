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


struct MeshData {
    // Holds unchanging variables related to the mesh
    // like the quick accessors and octree

    MDagPath meshDag; // The MDagPath pointing to a mesh
    MFnMesh meshFn; // The Mesh Functionset for this mesh
    MMeshIsectAccelParams accelParams; // Octree for speeding up raycasting
    MFloatMatrix inclusiveMatrix;  // The worldspace matrix of this mesh
    MFloatMatrix inclusiveMatrixInverse; // The inverse worldspace matrix of this mesh

    int numFaces; // The number of faces on the current mesh
    int numEdges; // The number of edges on the current mesh
    int numVertices; // The number of vertices on the current mesh
    float* mayaOrigRawPoints; // The flattened point positions of the UNDEFORMED mesh
    float* mayaRawPoints; // The flattened point positions of the mesh
    float* rawNormals; // The flattened per-face-vertex normals

    MIntArray verticesNormalsIndices; // takes input of vertex index, produces output of normal index
    MVectorArray verticesNormals; // The per-vertex local space normals of the mesh (already used verticesNormalIndices to reorder so verts/normals are matched per index)

    std::vector<MIntArray> perVertexFaces; // The face indices for each vertex
    std::vector<MIntArray> perVertexEdges; // The edge indices for each vertex
    std::vector<std::vector<MIntArray>> perFaceTriangleVertices; // Somehow get the triangle indices per face
    std::vector<std::pair<int, int>> perEdgeVertices; // The endpoint vertex indices of each edge

    // A values/counts array pair for the vertices belonging to each face
    std::vector<int> perFaceVerticesSetFLAT;
    std::vector<int> perFaceVerticesSetINDEX;

    // A values/counts array pair for the vertices neighboring each vertex
    std::vector<int> perVertexVerticesSetFLAT;
    std::vector<int> perVertexVerticesSetINDEX;
};


struct NurbsData {
    int numCVsInU_; // The U dimensions of CVs when painting nurbs
    int numCVsInV_; // The V dimensions of CVs when painting nurbs
    MDagPath nurbsDag;  // The dagpath to the nurbs object
    MFnNurbsSurface nurbsFn; // The nurbs funciton set to the dagpath
};


struct InfluenceData {
    // Stores the joints (and mirror data)
    // including their colors and locks
    int nbJoints; // The number of influences

    MIntArray influenceIndices; // Just range(len(nbJoints)) ... so maybe unneeded
    MIntArray indicesForInfluenceObjects; // Map between logical and physical indices
    MIntArray inflNamePixelSize; // A 2*N length array where adjacent pairs are the wid/height of the influence name in pixels
    MStringArray inflNames; // The names of the influences
    MColorArray jointsColors; // The color of each influence

    MColor lockJntColor; // The color to draw when the joint is locked
    MIntArray lockJoints; // 0 where the joint is unlocked. 1 where the joint is locked
    MIntArray ignoreLockJoints; // An array of all "false" that gets passed to functions instead of `lockJoins` when `ignoreLockVal` is set

    MDagPathArray inflDagPaths; // An Array holding dag paths to all the influence objects
};


struct WeightData {
    // Stores the per-vertex weights, and relationships between the verts
    // As well as the colors
    // MeshData generally won't change, weightData will, that's why this is separate

    MIntArray lockVertices;  // The vertices that are currently locked
    MDoubleArray skinWeightList; // The full weights list for the skin

    MColorArray multiCurrentColors; // The color per-vertex for rainbow coloring

    MColorArray soloCurrentColors; // The color per-vertex for solo coloring
    MDoubleArray soloColorsValues; // The raw values to set when solo-coloring. Easier for the math

    MColor lockVertColor; // The color to draw locked verts

    std::unordered_map<int, std::pair<float, float>> mirroredJoinedArray; // The base and mirrored wieghts per painted vertex

    MObject skinObj; // The skin cluster MObject
};


struct UserInputData {
    // The user options from the UI
    MColor colorVal; // -colorR -colorG -colorB // The color of the brush circle
    int curveVal;  // -curve // The falloff curve value
    bool drawBrushVal; // -drawBrush // Whether to draw the brush value to the screen for setting size/weight
    bool drawRangeVal; // -drawRange // Whether to draw the range of the brusn when painting (??)
    MString moduleImportString; // -importPython // Where is the python module for this tool
    MString enterToolCommandVal; // -enterToolCommand // A mel command to run when entering the tool
    MString exitToolCommandVal; // -exitToolCommand // A mel command to run when exiting the tool
    bool ignoreLockVal; // -ignoreLock // Whether to ignore the lock value when painting
    int lineWidthVal; // The width of the line drawn to screen
    bool messageVal; // Show the usage message at the top of the screen
    double oversamplingVal; // Value to scale the *effect* of the brush falloff by. Basically, make the falloffs quicker or slower
    double fractionOversamplingVal; // Whether to take the oversampling val into account (THIS IS A TERRIBLE NAME!)
    double sizeVal; // -size // The current size of the brush
    double smoothStrengthVal; // -smoothStrength // The current smoothing strength value
    double strengthVal; // -strength // the current strength value
    int undersamplingVal; // -undersampling // The number of drag-steps between evaluation when MMB dragging
    bool volumeVal; // -volume // whether to draw the volume range of the brush
    double mirrorMinDist; // -toleranceMirror //The tolerance for finding the mirrored vertices
    MIntArray mirrorInfluences; // -mirrorInfluences // A list of influences in-order of the skincluster that are the mirrors at the same index
    int paintMirror; // -mirrorPaint // The mirror behavior index
    bool useColorSetsWhilePainting; // -useColorSetsWhilePainting // Whether to use colorsets while painting
    bool drawTriangles;  // -meshdrawTriangles // Whether to draw Triangles
    bool drawEdges;  // -meshdrawEdges // Whether to draw Edges
    bool drawPoints;  // -meshdrawPoints // Whether to draw points
    bool drawTransparency; // -meshdrawTransparency // Whether to draw transparency
    double minSoloColor;  // -minColor // The minimum DISPLAY value for nonzero solo colors
    double maxSoloColor;  // -maxColor // The maximum DISPLAY value for nonzero solo colors
    ModifierCommands commandIndex;  // -commandIndex // The command index to run
    int smoothRepeat;  // -smoothRepeat // The number of iterations of smooth while dragging
    int soloColorVal; // -soloColor // Whether we color solo, or we color rainbow (SHOULD BE BOOL)
    int soloColorTypeVal; // The "enum" of the color type for soloing (lava, color, black/white (stuff like that))
    bool pickMaxInfluenceVal; // -pickMaxInfluence // Whether we're picking the largest influence of the verts under the mouse
    bool pickInfluenceVal; // -pickInfluence // Whether to pick the influence based on the influence object bounding box
    bool postSetting; // -postSetting // whether to set the weights every loop, or wait until mouse release
    ModifierKeys smoothModifier; // -shiftSmooths // What key smooths
    ModifierKeys removeModifier; // -shiftSmooths // What key removes
    int influenceIndex;  // -influenceIndex // The current influence index we're painting to

    // NEVER SET
    // double rangeVal;  // -range // A multiplier on the adjustment.  Unused, hard-coded to 0.5
    // bool coverageVal; // -coverage // Unused. Hard-coded to true
    // bool doNormalize;  // Whether to normalize the weights while painting // Unused. Hard-coded to true

    // UNUSED
    // double interactiveValue
    // double interactiveValue1
    // double interactiveValue2
};


struct drawingDeformers {
    MMatrix mat;
    MPoint center;
    MPoint minPt;
    MPoint maxPt;
    MVector up, right;
    double width, height, depth;
};


struct InteractionStartData {
    // Octrees for our current paint objects
    // I should maybe use the BVH library to speed up the volume selection process
    MMeshIntersector intersectorOrigShape;
    MMeshIntersector intersector;

    // The X and Y screen positions where the drag started for MMB dragging
    short startScreenX;
    short startScreenY;

    // The center of the view (informs where we draw the size on screen)
    short viewCenterX;
    short viewCenterY;

    std::vector<drawingDeformers> BBoxOfDeformers; // The vector of bounding boxes of all the deformers

    // Store the initial surface point and view vector to use when
    // the brush settings are adjusted because the brush circle
    // needs to be static during the adjustment.
    MFloatPoint surfacePointAdjust;
    MVector worldVectorAdjust;
};


struct InteractionPersistentData {
    MDoubleArray skinWeightsForUndo; // An array to hold the original skin weights for undoing ... I think
    MDoubleArray fullUndoSkinWeightList;  // An array to hold the whole skin weights list for undoing ... I think
    std::set<int> verticesPainted; // The full set of vertices that are currently being painted
    int previousfaceHit; //The index of the face that was hit previously
    bool performBrush; // Whether we should actually `doTheAction()` on drag ... ie, whether we actually hit on the drag

    // When MMB dragging to change size/strangth, don't actually draw every frame. Draw every N frames. This value keeps track of where we are in that loop
    // Look at undersamplingVal for "N"
    int undersamplingSteps;

    // Switch if the size should get adjusted or the strength based on the drag direction. A drag along the x axis defines size
    // and a drag along the y axis defines strength. InitAdjust makes sure that direction gets set on the first
    // drag event and gets reset the next time a mouse button is pressed.
    bool initAdjust;
    bool sizeAdjust; // Whether we're adjusting size (true) or strength (false)

    // Store the current drag distance when shift is prssed while MMB Dragging, That way we can fine-adjust from the current value
    // and not some global value that snaps when we press/release shift
    double storedDistance;

    double adjustValue; // Store the modified value for drawing and for setting the values when releasing the mouse button.

    MString pickedInfluence; // The name of the currently picked influence

    int biggestInfluence; // Storage of the biggest influence index on mouseover

    bool firstPaintDone; // Keep track of if we're done ... I think.  This one's weird.  Probably keeping track of if we close out unexpectedly or somethign
    MStatus pressStatus; // Store the status of the initial press, so if it fails, we can skip doing the drag stuff

    MStringArray orderedIndicesByWeights; // An array to hold the list of names of the influences ordered by weights
    MStringArray orderedIndicesByWeightsVals; // An array to hold the list of indices of the influences ordered by weights
};


struct InteractionPerFrameData {
    // All of the interaction data that we get every single frame
    // Some of it may be useful to us the next frame, so we'll store that in the persistent data struct

    ModifierKeys modifierNoneShiftControl;
    bool shiftMiddleDrag; // whether we're holding shift when dragging for fine-adjust scaling
    bool isNurbs; // whether we're painting on NURBS ... I think.  This may go in interaction data, or mesh data

    // Color set names (probably static, maybe unused)
    MString fullColorSet = MString("multiColorsSet");
    MString soloColorSet = MString("soloColorsSet");
    MString fullColorSet2 = MString("multiColorsSet2");
    MString soloColorSet2 = MString("soloColorsSet2");

    // From (pseudo-code) view.viewToWorld(event.getPosition())
    MVector worldVector; // The worldspace click ray direction
    MPoint worldPoint; // The worldspace click ray origin point

    int faceHit; // The index of the face that was hit
    int triHit; // The index of the triangle in the face that was hit ... (added by TFox)
    MFloatPoint hitPoint; // The 3d point that was hit by the click-ray
    MFloatPoint origHitPoint;  // Translate the barycentric coordinates of the deformed hit into the rest-space hit (for mirroring)
    MVector normalVector;  // The normal at the hit
    float pressDistance; // The parametric distance of the hit... hitPoint = raySource + (hitRayParam * rayDirection)

    // Hold the current screenspace x/y values
    short screenX;
    short screenY;
};






struct MirrorableData {
    // Need 2 copies of this struct.  One for regular paint, one for mirror
    MFloatPointArray AllHitPoints; // The currently hit points
    MFloatPoint centerOfBrush; // Where we should draw the brush in worldspace
    MFloatPoint inMatrixHit; // Where we should apply the brush in localspace
    std::unordered_map<int, float> skinValuesToSet;  // A map of [vert index: weight value] of the weights we're currently setting
    std::unordered_map<int, float> previousPaint;  // storage for the previous paint dist-from-hit-per-vertex
    std::vector<float> intensityValuesOrig;  // The intensity values for the paint. This is the intensity of the brush stroke

    // Not *quite* sure what this is for. I think it stores the previous paint distances as some sort of optimization? I may have to ask about this
    std::unordered_map<int, float> dicVertsDistSTART;
    bool successfullHit; // Whether the press hit was successful or not
    bool successFullDragHit; // Whether the drag had a successful hit or not
};


struct ProvidedByMaya {
    // Storage for the stuff provided by maya ... just a temp storage place, not really for *real* use later
    MHWRender::MUIDrawManager drawManager;
    M3dView view;
    MEvent event;
};

