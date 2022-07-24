#include "Utils.h"
#include "GMFS.h"

using namespace GarrysMod::Lua;

void printLua(ILuaBase* LUA, const char* text)
{
	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "print");
	LUA->PushString(text);
	LUA->Call(1, 0);
	LUA->Pop();
}

void dumpStack(ILuaBase* LUA)
{
	std::string toPrint = "";

	int max = LUA->Top();
	for (int i = 1; i <= max; i++) {
		toPrint += "[" + std::to_string(i) + "] ";
		switch (LUA->GetType(i)) {
		case Type::Angle:
			toPrint += "Angle: (" + std::to_string((int)LUA->GetAngle(i).x) + ", " + std::to_string((int)LUA->GetAngle(i).y) + ", " + std::to_string((int)LUA->GetAngle(i).z) + ")";
			break;
		case Type::Bool:
			toPrint += std::string("Bool: ") + (LUA->GetBool(i) ? std::string("true") : std::string("false"));
			break;
		case Type::Function:
			toPrint += "Function";
			break;
		case Type::Nil:
			toPrint += "nil";
			break;
		case Type::Number:
			toPrint += "Number: " + std::to_string(LUA->GetNumber(i));
			break;
		case Type::String:
			toPrint += "String: " + (std::string)LUA->GetString(i);
			break;
		case Type::Table:
			toPrint += "Table";
			break;
		case Type::Entity:
			toPrint += "Entity";
			break;
		default:
			toPrint += "Unknown";
			break;
		}
		toPrint += "\n";
	}

	printLua(LUA, toPrint.c_str());
}

Vector MakeVector(const float n)
{
	Vector v{};
	v.x = v.y = v.z = n;
	return v;
}

Vector MakeVector(const float x, const float y, const float z)
{
	Vector v{};
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

std::string getMaterialString(ILuaBase* LUA, const std::string& key)
{
	std::string val = "";
	LUA->GetField(-1, "GetString");
	LUA->Push(-2);
	LUA->PushString(key.c_str());
	LUA->Call(2, 1);
	if (LUA->IsType(-1, Type::String)) val = LUA->GetString();
	LUA->Pop();

	return val;
}

bool checkMaterialFlags(ILuaBase* LUA, const MaterialFlags flags)
{
	unsigned int flagVal = 0;
	LUA->GetField(-1, "GetInt");
	LUA->Push(-2);
	LUA->PushString("$flags");
	LUA->Call(2, 1);
	if (LUA->IsType(-1, Type::Number)) flagVal = static_cast<unsigned int>(LUA->GetNumber());
	LUA->Pop();

	return (flagVal & static_cast<unsigned int>(flags)) == static_cast<unsigned int>(flags);
}

glm::vec3 transformToBone(
	const Vector& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	const std::vector<std::pair<size_t, float>>& weights,
	const bool angleOnly
)
{
	glm::vec4 final(0.f);
	glm::vec4 vertex = glm::vec4(vec.x, vec.y, vec.z, angleOnly ? 0.f : 1.f);
	for (size_t i = 0U; i < weights.size(); i++) {
		final += bones[weights[i].first] * binds[weights[i].first] * vertex * weights[i].second;
	}
	return glm::vec3(final);
}

bool readTexture(const std::string& path, VTFTexture** ppTextureOut)
{
	std::string texturePath = "materials/" + path + ".vtf";
	if (!FileSystem::Exists(texturePath.c_str(), "GAME")) return false;
	FileHandle_t file = FileSystem::Open(texturePath.c_str(), "rb", "GAME");

	uint32_t filesize = FileSystem::Size(file);
	uint8_t* data = reinterpret_cast<uint8_t*>(malloc(filesize));
	if (data == nullptr) return false;

	FileSystem::Read(data, filesize, file);
	FileSystem::Close(file);

	VTFTexture* pTexture = new VTFTexture{ data, filesize };
	free(data);

	if (!pTexture->IsValid()) {
		delete pTexture;
		return false;
	}

	*ppTextureOut = pTexture;
	return true;
}

bool validVector(const glm::vec3& v)
{
	return (
		!(v.x == 0.f && v.y == 0.f && v.z == 0.f) &&
		(v.x == v.x && v.y == v.y && v.z == v.z) &&
		!(isinf(v.x) || isinf(v.y) || isinf(v.z))
	);
}
