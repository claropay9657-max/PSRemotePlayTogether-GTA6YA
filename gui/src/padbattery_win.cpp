// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "padbattery.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <devpropdef.h>

#include <cwchar>
#include <vector>

#ifndef CM_GETIDLIST_FILTER_PRESENT
#define CM_GETIDLIST_FILTER_PRESENT 0x00000100
#endif

namespace {

// Spelled out instead of taken from devpkey.h, which only defines them with INITGUID
// DEVPKEY_Device_FriendlyName
const DEVPROPKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
// DEVPKEY_Bluetooth_Battery: the charge Windows shows under Bluetooth & devices
const DEVPROPKEY kBluetoothBattery = { { 0x104ea319, 0x6ee2, 0x4701, { 0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5 } }, 2 };

int Ask()
{
	const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
	const wchar_t *enumerator = L"BTHLE";
	ULONG size = 0;
	if(CM_Get_Device_ID_List_SizeW(&size, enumerator, flags) != CR_SUCCESS || size == 0)
		return -1;
	std::vector<wchar_t> ids(size + 1, L'\0');
	if(CM_Get_Device_ID_ListW(enumerator, ids.data(), size, flags) != CR_SUCCESS)
		return -1;
	int lowest = -1;
	for(const wchar_t *id = ids.data(); *id; id += std::wcslen(id) + 1)
	{
		// The controller itself (BTHLE\DEV_<address>), not one of its services
		if(_wcsnicmp(id, L"BTHLE\\DEV_", 10) != 0)
			continue;
		DEVINST node = 0;
		if(CM_Locate_DevNodeW(&node, const_cast<DEVINSTID_W>(id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
			continue;
		wchar_t name[256] = {};
		DEVPROPTYPE type = 0;
		ULONG bytes = sizeof(name) - sizeof(wchar_t);
		if(CM_Get_DevNode_PropertyW(node, &kFriendlyName, &type, reinterpret_cast<PBYTE>(name), &bytes, 0) != CR_SUCCESS
			|| type != DEVPROP_TYPE_STRING)
			continue;
		// "Xbox Wireless Controller", "Xbox Elite Wireless Controller", "Xbox Adaptive Controller"
		if(!std::wcsstr(name, L"Xbox"))
			continue;
		BYTE level = 0;
		bytes = sizeof(level);
		if(CM_Get_DevNode_PropertyW(node, &kBluetoothBattery, &type, &level, &bytes, 0) != CR_SUCCESS
			|| type != DEVPROP_TYPE_BYTE || level > 100)
			continue;
		if(lowest < 0 || level < lowest)
			lowest = level;
	}
	return lowest;
}

} // namespace

int padbattery::XboxBluetooth()
{
	static ULONGLONG asked_ms = 0;
	static int answer = -1;
	const ULONGLONG now = GetTickCount64();
	if(asked_ms != 0 && now - asked_ms < 10000)
		return answer;
	asked_ms = now;
	answer = Ask();
	return answer;
}
