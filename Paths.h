#ifndef __Paths_h__
#define __Paths_h__

#include <string>

class Paths
{
public:

	static std::string GetActualFilePath(const std::string& RelativeFilePath)
	{
#ifdef __EMSCRIPTEN__
		return std::string("./") + RelativeFilePath;
#else
		return std::string("./../") + RelativeFilePath;
#endif
	}
};

#endif //__Paths_h__
