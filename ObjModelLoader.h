#ifndef __ObjModelLoader_h__
#define __ObjModelLoader_h__

#include <string>
#include <future>
#include <memory>

class ObjModelLoader
{
public:

	struct ModelData
	{
		std::vector<float> positions;
		std::vector<float> normals;
		std::vector<float> texcoords;
		std::vector<unsigned int> indices;
	};

	ObjModelLoader(const std::string& FilePath);
	virtual ~ObjModelLoader();

	ObjModelLoader(const ObjModelLoader&) = delete;
	ObjModelLoader& operator = (const ObjModelLoader&) = delete;

	ObjModelLoader(const ObjModelLoader&&) = delete;
	ObjModelLoader& operator = (const ObjModelLoader&&) = delete;

	std::future<std::unique_ptr<const ModelData>> Load();

private:

	const std::string m_ModelFilePath;
};

#endif //__ObjModelLoader_h__
