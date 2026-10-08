#include "shader.h"
namespace webd3d8 {
PixelShaderObject::~PixelShaderObject() {}
std::string PixelShaderObject::Generate(const ProgramKey &) const { return ""; }
bool CreateVertexShaderObject(const DWORD *, const DWORD *, VertexShaderObject **) { return false; }
bool CreatePixelShaderObject(const DWORD *, PixelShaderObject **) { return false; }
bool AssembleShader(const char *, size_t, std::vector<DWORD> &, std::string &) { return false; }
std::string BuildTranslatedVertexShader(const VertexShaderObject &, const ProgramKey &) { return ""; }
}
