#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MFloatMatrix.h>
#include <maya/MMatrix.h>
#include <maya/MIntArray.h>
#include <maya/MItMeshEdge.h>

#include <vector>
#include <span>
#include <array>
#include <unordered_map>
#include <algorithm>


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






class MeshData{

private:
    const MDagPath &dag;
    MFnMesh meshFn;

    int numFaces; // The number of faces on the current mesh
    int numEdges; // The number of edges on the current mesh
    int numVertices; // The number of vertices on the current mesh
    int numNormals; // The number of vertices on the current mesh
    MMatrix inclusiveMatrix;  // The worldspace matrix of this mesh
    MMatrix inclusiveMatrixInverse; // The inverse worldspace matrix of this mesh

    FlatChunks<float, 3> origPoints;
    FlatChunks<float, 3> rawPoints;
    FlatChunks<float, 3> rawNormals;
    FlatCounts<int> pvFaces;  // x[vertIdx] -> [list-of-faceIdxs]
    FlatCounts<int> pvEdges;  // x[vertIdx] -> [list-of-edgeIdxs]
    FlatCounts<int> pvVerts;  // x[vertIdx] -> [list-of-vertIdxs that share a face with the input]
    FlatCounts<int> pfVerts;  // x[faceIdx] -> [list-of-vertIdxs]
    FlatChunks<int> peVerts;  // x[edgeIdx] -> [pair-of-vertIdxs]
    DoubleChunks<int> pftVerts;  // x(face, triIdx) -> [list_of_vertIdxs]


public:
    MeshData(MDagPath &indag): dag(indag){
        MStatus status;

        meshFn.setObject(dag.node());
        inclusiveMatrix = dag.inclusiveMatrix();
        inclusiveMatrixInverse = dag.inclusiveMatrixInverse();
        numVertices = meshFn.numVertices();
        numEdges = meshFn.numEdges();
        numFaces = meshFn.numPolygons();
        numFaces = meshFn.numPolygons();
        numNormals = meshFn.numNormals();
        rawPoints.set(meshFn.getRawPoints(&status), numVertices);
        rawNormals.set(meshFn.getRawNormals(&status), numNormals);

    }

    // Explicitly delete the copy constructor since we're storing references
    MeshData(const MeshData&) = delete;

};





struct MeshData_OLD {
    // Holds unchanging variables related to the mesh
    // like the quick accessors and octree

    MDagPath meshDag; // The MDagPath pointing to a mesh
    MFnMesh meshFn; // The Mesh Functionset for this mesh
    int numFaces; // The number of faces on the current mesh
    int numEdges; // The number of edges on the current mesh
    int numVertices; // The number of vertices on the current mesh

    MMeshIsectAccelParams accelParams; // Octree for speeding up raycasting
    MFloatMatrix inclusiveMatrix;  // The worldspace matrix of this mesh
    MFloatMatrix inclusiveMatrixInverse; // The inverse worldspace matrix of this mesh

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


