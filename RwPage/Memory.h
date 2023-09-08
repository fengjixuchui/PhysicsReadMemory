#pragma once

namespace Memory
{
	// �ڴ����������Ҫ�����ַ
	inline PVOID MainVirtualAddress;

	// �ڴ����������Ҫҳ����
	inline PTE* MainPageEntry;

	// ��ʼ���ڴ������
	NTSTATUS Init();

	// ����ָ�������ַ��Ӧ��ҳ�������������ַ
	PVOID OverwritePage(ULONG64 physicalAddress);

	// ��ȡ�����ַ�е�����
	NTSTATUS ReadPhysicalAddress(ULONG64 targetAddress, PVOID buffer, SIZE_T size);

	// д�����ݵ������ַ
	NTSTATUS WritePhysicalAddress(ULONG64 targetAddress, PVOID buffer, SIZE_T size);

	// �����Ե�ַת��Ϊ�����ַ
	ULONG64 TranslateLinearAddress(ULONG64 directoryTableBase, ULONG64 virtualAddress);

	// ��ȡ���̵�Ŀ¼��ַ
	ULONG64 GetProcessDirectoryBase(PEPROCESS inputProcess);

	// ��ȡ�����е��ڴ�����
	NTSTATUS ReadProcessMemory(PEPROCESS process, ULONG64 address, PVOID buffer, SIZE_T size);

	// �������д���ڴ�����
	NTSTATUS WriteProcessMemory(PEPROCESS process, ULONG64 address, PVOID buffer, SIZE_T size);

	// ���ƽ����ڴ�����
	NTSTATUS CopyProcessMemory(PEPROCESS sourceProcess, PVOID sourceAddress, PEPROCESS targetProcess, PVOID targetAddress, SIZE_T bufferSize);
}