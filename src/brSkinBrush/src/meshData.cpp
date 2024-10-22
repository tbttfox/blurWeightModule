#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MFloatMatrix.h>
#include <maya/MMatrix.h>
#include <maya/MIntArray.h>
#include <maya/MItMeshEdge.h>

#include <vector>
#include <span>
#include <array>
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

    void set(const MIntArray &inCounts, const MIntArray &inVals){
        offsets.clear();
        values.clear();
        values = inVals;

        offsets.resize(inCounts.length() + 1);
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
    void set(const MIntArray &counts, const MIntArray &flattris){
        values.clear();
        values.reserve(flattris.length());
        for (size_t i=0; i<flattris.length(); ++i){
            values.push_back(flattris[i]);
        }

        offsets.clear();
        offsets.reserve(counts.length() + 1);
        offsets.push_back(0);
        size_t v = 0;
        for (size_t i=0; i<counts.length(); ++i){
            v += counts[i];
            offsets.push_back(v);
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

    FlatChunks<float, 3> origPoints; // The undeformed mesh points
    FlatChunks<float, 3> rawPoints; // The possibly deformed mesh points
    FlatChunks<float, 3> rawNormals; // The undeformed per-face-vert normals
    FlatCounts<int> pvFaces;  // x[vertIdx] -> [span-of-faceIdxs] faces that connect to the input vert
    FlatCounts<int> pvEdges;  // x[vertIdx] -> [span-of-edgeIdxs] edges that connect to the input vert
    FlatCounts<int> pvVerts;  // x[vertIdx] -> [span-of-vertIdxs] verts that share a face with the input vert
    FlatCounts<int> pfVerts;  // x[faceIdx] -> [span-of-vertIdxs] verts that are part of the input face
    FlatChunks<int> peVerts;  // x[edgeIdx] -> [2span-of-vertIdxs] verts that are part of the input edge
    DoubleChunks<int> pftVerts;  // x(face, triIdx) -> [3span-of-vertIdxs] verts that are part of the tri at the inputs

public:
    MeshData(MDagPath &indag): dag(indag){
        MStatus status;

        meshFn.setObject(dag.node());
        inclusiveMatrix = dag.inclusiveMatrix();
        inclusiveMatrixInverse = dag.inclusiveMatrixInverse();
        numVertices = meshFn.numVertices();
        numEdges = meshFn.numEdges();
        numFaces = meshFn.numPolygons();
        numNormals = meshFn.numNormals();
        rawPoints.set(meshFn.getRawPoints(&status), numVertices);
        rawNormals.set(meshFn.getRawNormals(&status), numNormals);

        // Get the data from the mesh and dag path
        MIntArray counts, flatFaces, triangleCounts, triangleVertices;

        meshFn.getVertices(counts, flatFaces);
        pfVerts.set(counts, flatFaces);

        meshFn.getTriangles(triangleCounts, triangleVertices);
        pftVerts.set(triangleCounts, triangleVertices);

        // Get the face/vert correlations
        std::vector<std::vector<int>> perVertexFaces;
        perVertexFaces.resize(numVertices);
        for (unsigned int faceId = 0; faceId < numFaces; ++faceId) {
            for (int i = 0; i < counts[faceId]; ++i) {
                perVertexFaces[flatFaces[i]].push_back(faceId);
            }
        }
        pvFaces.set(perVertexFaces);

        // Get the edge/vert correlations
        std::vector<int> perEdgeVertices;
        std::vector<std::vector<int>> perVertexEdges;
        perVertexEdges.resize(numVertices);
        perEdgeVertices.resize(numEdges * 2);
        MItMeshEdge edgeIter(dag);
        for (unsigned i = 0; !edgeIter.isDone(); edgeIter.next(), ++i) {
            int pt0Index = edgeIter.index(0);
            int pt1Index = edgeIter.index(1);
            perVertexEdges[pt0Index].push_back(i);
            perVertexEdges[pt1Index].push_back(i);
            perEdgeVertices[2 * i] = pt0Index;
            perEdgeVertices[2 * i + 1] = pt1Index;
        }
        pvEdges.set(perVertexEdges);
        peVerts.set(&(perEdgeVertices[0]), numEdges);

        // Build the face-growing neighbors
        std::vector<std::vector<int>> perVertexVertices;
        perVertexVertices.resize(numVertices);
        #pragma omp parallel for
        for (int vertIdx = 0; vertIdx < numVertices; ++vertIdx) {
            std::vector<int> &toAdd = perVertexVertices[vertIdx];
            for (int faceIdx: pvFaces[vertIdx]){
                // I don't know whether the span should be a reference
                const std::span<const int> faceVerts = pfVerts[faceIdx];
                toAdd.insert(toAdd.end(), faceVerts.begin(), faceVerts.end());
            }
            // For such a short vec, sorting then erasing is the fastest
            std::sort(toAdd.begin(), toAdd.end());
            toAdd.erase(std::unique(toAdd.begin(), toAdd.end()), toAdd.end());
        }
        pvVerts.set(perVertexVertices);

    }

    // Explicitly delete the copy constructor since we're storing references
    // to the dag path and (consequently) the mesh function set
    MeshData(const MeshData&) = delete;
};

