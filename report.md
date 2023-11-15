- [Summary](#summary)
- [Platform and requirements](#platform-and-requirements)
- [Description](#description)
  - [Background and core issue](#background-and-core-issue)
  - [Need of S4 transition](#need-of-s4-transition)
- [PoC](#poc)
  - [Demo configuration](#demo-configuration)
  - [Steps to repro](#steps-to-repro)
  - [Expected result](#expected-result)
  - [Actual result](#actual-result)
- [Possible fix](#possible-fix)
- [Comments](#comments)
- [References](#references)

# Summary

Kernel-mode code in the root partition can corrupt arbitrary physical pages irrespective of EPT permissions using the Hardware Feedback Interface processor feature.


# Platform and requirements

- Tested on Windows build 10.0.22621.1928
- A physical machine with Intel 12th+ gen processors (more specifically, support of HFI)
- Ability to perform:
  - arbitrary MSR write and kernel-memory write (eg, need a vulnerable driver)
  - system shutdown or hibernation


# Description


## Background and core issue

When the HVCI is enabled, the root partition is restricted in what physical memory it can write even with the kernel-mode privileges. This restriction can be partially violated by abusing two of MSRs that are freely writable from the kernel-mode code in the root partition: `IA32_HW_FEEDBACK_PTR` and `IA32_HW_FEEDBACK_CONFIG`.

Those MSRs are part of the Intel processor feature named Hardware Feedback Interface, which I will abbreviate as HFI. This is a mechanism for a processor to tell an operating system performance information. This performance information, called the HFI structure, is populated by a processor at a physical memory address specified by `IA32_HW_FEEDBACK_PTR`. The other MSR, `IA32_HW_FEEDBACK_CONFIG`, is responsible for enabling the feature. The greater details of HFI is described in "15.6 HARDWARE FEEDBACK INTERFACE AND INTEL® THREAD DIRECTOR" of the Intel SDM [1].

As stated by the SDM, the value in the `IA32_HW_FEEDBACK_PTR` is treated as a physical address.
```
15.6.4 Hardware Feedback Interface Pointer
...
ADDR. This is the physical address of the page frame of the first page of this structure.
```
This is the case regardless of whether the processor is in VMX non-root operation. When the processor populates the HFI structure, it does not consider EPT. Thus, if code in VMX non-root operation could write those MSRs, it is possible to bypass memory access restriction with EPT and populate the HFI structure on an arbitrary page.

This is what appears to be happening with Hyper-V. Hyper-V does not intercept read or write to the above mentioned to MSRs from the root partition. Thus, the root partition code could install a vulnerable driver that is not block-listed and capable of arbitrary MSR write and can populate the HFI structure irrespective to EPT permissions.


## Need of S4 transition

Actually triggering the described scenario _for me_ required transition to/from S4 power state.

As far as I gather from the Intel SDM, populating the HFI structure can occur more than once. For example, enable HFI first time, have the processor populate the HFI structure, disable HFI, re-enable HFI, and have the processor populate the HFI structure again. However, I was not able to observe re-population of the HFI structure. It appears as if the processor populates the HFI structure only once.

NTOS already enables HFI during processor startup, and thus, an attacker would not be able to reproduce the above mentioned scenarios even if she can write to the MSRs.

To workaround this, an attacker could shutdown or hibernate the system first, and then write to the MSRs after wake up. Both shutdown and hibernation on Windows put the processor into the S4 power state, where the processor is reset and MSRs values are cleared. After returning from S4, both HFI MSRs are reset to zero, and an attacker could write them and triggers the HFI structure generation.

This cannot normally happen as the MSRs are properly re-configured by NTOS on wake up. However, that logic is gated by a global variable named `nt!PpmHeteroHgsEnabled`. If this is set to 0, re-configuration does not occur. An attacker with arbitrary kernel-memory write can set this variable to 0, trigger shutdown or hibernation, and finally write the HFI MSRs. This process could be repeated as many time as the attacker wants.


# PoC

- A recording of reproduction is uploaded on Youtube: https://www.youtube.com/watch?v=NAhhJkA73mY
- Source code of PoC is uploaded on Github: https://github.com/tandasat/CVE-2023-36427


## Demo configuration

- Dell Latitude 7330
- Windows build 10.0.22621.1928
- HVCI is enabled
- Test-signing is enabled and secure boot is disabled
    - This is because using my own driver instead of a vulnerable driver is much clearer to demonstrate the issue.
-  Secure launch ("Firmware protection" on the Windows Defender configuration GUI) is disabled
     - This is because my system never successfully wakes up from shutdown or hibernation when secure launch is enabled.
- Livekd [2] is installed


## Steps to repro

NB: the target system will crash within a few minutes after following this instruction.

1. Check the linear address of `nt!PpmHeteroHgsEnabled` with `livekd`
   1. Start command prompt with the administrator privilege
   2. Run those commands:
      ```
      > livekd
      0: kd> x nt!PpmHeteroHgsEnabled
      fffff806`1a31da62 nt!PpmHeteroHgsEnabled = <no type information>
      ```
   3. Take note of the linear address, ie, `fffff8061a31da62` in this example
2. Decide the physical address to populate the HFI structure. This can be any address that is (1) normally not writable and (2) we can confirm that the HFI structure was written. We use `ci!g_CiOptions`
   1. On the same livekd session run the following command:
      ```
      0: kd> !pte ci!g_CiOptions
                                                VA fffff80618ac4004
      PXE at FFFFFCFE7F3F9F80    PPE at FFFFFCFE7F3F00C0    PDE at FFFFFCFE7E018628    PTE at FFFFFCFC030C5620
      contains 000000048F60B063  contains 000000048F60C063  contains 000000048F61E063  contains 890000011CECB121
      pfn 48f60b    ---DA--KWEV  pfn 48f60c    ---DA--KWEV  pfn 48f61e    ---DA--KWEV  pfn 11cecb    -G--A--KR-V
      ```
   2. Take note of the PFN, ie, `11cecb` in this example
   3. Double check that the current memory contents make sense
      ```
      0: kd> dd ci!g_CiOptions
      fffff806`18ac4004  0101c00e 00000000 00008004 00000860
      fffff806`18ac4014  00000000 00000000 00000000 00000000
      fffff806`18ac4024  00000000 00000000 00000000 00000000
      fffff806`18ac4034  00000000 00000000 00000000 00000000
      fffff806`18ac4044  00000000 00000000 00000000 00000000
      fffff806`18ac4054  00000000 00000000 00000000 00000000
      fffff806`18ac4064  00000000 00000000 00000000 00000000
      fffff806`18ac4074  00000000 00000000 00000000 00000000
      ```
3. Update and compile the PoC file. This step can be done on other machine
   1. Extract the msr.zip, and open the msr.sln on Visual Studio 2022
   2. Update `UINT8* ntPpmHeteroHgsEnabled = (UINT8*)0xfffff80738d1da62;` with what we noted at the step 1.3
   3. Update `constexpr unsigned long long pfn = 0x119932;` with what we noted at the step 2.2
   4. Build the solution for x64 / Debug
   5. If needed, copy over the compiled msr.sys into the target system
4. Load the PoC
   1. Optionally, start DbgView [3] with the administrator privileges and enable "Capture Kernel"
   2. Run the following command on the command prompt with the administrators privileges
      ```
      > sc create msr type= kernel binPath= <full_path_to_msr.sys>
      [SC] CreateService SUCCESS
      ```
   3. Start the PoC
      ```
      > sc start msr
      [SC] StartService FAILED 995:

      The I/O operation has been aborted because of either a thread exit or an application request
      ```
      The error message is expected. If DbgView is running, it should show messages like this:
      ```
      Starting the driver: Current MSR values:
      0x1b1 : 0x88320a82
      0x17d0: 0x48f7eb001
      0x17d1: 0x3
      Patched nt!PpmHeteroHgsEnabled. Hibernate the system, wake it up, and rerun this
      DriverEntry failed 0xc0000120 for driver \REGISTRY\MACHINE\SYSTEM\ControlSet001\Services\msr
      ```
5. From the start menu, select "Shut down" (alternatively, "Hibernate")
6. Boot the system
7. Rerun the PoC
   1. Start the command prompt with administrator privileges
   2. Optionally, start DbgView with the administrator privileges and enable "Capture Kernel"
   3. Rerun the PoC
      ```
      >sc start msr
      [SC] StartService FAILED 995:

      The I/O operation has been aborted because of either a thread exit or an application request.
      ```
      The error message is expected. If DbgView is running, it should show messages like this:
      ```
      Starting the driver: Current MSR values:
      0x1b1 : 0x88300a82
      0x17d0: 0x0
      0x17d1: 0x0
      Populating the HFI table at 0x11cecb000
      DriverEntry failed 0xc0000120 for driver \REGISTRY\MACHINE\SYSTEM\ControlSet001\Services\msr
      ```
8. Check that the memory contents has changed
   1. Start `livekd` and run the following command:
      ```
      0: kd> dd ci!g_CiOptions
      fffff806`18ac4004  00000002 00000101 00000000 00005c3d
      fffff806`18ac4014  00000000 00005c3d 00000000 00006424
      fffff806`18ac4024  00000000 00006424 00000000 00000000
      fffff806`18ac4034  00000000 00000000 00000000 00000000
      fffff806`18ac4044  00000000 00000000 00000000 00000000
      fffff806`18ac4054  00000000 00000000 00000000 00000000
      fffff806`18ac4064  00000000 00000000 00000000 00000000
      fffff806`18ac4074  00000000 00000000 00000000 00000000
      ```


## Expected result

The root partition is unable to corrupt a page that is write-protected or inaccessible due to EPT settings. `WRMSR` at the step 7 should trigger #GP or be no-op.


## Actual result

The root partition can corrupt a page regardless of whether it is write-protected or made inaccessible with EPT.


# Possible fix

I can think of a few:
- `WRMSR` to `IA32_HW_FEEDBACK_PTR` is intercepted. The host checks "accessibility" to the address by the guest. Reject `WRMSR` if the address is not where the guest is supposed to be able to write data.
- `WRMSR` to `IA32_HW_FEEDBACK_PTR` is intercepted. The host checks that the given address is what is already negotiated between the host and the guest. Reject `WRMSR` if not.
- Perform `WRMSR` within the hypervisor and expose the HFI structure to the guest through a read-only page. `WRMSR` from the guest is rejected unconditionally.


# Comments

- Real-world exportability is very low. An attacker can only populate data at the beginning of a page, and its contents are mostly uncontrollable. Not to mention it requires 12th+ gen hardware to trigger the bug. Besides, the root partition can disable with `bcdedit` and rebooting the system. Nonetheless, this is a bug with some security implications, which should be fixed in my view.
- Ideally, Intel should have provided an option to treat the specified address as a guest physical address, something similar to the "Intel PT uses guest physical addresses" VM-execution control.


# References

1. https://www.intel.com/sdm
2. https://learn.microsoft.com/en-us/sysinternals/downloads/livekd
3. https://learn.microsoft.com/en-us/sysinternals/downloads/debugview
