#include "Utils.h"
#include "GMFS.h"

#include <cmath>

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

IMaterialVar* GetMaterialVar(IMaterial* mat, const std::string& key) {
	bool found = false;

	IMaterialVar* result = mat->FindVar(key.c_str(), &found, false);
	return found ? result : nullptr;
}

std::string GetMaterialString(IMaterial* mat, const std::string& key)
{
	IMaterialVar* var = GetMaterialVar(mat, key);
	return var == nullptr ? "" : var->GetStringValue();
}

bool ValidVector(const glm::vec3& v)
{
	return (
		!(v.x == 0.f && v.y == 0.f && v.z == 0.f) &&
		(v.x == v.x && v.y == v.y && v.z == v.z) &&
		!(std::isinf(v.x) || std::isinf(v.y) || std::isinf(v.z))
	);
}
