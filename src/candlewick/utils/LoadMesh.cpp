#include "LoadMesh.h"
#include "MeshData.h"

#include <SDL3/SDL_log.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>

namespace candlewick {

MeshData loadAiMesh(const aiMesh *inMesh, const aiMatrix4x4 transform) {
  using Vertex = MeshData::Vertex;
  using IndexType = MeshData::IndexType;
  const Uint32 expectedFaceSize = 3;

  std::vector<Vertex> vertexData;
  std::vector<IndexType> indexData;
  vertexData.resize(inMesh->mNumVertices);
  indexData.resize(inMesh->mNumFaces * expectedFaceSize);

  for (Uint32 vertex_id = 0; vertex_id < inMesh->mNumVertices; vertex_id++) {
    aiVector3D pos = inMesh->mVertices[vertex_id];
    pos = transform * pos;
    Vertex &vertex = vertexData[vertex_id];
    vertex.pos = Float3::Map(&pos.x);

    if (inMesh->HasNormals()) {
      aiVector3D n_ = inMesh->mNormals[vertex_id];
      n_ = aiMatrix3x3(transform) * n_;
      vertex.normal = Float3::Map(&n_.x);
    }
  }

  for (Uint32 face_id = 0; face_id < inMesh->mNumFaces; face_id++) {
    const aiFace &f = inMesh->mFaces[face_id];
    for (Uint32 ii = 0; ii < f.mNumIndices; ii++) {
      indexData[face_id * expectedFaceSize + ii] = f.mIndices[ii];
    }
  }
  return MeshData{SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, std::move(vertexData),
                  std::move(indexData)};
}

LoadMeshReturn loadSceneMeshes(const char *path,
                               std::vector<MeshData> &meshData) {

  ::Assimp::Importer import;
  // remove point primitives
  import.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                            aiPrimitiveType_LINE | aiPrimitiveType_POINT);
  unsigned int pFlags = aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                        aiProcess_GenSmoothNormals | aiProcess_SortByPType |
                        aiProcess_JoinIdenticalVertices |
                        aiProcess_GenUVCoords | aiProcess_RemoveComponent |
                        aiProcess_FindDegenerates |
                        aiProcess_ImproveCacheLocality;
  import.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, true);
  const aiScene *scene = import.ReadFile(path, pFlags);
  if (!scene) {
    SDL_Log("%s: Warning: Failed to load resource. %s", __FUNCTION__,
            import.GetErrorString());
    return LoadMeshReturn::FailedToLoad;
  }

  if (!scene->HasMeshes())
    return LoadMeshReturn::NoMeshes;

  aiMatrix4x4 transform = scene->mRootNode->mTransformation;
  meshData.resize(scene->mNumMeshes);
  for (std::size_t i = 0; i < scene->mNumMeshes; i++) {
    aiMesh *inMesh = scene->mMeshes[i];
    meshData[i] = loadAiMesh(inMesh, transform);
    Uint32 materialId = inMesh->mMaterialIndex;
    if (scene->HasMaterials()) {
      aiMaterial *material = scene->mMaterials[materialId];
      meshData[i].material = loadFromAssimpMaterial(material);
    }
  }

  return LoadMeshReturn::OK;
}

} // namespace candlewick
