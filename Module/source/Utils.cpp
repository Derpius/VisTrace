#include "Utils.h"

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
			toPrint += "Bool: " + LUA->GetBool(i);
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
