#pragma once

#include "GarrysMod/Lua/Interface.h"

// Print a string to console using Lua
void printLua(GarrysMod::Lua::ILuaBase* LUA, const char* text);

// Prints the type of every element on the stack to the console
void dumpStack(GarrysMod::Lua::ILuaBase* LUA);
