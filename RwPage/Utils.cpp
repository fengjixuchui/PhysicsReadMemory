#include "General.h"

// �������ַת��Ϊ�����ַ
PVOID Utils::PhysicalToVirtual(ULONG64 address)
{
	PHYSICAL_ADDRESS physical;
	physical.QuadPart = address;
	return MmGetVirtualForPhysical(physical);
}