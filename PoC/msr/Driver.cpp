#include <ntddk.h>
#include <intrin.h>

EXTERN_C
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT,
    _In_ PUNICODE_STRING
    )
{
    // The address of nt!PpmHeteroHgsEnabled
    UINT8* ntPpmHeteroHgsEnabled = (UINT8*)0xfffff8012db1da62;

    // Change this with a PFN of the phyiscal address we want to populate the HFI structure
    constexpr unsigned long long pfn = 0x11cecb;

    // If CPUID.06H:EAX.[19] == 0, bail
    int regs[4];
    __cpuid(regs, 6);
    if ((regs[0] & (1 << 19)) == 0) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HFI not supported.\n");
        return STATUS_UNSUCCESSFUL;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Starting the driver: Current MSR values:\n");
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "0x1b1 : 0x%llx\n", __readmsr(0x1b1));
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "0x17d0: 0x%llx\n", __readmsr(0x17d0));
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "0x17d1: 0x%llx\n", __readmsr(0x17d1));
    
    if (*ntPpmHeteroHgsEnabled) 
    {
        *ntPpmHeteroHgsEnabled = 0;
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Patched nt!PpmHeteroHgsEnabled. Hibernate the system, wake it up, and rerun this.\n");
    }
    else 
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Populating the HFI structure at 0x%llx\n", pfn << 12);

        // Mark that OS has done with reading the structure. Clear bit[26]
        __writemsr(0x1b1, __readmsr(0x1b1) & ~(1ull << 26));

        // IA32_HW_FEEDBACK_PTR <= addr | Valid
        __writemsr(0x17d0, (pfn << 12) | 1);

        // IA32_HW_FEEDBACK_CONFIG <= Enable both HFI
        __writemsr(0x17d1, 1);
    }
    return STATUS_CANCELLED;
}
