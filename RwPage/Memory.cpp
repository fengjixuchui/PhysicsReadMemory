#include "General.h"

// �����ض����� 4996���þ����ʾ ExAllocatePool ����������
#pragma warning(disable : 4996)

// ��ʼ���ڴ������
NTSTATUS Memory::Init()
{
    PHYSICAL_ADDRESS maxAddress;
    maxAddress.QuadPart = MAXULONG64;

    // �����������ڴ�ҳ
    MainVirtualAddress = MmAllocateContiguousMemory(PAGE_SIZE, maxAddress);
    if (!MainVirtualAddress)
        return STATUS_INSUFFICIENT_RESOURCES;

    VIRTUAL_ADDRESS virtualAddress;
    virtualAddress.Pointer = MainVirtualAddress;

    PTE_CR3 cr3;
    cr3.Value = __readcr3();

    // ��ȡPML4��������ַ
    PML4E* pml4 = static_cast<PML4E*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(cr3.Pml4)));
    PML4E* pml4e = (pml4 + virtualAddress.Pml4Index);

    // ���PML4������ڣ��򷵻ش���״̬��
    if (!pml4e->Present)
        return STATUS_NOT_FOUND;

    // ��ȡPDPT��������ַ
    PDPTE* pdpt = static_cast<PDPTE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pml4e->Pdpt)));
    PDPTE* pdpte = pdpt + virtualAddress.PdptIndex;

    // ���PDPT������ڣ��򷵻ش���״̬��
    if (!pdpte->Present)
        return STATUS_NOT_FOUND;

    // ����Ƿ�Ϊ1GB��ҳ
    if (pdpte->PageSize)
        return STATUS_INVALID_PARAMETER;

    // ��ȡPD��������ַ
    PDE* pd = static_cast<PDE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pdpte->Pd)));
    PDE* pde = pd + virtualAddress.PdIndex;

    // ���PD������ڣ��򷵻ش���״̬��
    if (!pde->Present)
        return STATUS_NOT_FOUND;

    // ����Ƿ�Ϊ2MB��ҳ
    if (pde->PageSize)
        return STATUS_INVALID_PARAMETER;

    // ��ȡPT��������ַ
    PTE* pt = static_cast<PTE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pde->Pt)));
    PTE* pte = pt + virtualAddress.PtIndex;

    // ���PT������ڣ��򷵻ش���״̬��
    if (!pte->Present)
        return STATUS_NOT_FOUND;

    MainPageEntry = pte;

    return STATUS_SUCCESS;
}

// ����ָ�������ַ��Ӧ��ҳ
PVOID Memory::OverwritePage(
    ULONG64 physicalAddress   // �����ַ����ʾ����д��ҳ�����ʼ�����ַ
)
{
    // ҳ�߽�����Read/WriteProcessMemory�������
    // ����ҳ������ɢ�ڲ�ͬ��ҳ��

    // ���������ַ����ҳ��ƫ����
    ULONG pageOffset = physicalAddress % PAGE_SIZE;

    // ���������ַ����ҳ����ʼ�����ַ
    ULONG64 pageStartPhysical = physicalAddress - pageOffset;

    // ��ҳ�����PageFrame�ֶθ���ΪĿ��ҳ������ҳ��
    MainPageEntry->PageFrame = PAGE_TO_PFN(pageStartPhysical);

    // ˢ��ָ�������ַ����Ӧ��TLB����
    __invlpg(MainVirtualAddress);

    // ���ظ��Ǻ���ڴ��ַ
    return (PVOID)((ULONG64)MainVirtualAddress + pageOffset);
}


// ��ȡ�����ַ�е�����
NTSTATUS Memory::ReadPhysicalAddress(
    ULONG64 targetAddress,   // Ŀ�������ַ����ʾ����ȡ���ݵ������ڴ��ַ
    PVOID buffer,            // Ŀ�����ݻ������ĵ�ַ�����ڴ洢��ȡ������
    SIZE_T size              // ����ȡ���ڴ��С
)
{
    // ���������ַ��Ӧ��ҳ�����ظ��Ǻ�������ַ
    PVOID virtualAddress = OverwritePage(targetAddress);

    // �����Ǻ�������ַ�е����ݸ��Ƶ�Ŀ�����ݻ�������
    memcpy(buffer, virtualAddress, size);

    return STATUS_SUCCESS;
}


// д�����ݵ������ַ
NTSTATUS Memory::WritePhysicalAddress(
    ULONG64 targetAddress,   // Ŀ�������ַ����ʾ��д�����ݵ������ڴ��ַ
    PVOID buffer,            // Դ���ݻ������ĵ�ַ������д�������
    SIZE_T size              // ��д����ڴ��С
)
{
    // ���������ַ��Ӧ��ҳ�����ظ��Ǻ�������ַ
    PVOID virtualAddress = OverwritePage(targetAddress);

    // ��Դ���ݻ������е����ݸ��Ƶ����Ǻ�������ַ��
    memcpy(virtualAddress, buffer, size);

    return STATUS_SUCCESS;
}


#define PAGE_OFFSET_SIZE 12    // ҳƫ�ƴ�С����ʾҳ�ڵ�ƫ����λ��

// �����ַ���룬�������������ַ�е�ƫ�����͵�λ��������λ�������ַ
static const ULONG64 PMASK = (~0xfull << 8) & 0xfffffffffull;


// �����Ե�ַת��Ϊ�����ַ�����Ե�ַ:����ʹ�õĵ�ַ�ռ���߼���ַ��
ULONG64 Memory::TranslateLinearAddress(
    ULONG64 directoryTableBase,   // ҳ��Ŀ¼��ַ����ʾ����������Ե�ַ���ڵ�ҳ��Ŀ¼�Ļ�ַ
    ULONG64 virtualAddress        // ����������Ե�ַ
)
{
    directoryTableBase &= ~0xf;    // ���ҳ��Ŀ¼��ַ�ĵ�4λ��������λ��ҳ��Ŀ¼��ַ

    ULONG64 pageOffset = virtualAddress & ~(~0ul << PAGE_OFFSET_SIZE);
    // �������Ե�ַ��ҳ��ƫ��������ȡ���Ե�ַ�ĵ�12λ��ҳƫ�ƴ�СΪ12λ��

    ULONG64 pte = ((virtualAddress >> 12) & (0x1ffll));
    // ��ȡҳ����������ͨ������12λ�õ�ԭʼ������Ȼ��ͨ��λ�����ȡ��9λ��ҳ��������ռ9λ��

    ULONG64 pt = ((virtualAddress >> 21) & (0x1ffll));
    // ��ȡҳ��Ŀ¼��������ͨ������21λ�õ�ԭʼ������Ȼ��ͨ��λ�����ȡ��9λ��ҳ��Ŀ¼������ռ9λ��

    ULONG64 pd = ((virtualAddress >> 30) & (0x1ffll));
    // ��ȡҳ��Ŀ¼������ͨ������30λ�õ�ԭʼ������Ȼ��ͨ��λ�����ȡ��9λ��ҳ��Ŀ¼����ռ9λ��

    ULONG64 pdp = ((virtualAddress >> 39) & (0x1ffll));
    // ��ȡҳ��Ŀ¼ָ��������ͨ������39λ�õ�ԭʼ������Ȼ��ͨ��λ�����ȡ��9λ��ҳ��Ŀ¼ָ������ռ9λ��

    ULONG64 pdpe = 0;
    ReadPhysicalAddress(directoryTableBase + 8 * pdp, &pdpe, sizeof(pdpe));
    // ��ȡ�����ַ�е�ҳ��Ŀ¼ָ����洢��pdpe������

    if (~pdpe & 1)
        return 0;   // ҳ��Ŀ¼ָ������Ч������0��ʾת��ʧ��

    ULONG64 pde = 0;
    ReadPhysicalAddress((pdpe & PMASK) + 8 * pd, &pde, sizeof(pde));
    // ��ȡ�����ַ�е�ҳ��Ŀ¼��洢��pde������

    if (~pde & 1)
        return 0;   // ҳ��Ŀ¼����Ч������0��ʾת��ʧ��

    // 1GB��ҳ��ʹ��pde��12-34λ��Ϊƫ��
    if (pde & 0x80)
        return (pde & (~0ull << 42 >> 12)) + (virtualAddress & ~(~0ull << 30));
    // �ж��Ƿ�Ϊ1GB��ҳ������ǣ���pde�е�12-34λ��Ϊƫ�����������Ե�ַ�ĵ�30λ���кϲ����õ������ַ

    ULONG64 pteAddr = 0;
    ReadPhysicalAddress((pde & PMASK) + 8 * pt, &pteAddr, sizeof(pteAddr));
    // ��ȡ�����ַ�е�ҳ����ָ�룬�洢��pteAddr������

    if (~pteAddr & 1)
        return 0;   // ҳ������Ч������0��ʾת��ʧ��

    // 2MB��ҳ
    if (pteAddr & 0x80)
        return (pteAddr & PMASK) + (virtualAddress & ~(~0ull << 21));
    // �ж��Ƿ�Ϊ2MB��ҳ������ǣ���pteAddr������PMASK����λ��������õ������ַ

    virtualAddress = 0;
    ReadPhysicalAddress((pteAddr & PMASK) + 8 * pte, &virtualAddress, sizeof(virtualAddress));
    // ��ȡ�����ַ�е�ҳ����洢��virtualAddress������

    virtualAddress &= PMASK;
    // ͨ��������PMASK����λ����������ε���λ��������λ�������ַ

    if (!virtualAddress)
        return 0;   // δ�ҵ���Ч��ҳ�������0��ʾת��ʧ��

    return virtualAddress + pageOffset;
    // ���������ַ�����������ַ�ĸ�λ�������ַ�ϲ���������ҳ��ƫ����
}

// ��ȡ���̵�Ŀ¼��ַ
ULONG64 Memory::GetProcessDirectoryBase(
    PEPROCESS inputProcess   // ������̵�PEPROCESS�ṹָ�룬��ʾ�������
)
{
    // ��PEPROCESS�ṹָ��ǿ��ת��ΪUCHAR����ָ�룬���ڲ����ڴ��е�����
    UCHAR* process = reinterpret_cast<UCHAR*>(inputProcess);

    // �ӽ��̽ṹ��ƫ��0x28�ֽڴ���ȡĿ¼��ַ
    ULONG64 dirbase = *reinterpret_cast<ULONG64*>(process + 0x28);

    // ���Ŀ¼��ַΪ0�����ʾ����Ϊ�û�̬���̣���Ҫ��ȡ�û�Ŀ¼��ַ
    if (!dirbase)
    {
        // �ӽ��̽ṹ��ƫ��0x388�ֽڴ���ȡ�û�Ŀ¼��ַ
        ULONG64 userDirbase = *reinterpret_cast<ULONG64*>(process + 0x388);

        // �����û�Ŀ¼��ַ��Ϊ���
        return userDirbase;
    }

    // ���ؽ���Ŀ¼��ַ��Ϊ���
    return dirbase;
}


// ��ȡ�����е��ڴ�����
NTSTATUS Memory::ReadProcessMemory(
    PEPROCESS process,   // Դ���̵�PEPROCESS�ṹָ�룬��ʾԴ����
    ULONG64 address,     // Դ�����д���ȡ�ڴ����ʼ��ַ
    PVOID buffer,        // Ŀ�����ݻ������ĵ�ַ�����ڴ洢��ȡ������
    SIZE_T size          // ����ȡ���ڴ��С
)
{
    // �����ʼ��ַΪ0�����ʾ������Ч�����ش���״̬STATUS_INVALID_PARAMETER
    if (!address)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64 processDirbase = GetProcessDirectoryBase(process); // ��ȡԴ���̵�Ŀ¼��ַ
    SIZE_T currentOffset = 0; // ��ǰƫ���������ڸ����Ѷ�ȡ������
    SIZE_T totalSize = size;  // ʣ�����ȡ���ڴ��С

    while (totalSize)
    {
        // �������Ե�ַΪ�����ַ
        ULONG64 currentPhysicalAddress = TranslateLinearAddress(processDirbase, address + currentOffset);
        // ���ת��ʧ�ܣ���δ�ҵ���Ч�������ַ�����ش���״̬STATUS_NOT_FOUND
        if (!currentPhysicalAddress)
            return STATUS_NOT_FOUND;

        // ���㵱ǰ�ɶ�ȡ�Ĵ�С������ǰ����ҳ��ʣ��δ��ȡ�����ݴ�С
        ULONG64 readSize = min(PAGE_SIZE - (currentPhysicalAddress & 0xFFF), totalSize);

        // �������ַ�ж�ȡ���ݣ����洢��Ŀ�����ݻ�������
        status = ReadPhysicalAddress(currentPhysicalAddress,
            reinterpret_cast<PVOID>(reinterpret_cast<ULONG64>(buffer) + currentOffset), readSize);

        totalSize -= readSize;     // ����ʣ�����ȡ���ڴ��С
        currentOffset += readSize; // ���µ�ǰƫ����

        // �����ȡʧ�ܣ�����ѭ��
        if (!NT_SUCCESS(status))
            break;

        // �����ǰҳ��ȫ����ȡ��ϣ�����ѭ��
        if (!readSize)
            break;
    }

    return status; // ���ض�ȡ������״̬
}


// �������д���ڴ�����
NTSTATUS Memory::WriteProcessMemory(
    PEPROCESS process,   // Ŀ����̵�PEPROCESS�ṹָ�룬��ʾĿ�����
    ULONG64 address,     // Ŀ������д�д���ڴ����ʼ��ַ
    PVOID buffer,        // Դ���ݻ������ĵ�ַ������д�������
    SIZE_T size          // ��д����ڴ��С
)
{
    // �����ʼ��ַΪ0�����ʾ������Ч�����ش���״̬STATUS_INVALID_PARAMETER
    if (!address)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64 processDirbase = GetProcessDirectoryBase(process); // ��ȡĿ����̵�Ŀ¼��ַ
    SIZE_T currentOffset = 0; // ��ǰƫ���������ڸ�����д�������
    SIZE_T totalSize = size;  // ʣ���д����ڴ��С

    while (totalSize)
    {
        // ת�����Ե�ַΪ�����ַ
        ULONG64 currentPhysicalAddress = TranslateLinearAddress(processDirbase, address + currentOffset);
        // ���ת��ʧ�ܣ���δ�ҵ���Ч�������ַ�����ش���״̬STATUS_NOT_FOUND
        if (!currentPhysicalAddress)
            return STATUS_NOT_FOUND;

        // ���㵱ǰ��д��Ĵ�С������ǰ����ҳ��ʣ���д������ݴ�С
        ULONG64 writeSize = min(PAGE_SIZE - (currentPhysicalAddress & 0xFFF), totalSize);

        // �����ݴ�Դ���ݻ�����д�뵽�����ַ��
        status = WritePhysicalAddress(currentPhysicalAddress,
            reinterpret_cast<PVOID>(reinterpret_cast<ULONG64>(buffer) + currentOffset), writeSize);

        totalSize -= writeSize;     // ����ʣ���д����ڴ��С
        currentOffset += writeSize; // ���µ�ǰƫ����

        // ���д��ʧ�ܣ�����ѭ��
        if (!NT_SUCCESS(status))
            break;

        // �����ǰҳ��ȫ��д����ϣ�����ѭ��
        if (!writeSize)
            break;
    }

    return status; // ����д�������״̬
}


// ���ƽ����ڴ�����
NTSTATUS Memory::CopyProcessMemory(
    PEPROCESS sourceProcess,     // Դ���̵�PEPROCESS�ṹָ�룬��ʾԴ����
    PVOID sourceAddress,         // Դ�����д������ڴ����ʼ��ַ
    PEPROCESS targetProcess,     // Ŀ����̵�PEPROCESS�ṹָ�룬��ʾĿ�����
    PVOID targetAddress,         // Ŀ������д�д���ڴ����ʼ��ַ
    SIZE_T bufferSize            // �����Ƶ��ڴ��С
) {
    // ������ʱ������
    PVOID temporaryBuffer = ExAllocatePool(NonPagedPoolNx, bufferSize);
    // �������ʧ�ܣ����ش���״̬STATUS_INSUFFICIENT_RESOURCES
    if (!temporaryBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    // ��Դ�����ж�ȡ���ݵ���ʱ������
    NTSTATUS status = ReadProcessMemory(sourceProcess,
        reinterpret_cast<ULONG64>(sourceAddress), temporaryBuffer, bufferSize);
    // �����ȡʧ�ܣ���ת���˳���ǩ
    if (!NT_SUCCESS(status))
        goto Exit;

    // ����ʱ�������е�����д��Ŀ�����
    status = WriteProcessMemory(targetProcess,
        reinterpret_cast<ULONG64>(targetAddress), temporaryBuffer, bufferSize);

Exit:
    ExFreePool(temporaryBuffer); // �ͷ���ʱ������
    return status; // ���ظ��Ʋ�����״̬
}