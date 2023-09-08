#include "General.h"

// 定义 ActiveProcessLinks 的偏移量
#define OFFSET_ActiveProcessLinks 0x448

// 查找指定进程并获取其主模块基址的函数
EXTERN_C PEPROCESS FindProcess(const wchar_t* executableName, PVOID* mainModuleBaseAddress)
{
    // 获取当前进程
    PEPROCESS currentProcess = IoGetCurrentProcess();

    // 获得当前进程的 ActiveProcessLinks 地址
    PLIST_ENTRY list = reinterpret_cast<PLIST_ENTRY>(reinterpret_cast<PCHAR>(currentProcess) + OFFSET_ActiveProcessLinks);

    // 遍历进程链表
    for (; list->Flink != reinterpret_cast<PLIST_ENTRY>(reinterpret_cast<PCHAR>(currentProcess) + OFFSET_ActiveProcessLinks); list = list->Flink)
    {
        // 获取目标进程
        PEPROCESS targetProcess = reinterpret_cast<PEPROCESS>(reinterpret_cast<PCHAR>(list->Flink) - OFFSET_ActiveProcessLinks);

        // 获取目标进程的 PEB（Process Environment Block）地址
        PPEB pebAddress = PsGetProcessPeb(targetProcess);
        if (!pebAddress)
            continue;

        // 复制目标进程的 PEB 结构体
        PEB pebLocal = { 0 };
        Memory::CopyProcessMemory(targetProcess, pebAddress, currentProcess, &pebLocal, sizeof(PEB));

        // 复制目标进程的 PEB_LDR_DATA 结构体
        PEB_LDR_DATA loaderData = { 0 };
        Memory::CopyProcessMemory(targetProcess, pebLocal.Ldr, currentProcess, &loaderData, sizeof(PEB_LDR_DATA));

        // 遍历模块链表
        PLIST_ENTRY currentListEntry = loaderData.InMemoryOrderModuleList.Flink;
        while (true)
        {
            // 复制当前模块的 LIST_ENTRY 结构体
            LIST_ENTRY listEntryLocal = { 0 };
            Memory::CopyProcessMemory(targetProcess, currentListEntry, currentProcess, &listEntryLocal, sizeof(LIST_ENTRY));
            if (loaderData.InMemoryOrderModuleList.Flink == listEntryLocal.Flink || !listEntryLocal.Flink)
                break;

            // 获取当前模块的 LDR_DATA_TABLE_ENTRY 地址
            PLDR_DATA_TABLE_ENTRY entryAddress = CONTAINING_RECORD(currentListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

            // 复制当前模块的 LDR_DATA_TABLE_ENTRY 结构体
            LDR_DATA_TABLE_ENTRY entryLocal = { 0 };
            Memory::CopyProcessMemory(targetProcess, entryAddress, currentProcess, &entryLocal, sizeof(LDR_DATA_TABLE_ENTRY));

            // 复制当前模块的模块路径名
            UNICODE_STRING modulePathStr = { 0 };
            wchar_t moduleName[256];
            Memory::CopyProcessMemory(targetProcess, entryLocal.BaseDllName.Buffer, currentProcess, moduleName, min(entryLocal.BaseDllName.Length, 256));

            modulePathStr.Buffer = moduleName;
            modulePathStr.Length = min(entryLocal.BaseDllName.Length, 256);
            modulePathStr.MaximumLength = min(entryLocal.BaseDllName.MaximumLength, 256);

            // 初始化目标模块名
            UNICODE_STRING moduleNameStr = { 0 };
            RtlInitUnicodeString(&moduleNameStr, executableName);

            // 比较模块路径名和目标模块名
            LONG compare = RtlCompareUnicodeString(&modulePathStr, &moduleNameStr, TRUE);
            if (compare == 0)
            {
                // 如果匹配成功，则将主模块基址存储在指定的变量中，并返回目标进程指针
                *mainModuleBaseAddress = entryLocal.DllBase;
                return targetProcess;
            }

            currentListEntry = listEntryLocal.Flink;
        }
    }

    return nullptr;
}


// 驱动程序入口函数
EXTERN_C NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
	UNREFERENCED_PARAMETER(driverObject);
	UNREFERENCED_PARAMETER(registryPath);

	// 打印入口日志
	Log("从地址 0x%p 处调用 Entry 函数，当前进程的 PID 为 %u", _ReturnAddress(), PsGetCurrentProcessId());

	// 初始化内存操作函数
	NTSTATUS status = Memory::Init();
	// 如果初始化失败，直接返回错误状态
	if (!NT_SUCCESS(status))
		return status;

	// 定义目标进程和模块的基址变量
	PVOID moduleBase = NULL;
	PEPROCESS targetProcess = FindProcess(L"notepad.exe", &moduleBase);
	// 如果找不到目标进程或者模块基址为空，返回未找到的错误状态
	if (!targetProcess || !moduleBase)
		return STATUS_NOT_FOUND;

	// 获取目标进程的 PID，并打印日志
	HANDLE processId = PsGetProcessId(targetProcess);
	Log("notepad.exe 的 PID 为 %u，基址为 0x%p", processId, moduleBase);

	// 获取主模块的头部值，并打印日志
	ULONG64 header = 0;
	// 调用内存操作函数，将目标进程的主模块头部值复制到当前进程中
	Memory::CopyProcessMemory(targetProcess, moduleBase, IoGetCurrentProcess(), &header, sizeof(ULONG64));

	Log("主模块的头部值为 0x%p", header);

	return STATUS_SUCCESS;
}
