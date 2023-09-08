#include "General.h"

// 禁用特定警告 4996，该警告表示 ExAllocatePool 函数已弃用
#pragma warning(disable : 4996)

// 初始化内存管理器
NTSTATUS Memory::Init()
{
    PHYSICAL_ADDRESS maxAddress;
    maxAddress.QuadPart = MAXULONG64;

    // 分配连续的内存页
    MainVirtualAddress = MmAllocateContiguousMemory(PAGE_SIZE, maxAddress);
    if (!MainVirtualAddress)
        return STATUS_INSUFFICIENT_RESOURCES;

    VIRTUAL_ADDRESS virtualAddress;
    virtualAddress.Pointer = MainVirtualAddress;

    PTE_CR3 cr3;
    cr3.Value = __readcr3();

    // 获取PML4表的虚拟地址
    PML4E* pml4 = static_cast<PML4E*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(cr3.Pml4)));
    PML4E* pml4e = (pml4 + virtualAddress.Pml4Index);

    // 如果PML4表项不存在，则返回错误状态码
    if (!pml4e->Present)
        return STATUS_NOT_FOUND;

    // 获取PDPT表的虚拟地址
    PDPTE* pdpt = static_cast<PDPTE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pml4e->Pdpt)));
    PDPTE* pdpte = pdpt + virtualAddress.PdptIndex;

    // 如果PDPT表项不存在，则返回错误状态码
    if (!pdpte->Present)
        return STATUS_NOT_FOUND;

    // 检查是否为1GB大页
    if (pdpte->PageSize)
        return STATUS_INVALID_PARAMETER;

    // 获取PD表的虚拟地址
    PDE* pd = static_cast<PDE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pdpte->Pd)));
    PDE* pde = pd + virtualAddress.PdIndex;

    // 如果PD表项不存在，则返回错误状态码
    if (!pde->Present)
        return STATUS_NOT_FOUND;

    // 检查是否为2MB大页
    if (pde->PageSize)
        return STATUS_INVALID_PARAMETER;

    // 获取PT表的虚拟地址
    PTE* pt = static_cast<PTE*>(Utils::PhysicalToVirtual(PFN_TO_PAGE(pde->Pt)));
    PTE* pte = pt + virtualAddress.PtIndex;

    // 如果PT表项不存在，则返回错误状态码
    if (!pte->Present)
        return STATUS_NOT_FOUND;

    MainPageEntry = pte;

    return STATUS_SUCCESS;
}

// 覆盖指定物理地址对应的页
PVOID Memory::OverwritePage(
    ULONG64 physicalAddress   // 物理地址，表示待覆写的页面的起始物理地址
)
{
    // 页边界检查由Read/WriteProcessMemory函数完成
    // 并且页表项不会分散在不同的页上

    // 计算物理地址所在页的偏移量
    ULONG pageOffset = physicalAddress % PAGE_SIZE;

    // 计算物理地址所在页的起始物理地址
    ULONG64 pageStartPhysical = physicalAddress - pageOffset;

    // 将页表项的PageFrame字段更新为目标页的物理页号
    MainPageEntry->PageFrame = PAGE_TO_PFN(pageStartPhysical);

    // 刷新指定虚拟地址所对应的TLB缓存
    __invlpg(MainVirtualAddress);

    // 返回覆盖后的内存地址
    return (PVOID)((ULONG64)MainVirtualAddress + pageOffset);
}


// 读取物理地址中的数据
NTSTATUS Memory::ReadPhysicalAddress(
    ULONG64 targetAddress,   // 目标物理地址，表示待读取数据的物理内存地址
    PVOID buffer,            // 目标数据缓冲区的地址，用于存储读取的数据
    SIZE_T size              // 待读取的内存大小
)
{
    // 覆盖物理地址对应的页，返回覆盖后的虚拟地址
    PVOID virtualAddress = OverwritePage(targetAddress);

    // 将覆盖后的虚拟地址中的数据复制到目标数据缓冲区中
    memcpy(buffer, virtualAddress, size);

    return STATUS_SUCCESS;
}


// 写入数据到物理地址
NTSTATUS Memory::WritePhysicalAddress(
    ULONG64 targetAddress,   // 目标物理地址，表示待写入数据的物理内存地址
    PVOID buffer,            // 源数据缓冲区的地址，即待写入的数据
    SIZE_T size              // 待写入的内存大小
)
{
    // 覆盖物理地址对应的页，返回覆盖后的虚拟地址
    PVOID virtualAddress = OverwritePage(targetAddress);

    // 将源数据缓冲区中的数据复制到覆盖后的虚拟地址中
    memcpy(virtualAddress, buffer, size);

    return STATUS_SUCCESS;
}


#define PAGE_OFFSET_SIZE 12    // 页偏移大小，表示页内的偏移量位数

// 物理地址掩码，用于屏蔽物理地址中的偏移量和低位，保留高位的物理地址
static const ULONG64 PMASK = (~0xfull << 8) & 0xfffffffffull;


// 将线性地址转换为物理地址（线性地址:程序使用的地址空间的逻辑地址）
ULONG64 Memory::TranslateLinearAddress(
    ULONG64 directoryTableBase,   // 页表目录基址，表示待翻译的线性地址所在的页表目录的基址
    ULONG64 virtualAddress        // 待翻译的线性地址
)
{
    directoryTableBase &= ~0xf;    // 清除页表目录基址的低4位，保留高位的页表目录基址

    ULONG64 pageOffset = virtualAddress & ~(~0ul << PAGE_OFFSET_SIZE);
    // 计算线性地址的页内偏移量，即取线性地址的低12位（页偏移大小为12位）

    ULONG64 pte = ((virtualAddress >> 12) & (0x1ffll));
    // 获取页表项索引，通过右移12位得到原始索引，然后通过位与操作取低9位（页表项索引占9位）

    ULONG64 pt = ((virtualAddress >> 21) & (0x1ffll));
    // 获取页中目录项索引，通过右移21位得到原始索引，然后通过位与操作取低9位（页中目录项索引占9位）

    ULONG64 pd = ((virtualAddress >> 30) & (0x1ffll));
    // 获取页面目录索引，通过右移30位得到原始索引，然后通过位与操作取低9位（页面目录索引占9位）

    ULONG64 pdp = ((virtualAddress >> 39) & (0x1ffll));
    // 获取页面目录指针索引，通过右移39位得到原始索引，然后通过位与操作取低9位（页面目录指针索引占9位）

    ULONG64 pdpe = 0;
    ReadPhysicalAddress(directoryTableBase + 8 * pdp, &pdpe, sizeof(pdpe));
    // 读取物理地址中的页面目录指针项，存储在pdpe变量中

    if (~pdpe & 1)
        return 0;   // 页面目录指针项无效，返回0表示转换失败

    ULONG64 pde = 0;
    ReadPhysicalAddress((pdpe & PMASK) + 8 * pd, &pde, sizeof(pde));
    // 读取物理地址中的页面目录项，存储在pde变量中

    if (~pde & 1)
        return 0;   // 页面目录项无效，返回0表示转换失败

    // 1GB大页，使用pde的12-34位作为偏移
    if (pde & 0x80)
        return (pde & (~0ull << 42 >> 12)) + (virtualAddress & ~(~0ull << 30));
    // 判断是否为1GB大页，如果是，则将pde中的12-34位作为偏移量，与线性地址的低30位进行合并，得到物理地址

    ULONG64 pteAddr = 0;
    ReadPhysicalAddress((pde & PMASK) + 8 * pt, &pteAddr, sizeof(pteAddr));
    // 读取物理地址中的页表项指针，存储在pteAddr变量中

    if (~pteAddr & 1)
        return 0;   // 页表项无效，返回0表示转换失败

    // 2MB大页
    if (pteAddr & 0x80)
        return (pteAddr & PMASK) + (virtualAddress & ~(~0ull << 21));
    // 判断是否为2MB大页，如果是，则将pteAddr与掩码PMASK进行位与操作，得到物理地址

    virtualAddress = 0;
    ReadPhysicalAddress((pteAddr & PMASK) + 8 * pte, &virtualAddress, sizeof(virtualAddress));
    // 读取物理地址中的页表项，存储在virtualAddress变量中

    virtualAddress &= PMASK;
    // 通过与掩码PMASK进行位与操作，屏蔽掉低位，保留高位的物理地址

    if (!virtualAddress)
        return 0;   // 未找到有效的页表项，返回0表示转换失败

    return virtualAddress + pageOffset;
    // 返回物理地址，即将虚拟地址的高位与物理地址合并，并加上页内偏移量
}

// 获取进程的目录基址
ULONG64 Memory::GetProcessDirectoryBase(
    PEPROCESS inputProcess   // 输入进程的PEPROCESS结构指针，表示输入进程
)
{
    // 将PEPROCESS结构指针强制转换为UCHAR类型指针，用于操作内存中的数据
    UCHAR* process = reinterpret_cast<UCHAR*>(inputProcess);

    // 从进程结构中偏移0x28字节处读取目录基址
    ULONG64 dirbase = *reinterpret_cast<ULONG64*>(process + 0x28);

    // 如果目录基址为0，则表示进程为用户态进程，需要获取用户目录基址
    if (!dirbase)
    {
        // 从进程结构中偏移0x388字节处读取用户目录基址
        ULONG64 userDirbase = *reinterpret_cast<ULONG64*>(process + 0x388);

        // 返回用户目录基址作为结果
        return userDirbase;
    }

    // 返回进程目录基址作为结果
    return dirbase;
}


// 读取进程中的内存数据
NTSTATUS Memory::ReadProcessMemory(
    PEPROCESS process,   // 源进程的PEPROCESS结构指针，表示源进程
    ULONG64 address,     // 源进程中待读取内存的起始地址
    PVOID buffer,        // 目标数据缓冲区的地址，用于存储读取的数据
    SIZE_T size          // 待读取的内存大小
)
{
    // 如果起始地址为0，则表示参数无效，返回错误状态STATUS_INVALID_PARAMETER
    if (!address)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64 processDirbase = GetProcessDirectoryBase(process); // 获取源进程的目录基址
    SIZE_T currentOffset = 0; // 当前偏移量，用于跟踪已读取的数据
    SIZE_T totalSize = size;  // 剩余待读取的内存大小

    while (totalSize)
    {
        // 翻译线性地址为物理地址
        ULONG64 currentPhysicalAddress = TranslateLinearAddress(processDirbase, address + currentOffset);
        // 如果转换失败，即未找到有效的物理地址，返回错误状态STATUS_NOT_FOUND
        if (!currentPhysicalAddress)
            return STATUS_NOT_FOUND;

        // 计算当前可读取的大小，即当前物理页中剩余未读取的数据大小
        ULONG64 readSize = min(PAGE_SIZE - (currentPhysicalAddress & 0xFFF), totalSize);

        // 从物理地址中读取数据，并存储到目标数据缓冲区中
        status = ReadPhysicalAddress(currentPhysicalAddress,
            reinterpret_cast<PVOID>(reinterpret_cast<ULONG64>(buffer) + currentOffset), readSize);

        totalSize -= readSize;     // 更新剩余待读取的内存大小
        currentOffset += readSize; // 更新当前偏移量

        // 如果读取失败，跳出循环
        if (!NT_SUCCESS(status))
            break;

        // 如果当前页已全部读取完毕，跳出循环
        if (!readSize)
            break;
    }

    return status; // 返回读取操作的状态
}


// 向进程中写入内存数据
NTSTATUS Memory::WriteProcessMemory(
    PEPROCESS process,   // 目标进程的PEPROCESS结构指针，表示目标进程
    ULONG64 address,     // 目标进程中待写入内存的起始地址
    PVOID buffer,        // 源数据缓冲区的地址，即待写入的数据
    SIZE_T size          // 待写入的内存大小
)
{
    // 如果起始地址为0，则表示参数无效，返回错误状态STATUS_INVALID_PARAMETER
    if (!address)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64 processDirbase = GetProcessDirectoryBase(process); // 获取目标进程的目录基址
    SIZE_T currentOffset = 0; // 当前偏移量，用于跟踪已写入的数据
    SIZE_T totalSize = size;  // 剩余待写入的内存大小

    while (totalSize)
    {
        // 转换线性地址为物理地址
        ULONG64 currentPhysicalAddress = TranslateLinearAddress(processDirbase, address + currentOffset);
        // 如果转换失败，即未找到有效的物理地址，返回错误状态STATUS_NOT_FOUND
        if (!currentPhysicalAddress)
            return STATUS_NOT_FOUND;

        // 计算当前可写入的大小，即当前物理页中剩余可写入的数据大小
        ULONG64 writeSize = min(PAGE_SIZE - (currentPhysicalAddress & 0xFFF), totalSize);

        // 将数据从源数据缓冲区写入到物理地址中
        status = WritePhysicalAddress(currentPhysicalAddress,
            reinterpret_cast<PVOID>(reinterpret_cast<ULONG64>(buffer) + currentOffset), writeSize);

        totalSize -= writeSize;     // 更新剩余待写入的内存大小
        currentOffset += writeSize; // 更新当前偏移量

        // 如果写入失败，跳出循环
        if (!NT_SUCCESS(status))
            break;

        // 如果当前页已全部写入完毕，跳出循环
        if (!writeSize)
            break;
    }

    return status; // 返回写入操作的状态
}


// 复制进程内存数据
NTSTATUS Memory::CopyProcessMemory(
    PEPROCESS sourceProcess,     // 源进程的PEPROCESS结构指针，表示源进程
    PVOID sourceAddress,         // 源进程中待复制内存的起始地址
    PEPROCESS targetProcess,     // 目标进程的PEPROCESS结构指针，表示目标进程
    PVOID targetAddress,         // 目标进程中待写入内存的起始地址
    SIZE_T bufferSize            // 待复制的内存大小
) {
    // 分配临时缓冲区
    PVOID temporaryBuffer = ExAllocatePool(NonPagedPoolNx, bufferSize);
    // 如果分配失败，返回错误状态STATUS_INSUFFICIENT_RESOURCES
    if (!temporaryBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    // 从源进程中读取数据到临时缓冲区
    NTSTATUS status = ReadProcessMemory(sourceProcess,
        reinterpret_cast<ULONG64>(sourceAddress), temporaryBuffer, bufferSize);
    // 如果读取失败，跳转到退出标签
    if (!NT_SUCCESS(status))
        goto Exit;

    // 将临时缓冲区中的数据写入目标进程
    status = WriteProcessMemory(targetProcess,
        reinterpret_cast<ULONG64>(targetAddress), temporaryBuffer, bufferSize);

Exit:
    ExFreePool(temporaryBuffer); // 释放临时缓冲区
    return status; // 返回复制操作的状态
}