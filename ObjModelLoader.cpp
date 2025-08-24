#include "ObjModelLoader.h"
#define TINYOBJLOADER_IMPLEMENTATION // add this to exactly 1 of your C++ files
#include "tiny_obj_loader.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

#include <iostream>

#include <memory>
#include <vector>
#include <PANDUVector3.h>
#include <PANDUVector2.h>

std::unique_ptr<ObjModelLoader::ModelData> ReadObjFile(std::istream& filestream);

ObjModelLoader::ObjModelLoader(const std::string& FilePath)
	: m_ModelFilePath(FilePath)
{
 
}

ObjModelLoader::~ObjModelLoader()
{

}




std::future<std::unique_ptr<const ObjModelLoader::ModelData>> ObjModelLoader::Load()
{
#ifdef __EMSCRIPTEN__
    auto promise = std::make_shared<std::promise<std::unique_ptr<const ModelData>>>();
    auto future = promise->get_future();

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;

    struct FetchData
    {
        std::shared_ptr<std::promise<std::unique_ptr<const ModelData>>> promise;
        std::string filePath;
    };

    auto fetchData = new FetchData{ promise, m_ModelFilePath };
    attr.userData = fetchData;

    const std::string filePath = m_ModelFilePath;

    attr.onsuccess = [](emscripten_fetch_t* fetch) {
        auto* data = static_cast<FetchData*>(fetch->userData);

        std::string objContent(fetch->data, fetch->numBytes);
        std::istringstream objStream(objContent);

        if (!objStream)
        {
            std::cerr << "Obj file load failed : " << std::endl;
        }

        auto RetVal = ReadObjFile(objStream);
        data->promise->set_value(std::move(RetVal));

        emscripten_fetch_close(fetch);
        delete data;
    };

    attr.onerror = [](emscripten_fetch_t* fetch) {
        auto* data = static_cast<FetchData*>(fetch->userData);
        data->promise->set_value(nullptr);
        emscripten_fetch_close(fetch);
        delete data;
    };

    emscripten_fetch(&attr, m_ModelFilePath.c_str());
    return future;

#else
    // Native async load
    return std::async(std::launch::async, [filePath = this->m_ModelFilePath]() -> std::unique_ptr<const ModelData> {
        

        std::ifstream ifs(filePath);
        if (!ifs)
        {
            return nullptr;
        }

        //tinyobj::MaterialFileReader matReader(".");

        return ReadObjFile(ifs);
    });
#endif
}

std::unique_ptr<ObjModelLoader::ModelData> ReadObjFile(std::istream& filestream)
{
    struct VertexKey
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;

        inline bool nearlyEqual(float a, float b, float eps = 1e-8f) const { return std::fabs(a - b) < eps; }

        bool isEqualPosition(const VertexKey& o) const { return nearlyEqual(px, o.px) && nearlyEqual(py, o.py) && nearlyEqual(pz, o.pz); }
        bool isEqualNormal(const VertexKey& o) const { return nearlyEqual(nx, o.nx) && nearlyEqual(ny, o.ny) && nearlyEqual(nz, o.nz); }
        bool isEqualUv(const VertexKey& o) const { return nearlyEqual(u, o.u) && nearlyEqual(v, o.v); }

        bool operator == (const VertexKey& o) const
        {
            return isEqualPosition(o) && isEqualNormal(o) && isEqualUv(o);
        }

        bool compare(const VertexKey& o) const
        {
            if (!nearlyEqual(px, o.px))
                return px < o.px;

            if (!nearlyEqual(py, o.py))
                return py < o.py;

            if (!nearlyEqual(pz, o.pz))
                return pz < o.pz;

            if (!nearlyEqual(nx, o.nx))
                return nx < o.nx;

            if (!nearlyEqual(ny, o.ny))
                return ny < o.ny;

            if (!nearlyEqual(nz, o.nz))
                return nz < o.nz;

            if (!nearlyEqual(u, o.u))
                return u < o.u;

            return v < o.v;
        }

        bool operator < (const VertexKey& o) const
        {
            return compare(o);
        }

    };

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &filestream))
    {
        auto model = std::make_unique<ObjModelLoader::ModelData>();

        std::map<VertexKey, uint32_t> uniqueVerts;

        for (const auto& shape : shapes)
        {
            for (const auto& idx : shape.mesh.indices)
            {
                Pandu::Vector3 position(0, 0, 0), normal(0, 0, 1);
                Pandu::Vector2 uv(0, 0);

                if (idx.vertex_index >= 0) position = { attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2] };
                if (idx.normal_index >= 0)  normal = { attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };

                if (idx.texcoord_index >= 0) uv = { attrib.texcoords[2 * idx.texcoord_index + 0], attrib.texcoords[2 * idx.texcoord_index + 1] };

                VertexKey key{ position.x, position.y, position.z, normal.x, normal.y, normal.z, uv.x, uv.y };

                auto it = uniqueVerts.find(key);
                if (it == uniqueVerts.end())
                {
                    uint32_t newIndex = static_cast<uint32_t>(model->positions.size() / 3);
                    uniqueVerts[key] = newIndex;

                    model->positions.push_back(position.x);
                    model->positions.push_back(position.y);
                    model->positions.push_back(position.z);

                    model->normals.push_back(normal.x);
                    model->normals.push_back(normal.y);
                    model->normals.push_back(normal.z);

                    model->texcoords.push_back(uv.x);
                    model->texcoords.push_back(uv.y);

                    model->indices.push_back(newIndex);
                }
                else
                {
                    // Already created
                    model->indices.push_back(it->second);
                }
            }
        }

        return model;
    }

    return nullptr;
}