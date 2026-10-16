#pragma once

#include "../globals.hpp"

typedef bool(__thiscall* _Init)(CHLClient*, void*, void*, void*, void*);
_Init oInit;

bool __fastcall hkInit(CHLClient* thisptr, void* a2, void* a3, void* a4, void* a5)
{
	// Call original Init first
	bool result = oInit(thisptr, a2, a3, a4, a5);

	// After Init, ClientMode should be initialized
	// Try to get ClientMode from CHLClient VMT (index 10)
	if (!ClientMode)
	{
		uintptr_t clientModeVMTFunc = ((uintptr_t*)thisptr)[10];
		ClientMode = reinterpret_cast<ClientModeShared*>(getAbsAddr(clientModeVMTFunc));

		// If we got ClientMode, install CreateMove hook
		if (ClientMode)
		{
			oCreateMove = VMTHook<_CreateMove>((PVOID**)ClientMode, (PVOID)hkCreateMove, 21);
		}
	}

	return result;
}
