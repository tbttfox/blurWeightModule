#ifndef _functions_h
#define _functions_h

#include "enums.h"
// MAYA HEADER FILES:

#include <maya/MBoundingBox.h>
#include <maya/MColorArray.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnIntArrayData.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNurbsSurface.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPointArray.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

const float PI = 3.14159265359f;
const float DEGTORAD = PI / 180.0f;

typedef float coord_t;
typedef std::tuple<coord_t, coord_t, coord_t> point_t;
coord_t distance_sq(const point_t& a, const point_t& b);
coord_t distance(const point_t& a, const point_t& b);
// FUNCTION DECLARATION:
unsigned int getMIntArrayIndex(MIntArray& myArray, int searching);
void CVsAround(int storedU, int storedV, int numCVsInU, int numCVsInV, bool UIsPeriodic,
               bool VIsPeriodic, MIntArray& vertices);
MStatus findSkinCluster(MDagPath MeshPath, MObject& theSkinCluster, int indSkinCluster);
MStatus findNurbsTesselate(MDagPath NurbsPath, MObject& MeshObj);
MStatus findNurbsTesselateOrig(MDagPath meshPath, MObject& origMeshObj);

MStatus findMesh(MObject& theSkinCluster, MDagPath& theMeshPath);
MStatus findOrigMesh(MObject& theSkinCluster, MObject& origMesh);
MStatus getListColorsJoints(MObject& skinCluster, int nbJoints,
                            MIntArray indicesForInfluenceObjects, MColorArray& jointsColors);
MStatus getListLockJoints(MObject& skinCluster, int nbJoints, MIntArray indicesForInfluenceObjects,
                          MIntArray& jointsLocks);
MStatus getListLockVertices(MObject& skinCluster, MIntArray& vertsLocks, MIntArray& lockedIndices);
MStatus getSymetryAttributes(MObject& skinCluster, MIntArray& symetryList);
MStatus getMirrorVertices(MIntArray mirrorVertices, MIntArray& theEditVerts,
                          MIntArray& theMirrorVerts, MIntArray& editAndMirrorVerts,
                          MDoubleArray& editVertsWeights, MDoubleArray& mirrorVertsWeights,
                          MDoubleArray& editAndMirrorWeights, bool doMerge = true);
MStatus editLocks(MObject& skinCluster, MIntArray& vertsToLock, bool addToLock,
                  MIntArray& vertsLocks);
MStatus editArray(ModifierCommands command, int influence, int nbJoints, MIntArray& lockJoints,
                  MDoubleArray& fullWeightArray, std::map<int, double>& valuesToSet,
                  MDoubleArray& theWeights, bool normalize = true, double mutliplier = 1.0);
MStatus editArrayMirror(ModifierCommands command, int influence, int influenceMirror, int nbJoints,
                        MIntArray& lockJoints, MDoubleArray& fullWeightArray,
                        std::map<int, std::pair<float, float>>& valuesToSetMirror,
                        MDoubleArray& theWeights, bool normalize = true, double mutliplier = 1.0);

MStatus setAverageWeight(std::vector<int>& verticesAround, int currentVertex, int indexCurrVert,
                         int nbJoints, MIntArray& lockJoints, MDoubleArray& fullWeightArray,
                         MDoubleArray& theWeights, double strengthVal);
MStatus doPruneWeight(MDoubleArray& theWeights, int nbJoints, double pruneCutWeight);
MStatus transferPointNurbsToMesh(MFnMesh& msh, MFnNurbsSurface& nrbs);

bool RayIntersectsBBox(MPoint minPt, MPoint maxPt, MPoint Orig, MVector dest);

bool bboxIntersection(const MPoint& minPoint, const MPoint& maxPoint, const MMatrix& bbSpace,
                      const MPoint& rayPoint, const MVector& rayVector, MPoint& intersection);

void lineC(short x0, short y0, short x1, short y1, std::vector<std::pair<short, short>>& posi);

float dist2D(short x0, short y0, short x1, short y1);

void getRawNeighbors(const MIntArray& counts, const MIntArray& indices, int numVerts,
                     std::vector<std::unordered_set<int>>& faceNeighbors,
                     std::vector<std::unordered_set<int>>& edgeNeigbors);
void convertToCountIndex(const std::vector<std::unordered_set<int>>& input,
                         std::vector<int>& counts, std::vector<int>& indices);

float pack_float(float x, float y);
int unpack_float(float f, float* x, float* y);

#endif
