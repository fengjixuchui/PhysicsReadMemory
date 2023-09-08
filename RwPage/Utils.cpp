#include "General.h"

// 将物理地址转换为虚拟地址
PVOID Utils::PhysicalToVirtual(ULONG64 address)
{
	PHYSICAL_ADDRESS physical;
	physical.QuadPart = address;
	return MmGetVirtualForPhysical(physical);
}