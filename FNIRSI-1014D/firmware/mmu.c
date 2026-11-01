//----------------------------------------------------------------------------------------------------------------------------------
//
//On the ARM926 the C bit in the control register only takes effect with the MMU on, because the
//cacheability of an access comes from the page tables. Without it every data access goes to the
//DRAM chip uncached, which made a trace frame take about 100 ms.
//
//The map is flat (virtual = physical) with 1 MB sections:
//  0x80000000..0x81FFFFFF  DRAM 32 MB   cacheable, write-through (C=1, B=0)
//  everything else         device       uncached and unbuffered (C=0, B=0)
//
//Write-through keeps DRAM always up to date: the display engine scans the framebuffers straight out
//of DRAM and sees every pixel the CPU writes, with no cache cleaning. The CPU is the only bus master
//that writes DRAM (SD card, USB and FPGA transfers are all done by the CPU through registers), so no
//cache line can go stale either. Peripherals (0x01Cxxxxx, the FPGA bus on port E, interrupt
//controller, SD, USB, display engine) and the SRAM with the exception vectors stay uncached.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "mmu.h"

#if PORT_1014D

//----------------------------------------------------------------------------------------------------------------------------------

#define MMU_SECTION             0x00000002     //Section descriptor
#define MMU_SECTION_SBO         0x00000010     //Bit 4 should be one on the ARM926
#define MMU_SECTION_C           0x00000008     //Cacheable
#define MMU_SECTION_B           0x00000004     //Bufferable
#define MMU_SECTION_AP_FULL     0x00000C00     //Read / write in all modes
#define MMU_SECTION_DOMAIN0     0x00000000

#define MMU_DRAM_FIRST_SECTION  0x800          //0x80000000
#define MMU_DRAM_SECTIONS       32             //32 MB

#define CP15_MMU                (1 << 0)
#define CP15_ALIGN              (1 << 1)
#define CP15_DCACHE             (1 << 2)
#define CP15_ICACHE             (1 << 12)

//First level table: 4096 entries, must be 16 KB aligned
static uint32 mmu_table[4096] __attribute__((aligned(16384)));

//----------------------------------------------------------------------------------------------------------------------------------

static inline uint32 mmu_read_control(void)
{
  uint32 value;

  __asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r" (value) : : "memory");

  return(value);
}

//----------------------------------------------------------------------------------------------------------------------------------

static inline void mmu_write_control(uint32 value)
{
  //The two nops cover the pipeline while the new setting takes effect
  __asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0\n nop\n nop" : : "r" (value) : "memory");
}

//----------------------------------------------------------------------------------------------------------------------------------

static inline void mmu_invalidate_caches_and_tlb(void)
{
  uint32 zero = 0;

  __asm__ __volatile__(
    "mcr p15, 0, %0, c7, c10, 4\n"     //Drain the write buffer
    "mcr p15, 0, %0, c7, c7, 0\n"      //Invalidate the instruction and data caches
    "mcr p15, 0, %0, c8, c7, 0\n"      //Invalidate the TLBs
    : : "r" (zero) : "memory");
}

//----------------------------------------------------------------------------------------------------------------------------------

void mmu_setup(void)
{
  uint32 i;
  uint32 control;

  //Flat map of the whole 4 GB as uncached device memory
  for(i=0;i<4096;i++)
  {
    mmu_table[i] = (i << 20) | MMU_SECTION_AP_FULL | MMU_SECTION_DOMAIN0 | MMU_SECTION_SBO | MMU_SECTION;
  }

  //DRAM cacheable write-through
  for(i=MMU_DRAM_FIRST_SECTION;i<(MMU_DRAM_FIRST_SECTION + MMU_DRAM_SECTIONS);i++)
  {
    mmu_table[i] |= MMU_SECTION_C;
  }

  //Caches still off here, but make sure nothing stale is used once they are switched on
  mmu_invalidate_caches_and_tlb();

  //Table base and domain 0 as manager (no permission checks)
  __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 0" : : "r" ((uint32)mmu_table) : "memory");
  __asm__ __volatile__("mcr p15, 0, %0, c3, c0, 0" : : "r" (0x00000003) : "memory");

  //MMU, data and instruction cache on; alignment checking stays off as it was before
  control = mmu_read_control();
  control |= CP15_MMU | CP15_DCACHE | CP15_ICACHE;
  control &= ~CP15_ALIGN;
  mmu_write_control(control);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Before handing the CPU to the boot ROM (FEL) the state it expects is restored: MMU and data cache
//off. With write-through caching there is nothing to write back, only the caches to invalidate.

void mmu_disable(void)
{
  uint32 control = mmu_read_control();

  control &= ~(CP15_MMU | CP15_DCACHE);
  mmu_write_control(control);

  mmu_invalidate_caches_and_tlb();
}

//----------------------------------------------------------------------------------------------------------------------------------

#endif //PORT_1014D
