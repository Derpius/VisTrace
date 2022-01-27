#include "AccelStruct.h"
#include "Utils.h"

#include "filesystem.h"
#include "GarrysMod/InterfacePointers.hpp"

#include "VTFParser.h"

using namespace GarrysMod::Lua;

static int AccelStruct_id;
static IFileSystem* pFileSystem;

LUA_FUNCTION(AccelStruct_gc)
{
	LUA->CheckType(1, AccelStruct_id);
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	LUA->SetUserType(1, NULL);
	delete pAccelStruct;

	return 0;
}

/*
	table[Entity] entities = {}

	returns AccelStruct
*/
LUA_FUNCTION(CreateAccel)
{
	AccelStruct* pAccelStruct = new AccelStruct(pFileSystem);

	if (LUA->Top() == 0) LUA->CreateTable();
	else if (LUA->IsType(1, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(1, Type::Table);
		LUA->Pop(LUA->Top() - 1); // Pop all but the table
	}
	pAccelStruct->PopulateAccel(LUA);

	LUA->PushUserType_Value(pAccelStruct, AccelStruct_id);
	return 1;
}

/*
	AccelStruct   accel
	table[Entity] entities = {}
*/
LUA_FUNCTION(RebuildAccel)
{
	LUA->CheckType(1, AccelStruct_id);
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	if (LUA->Top() == 1) LUA->CreateTable();
	else if (LUA->IsType(2, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(2, Type::Table);
		LUA->Pop(LUA->Top() - 2); // Pop all but the table (and self)
	}
	pAccelStruct->PopulateAccel(LUA);

	return 0;
}

/*
	AccelStruct accel
	Vector      origin
	Vector      direction
	float       tMin = 0
	float       tMax = FLT_MAX
	bool        hitWorld = true
	bool        hitWater = false

	returns modified TraceResult struct (https://wiki.facepunch.com/gmod/Structures/TraceResult)
*/
LUA_FUNCTION(TraverseScene)
{
	LUA->CheckType(1, AccelStruct_id);
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	pAccelStruct->Traverse(LUA);

	return 1; // Return the table at the top of the stack (this will either be a default TraceResult, a TraceResult populated by BVH intersection, or a TraceResult populated by world intersection)
}

LUA_FUNCTION(AccelStruct_tostring)
{
	LUA->PushString("AccelStruct");
	return 1;
}

GMOD_MODULE_OPEN()
{
	pFileSystem = InterfacePointers::FileSystem();
	if (pFileSystem == nullptr) LUA->ThrowError("Failed to get filesystem");

	AccelStruct_id = LUA->CreateMetaTable("AccelStruct");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(AccelStruct_gc);
		LUA->SetField(-2, "__gc");
		LUA->PushCFunction(RebuildAccel);
		LUA->SetField(-2, "Rebuild");
		LUA->PushCFunction(TraverseScene);
		LUA->SetField(-2, "Traverse");
		LUA->PushCFunction(AccelStruct_tostring);
		LUA->SetField(-2, "__tostring");
	LUA->Pop();

	LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
			LUA->PushCFunction(CreateAccel);
			LUA->SetField(-2, "CreateAccel");
		LUA->SetField(-2, "vistrace");
	LUA->Pop();

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	return 0;
}