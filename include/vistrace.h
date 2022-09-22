#pragma once

#include "GarrysMod/Lua/Interface.h"

#include "vistrace/IRenderTarget.h"
#include "vistrace/ISampler.h"

namespace VisTrace
{
	constexpr const char* API_VERSION = "0.12";

	namespace VType
	{
		int VTFTexture = -1;
		int RenderTarget = -1;
		int Sampler = -1;
	};
}

#define VISTRACE_EXTENSION_OPEN(ExtensionName)                                          \
void vt_extension_open__Imp(GarrysMod::Lua::ILuaBase* LUA);                             \
int vt_extension_open(lua_State* L)                                                     \
{                                                                                       \
	GarrysMod::Lua::ILuaBase* LUA = L->luabase;                                         \
	LUA->SetState(L);                                                                   \
                                                                                        \
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_REG);                                      \
	LUA->GetField(-1, "VisTraceVTFTexture_id");                                         \
	if (LUA->IsType(-1, GarrysMod::Lua::Type::Number))                                  \
		VisTrace::VType::VTFTexture = LUA->GetNumber();                                 \
	LUA->Pop();                                                                         \
                                                                                        \
	LUA->GetField(-1, "VisTraceRT_id");                                                 \
	if (LUA->IsType(-1, GarrysMod::Lua::Type::Number))                                  \
		VisTrace::VType::RenderTarget = LUA->GetNumber();                               \
	LUA->Pop();                                                                         \
                                                                                        \
	LUA->GetField(-1, "Sampler_id");                                                    \
	if (LUA->IsType(-1, GarrysMod::Lua::Type::Number))                                  \
		VisTrace::VType::Sampler = LUA->GetNumber();                                    \
	LUA->Pop();                                                                         \
                                                                                        \
	vt_extension_open__Imp(LUA);                                                        \
	return 0;                                                                           \
}                                                                                       \
DLL_EXPORT int gmod13_open(lua_State* L)                                                \
{                                                                                       \
	GarrysMod::Lua::ILuaBase* LUA = L->luabase;                                         \
                                                                                        \
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_REG);                                      \
	LUA->GetField(-1, "VisTrace.Extensions");                                           \
	if (!LUA->IsType(-1, GarrysMod::Lua::Type::Table)) {                                \
		LUA->Pop();                                                                     \
		LUA->CreateTable();                                                             \
		LUA->SetField(-2, "VisTrace.Extensions");                                       \
		LUA->GetField(-1, "VisTrace.Extensions");                                       \
	}                                                                                   \
                                                                                        \
	LUA->GetField(-1, #ExtensionName);                                                  \
	if (LUA->IsType(-1, GarrysMod::Lua::Type::Table))                                   \
		LUA->ThrowError("VisTrace extension " #ExtensionName " is already registered"); \
	LUA->Pop();                                                                         \
                                                                                        \
	LUA->CreateTable();                                                                 \
	LUA->PushCFunction(vt_extension_open);                                              \
	LUA->SetField(-2, "Open");                                                          \
	LUA->PushString(VisTrace::API_VERSION);                                             \
	LUA->SetField(-2, "Version");                                                       \
                                                                                        \
	LUA->SetField(-2, #ExtensionName);                                                  \
	LUA->Pop(2);                                                                        \
                                                                                        \
	return 0;                                                                           \
}                                                                                       \
void vt_extension_open__Imp(GarrysMod::Lua::ILuaBase* LUA)

#define VISTRACE_EXTENSION_CLOSE()                           \
void vt_extension_close__Imp(GarrysMod::Lua::ILuaBase* LUA); \
DLL_EXPORT int gmod13_close(lua_State* L)                    \
{                                                            \
	vt_extension_close__Imp(L->luabase);                     \
	return 0;                                                \
}                                                            \
void vt_extension_close__Imp(GarrysMod::Lua::ILuaBase* LUA)
