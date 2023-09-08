#pragma once

namespace Utils
{
	// 将物理地址转换为虚拟地址
	PVOID PhysicalToVirtual(ULONG64 address);
}