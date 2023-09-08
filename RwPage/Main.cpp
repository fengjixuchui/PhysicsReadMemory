#include "General.h"

// ���� ActiveProcessLinks ��ƫ����
#define OFFSET_ActiveProcessLinks 0x448

// ����ָ�����̲���ȡ����ģ���ַ�ĺ���
EXTERN_C PEPROCESS FindProcess(const wchar_t* executableName, PVOID* mainModuleBaseAddress)
{
    // ��ȡ��ǰ����
    PEPROCESS currentProcess = IoGetCurrentProcess();

    // ��õ�ǰ���̵� ActiveProcessLinks ��ַ
    PLIST_ENTRY list = reinterpret_cast<PLIST_ENTRY>(reinterpret_cast<PCHAR>(currentProcess) + OFFSET_ActiveProcessLinks);

    // ������������
    for (; list->Flink != reinterpret_cast<PLIST_ENTRY>(reinterpret_cast<PCHAR>(currentProcess) + OFFSET_ActiveProcessLinks); list = list->Flink)
    {
        // ��ȡĿ�����
        PEPROCESS targetProcess = reinterpret_cast<PEPROCESS>(reinterpret_cast<PCHAR>(list->Flink) - OFFSET_ActiveProcessLinks);

        // ��ȡĿ����̵� PEB��Process Environment Block����ַ
        PPEB pebAddress = PsGetProcessPeb(targetProcess);
        if (!pebAddress)
            continue;

        // ����Ŀ����̵� PEB �ṹ��
        PEB pebLocal = { 0 };
        Memory::CopyProcessMemory(targetProcess, pebAddress, currentProcess, &pebLocal, sizeof(PEB));

        // ����Ŀ����̵� PEB_LDR_DATA �ṹ��
        PEB_LDR_DATA loaderData = { 0 };
        Memory::CopyProcessMemory(targetProcess, pebLocal.Ldr, currentProcess, &loaderData, sizeof(PEB_LDR_DATA));

        // ����ģ������
        PLIST_ENTRY currentListEntry = loaderData.InMemoryOrderModuleList.Flink;
        while (true)
        {
            // ���Ƶ�ǰģ��� LIST_ENTRY �ṹ��
            LIST_ENTRY listEntryLocal = { 0 };
            Memory::CopyProcessMemory(targetProcess, currentListEntry, currentProcess, &listEntryLocal, sizeof(LIST_ENTRY));
            if (loaderData.InMemoryOrderModuleList.Flink == listEntryLocal.Flink || !listEntryLocal.Flink)
                break;

            // ��ȡ��ǰģ��� LDR_DATA_TABLE_ENTRY ��ַ
            PLDR_DATA_TABLE_ENTRY entryAddress = CONTAINING_RECORD(currentListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

            // ���Ƶ�ǰģ��� LDR_DATA_TABLE_ENTRY �ṹ��
            LDR_DATA_TABLE_ENTRY entryLocal = { 0 };
            Memory::CopyProcessMemory(targetProcess, entryAddress, currentProcess, &entryLocal, sizeof(LDR_DATA_TABLE_ENTRY));

            // ���Ƶ�ǰģ���ģ��·����
            UNICODE_STRING modulePathStr = { 0 };
            wchar_t moduleName[256];
            Memory::CopyProcessMemory(targetProcess, entryLocal.BaseDllName.Buffer, currentProcess, moduleName, min(entryLocal.BaseDllName.Length, 256));

            modulePathStr.Buffer = moduleName;
            modulePathStr.Length = min(entryLocal.BaseDllName.Length, 256);
            modulePathStr.MaximumLength = min(entryLocal.BaseDllName.MaximumLength, 256);

            // ��ʼ��Ŀ��ģ����
            UNICODE_STRING moduleNameStr = { 0 };
            RtlInitUnicodeString(&moduleNameStr, executableName);

            // �Ƚ�ģ��·������Ŀ��ģ����
            LONG compare = RtlCompareUnicodeString(&modulePathStr, &moduleNameStr, TRUE);
            if (compare == 0)
            {
                // ���ƥ��ɹ�������ģ���ַ�洢��ָ���ı����У�������Ŀ�����ָ��
                *mainModuleBaseAddress = entryLocal.DllBase;
                return targetProcess;
            }

            currentListEntry = listEntryLocal.Flink;
        }
    }

    return nullptr;
}


// ����������ں���
EXTERN_C NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
	UNREFERENCED_PARAMETER(driverObject);
	UNREFERENCED_PARAMETER(registryPath);

	// ��ӡ�����־
	Log("�ӵ�ַ 0x%p ������ Entry ��������ǰ���̵� PID Ϊ %u", _ReturnAddress(), PsGetCurrentProcessId());

	// ��ʼ���ڴ��������
	NTSTATUS status = Memory::Init();
	// �����ʼ��ʧ�ܣ�ֱ�ӷ��ش���״̬
	if (!NT_SUCCESS(status))
		return status;

	// ����Ŀ����̺�ģ��Ļ�ַ����
	PVOID moduleBase = NULL;
	PEPROCESS targetProcess = FindProcess(L"notepad.exe", &moduleBase);
	// ����Ҳ���Ŀ����̻���ģ���ַΪ�գ�����δ�ҵ��Ĵ���״̬
	if (!targetProcess || !moduleBase)
		return STATUS_NOT_FOUND;

	// ��ȡĿ����̵� PID������ӡ��־
	HANDLE processId = PsGetProcessId(targetProcess);
	Log("notepad.exe �� PID Ϊ %u����ַΪ 0x%p", processId, moduleBase);

	// ��ȡ��ģ���ͷ��ֵ������ӡ��־
	ULONG64 header = 0;
	// �����ڴ������������Ŀ����̵���ģ��ͷ��ֵ���Ƶ���ǰ������
	Memory::CopyProcessMemory(targetProcess, moduleBase, IoGetCurrentProcess(), &header, sizeof(ULONG64));

	Log("��ģ���ͷ��ֵΪ 0x%p", header);

	return STATUS_SUCCESS;
}
