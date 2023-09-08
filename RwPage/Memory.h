#pragma once

namespace Memory
{
	// 内存管理器的主要虚拟地址
	inline PVOID MainVirtualAddress;

	// 内存管理器的主要页表项
	inline PTE* MainPageEntry;

	// 初始化内存管理器
	NTSTATUS Init();

	// 覆盖指定物理地址对应的页，并返回虚拟地址
	PVOID OverwritePage(ULONG64 physicalAddress);

	// 读取物理地址中的数据
	NTSTATUS ReadPhysicalAddress(ULONG64 targetAddress, PVOID buffer, SIZE_T size);

	// 写入数据到物理地址
	NTSTATUS WritePhysicalAddress(ULONG64 targetAddress, PVOID buffer, SIZE_T size);

	// 将线性地址转换为物理地址
	ULONG64 TranslateLinearAddress(ULONG64 directoryTableBase, ULONG64 virtualAddress);

	// 获取进程的目录基址
	ULONG64 GetProcessDirectoryBase(PEPROCESS inputProcess);

	// 读取进程中的内存数据
	NTSTATUS ReadProcessMemory(PEPROCESS process, ULONG64 address, PVOID buffer, SIZE_T size);

	// 向进程中写入内存数据
	NTSTATUS WriteProcessMemory(PEPROCESS process, ULONG64 address, PVOID buffer, SIZE_T size);

	// 复制进程内存数据
	NTSTATUS CopyProcessMemory(PEPROCESS sourceProcess, PVOID sourceAddress, PEPROCESS targetProcess, PVOID targetAddress, SIZE_T bufferSize);
}