/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2010  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/kernel.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/time.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/machine/kernel.h>
#include <grub/machine/memory.h>
#include <grub/mips/loongson.h>
#include <grub/cpu/kernel.h>
#include <grub/cs5536.h>

extern void grub_video_sm712_init (void);
extern void grub_video_video_init (void);
extern void grub_video_bitmap_init (void);
extern void grub_font_manager_init (void);
extern void grub_term_gfxterm_init (void);
extern void grub_at_keyboard_init (void);

/* FIXME: use interrupt to count high.  */
grub_uint64_t
grub_get_rtc (void)
{
  static grub_uint32_t high = 0;
  static grub_uint32_t last = 0;
  grub_uint32_t low;

  asm volatile ("mfc0 %0, " GRUB_CPU_LOONGSON_COP0_TIMER_COUNT : "=r" (low));
  if (low < last)
    high++;
  last = low;

  return (((grub_uint64_t) high) << 32) | low;
}

grub_err_t
grub_machine_mmap_iterate (int NESTED_FUNC_ATTR (*hook) (grub_uint64_t,
							 grub_uint64_t,
							 grub_uint32_t))
{
  hook (GRUB_ARCH_LOWMEMPSTART, grub_arch_memsize << 20,
	GRUB_MACHINE_MEMORY_AVAILABLE);
  hook (GRUB_ARCH_HIGHMEMPSTART, grub_arch_highmemsize << 20,
	GRUB_MACHINE_MEMORY_AVAILABLE);
  return GRUB_ERR_NONE;
}


static void *
get_modules_end (void)
{
  struct grub_module_info *modinfo;
  struct grub_module_header *header;
  grub_addr_t modbase;

  modbase = grub_arch_modules_addr ();
  modinfo = (struct grub_module_info *) modbase;

  /* Check if there are any modules.  */
  if ((modinfo == 0) || modinfo->magic != GRUB_MODULE_MAGIC)
    return modinfo;

  for (header = (struct grub_module_header *) (modbase + modinfo->offset);
       header < (struct grub_module_header *) (modbase + modinfo->size);
       header = (struct grub_module_header *) ((char *) header + header->size));

  return header;
}

void
grub_machine_init (void)
{
  void *modend;

  /* FIXME: measure this.  */
  if (grub_arch_busclock == 0)
    {
      grub_arch_busclock = 66000000;
      grub_arch_cpuclock = 797000000;
    }

  grub_install_get_time_ms (grub_rtc_get_time_ms);

  if (grub_arch_memsize == 0)
    {
      grub_port_t smbbase;
      grub_err_t err;
      grub_pci_device_t dev;
      struct grub_smbus_spd spd;
      unsigned totalmem;
      int i;

      if (!grub_cs5536_find (&dev))
	grub_fatal ("No CS5536 found\n");

      err = grub_cs5536_init_smbus (dev, 0x7ff, &smbbase);
      if (err)
	grub_fatal ("Couldn't init SMBus: %s\n", grub_errmsg);

      /* Yeeloong has only one memory slot.  */
      err = grub_cs5536_read_spd (smbbase, GRUB_SMB_RAM_START_ADDR, &spd);
      if (err)
	grub_fatal ("Couldn't read SPD: %s\n", grub_errmsg);
      for (i = 5; i < 13; i++)
	if (spd.ddr2.rank_capacity & (1 << (i & 7)))
	  break;
      /* Something is wrong.  */
      if (i == 13)
	totalmem = 256;
      else
	totalmem = ((spd.ddr2.num_of_ranks
		     & GRUB_SMBUS_SPD_MEMORY_NUM_OF_RANKS_MASK) + 1) << (i + 2);
      
      if (totalmem >= 256)
	{
	  grub_arch_memsize = 256;
	  grub_arch_highmemsize = totalmem - 256;
	}
      else
	{
	  grub_arch_memsize = (totalmem >> 20);
	  grub_arch_highmemsize = 0;
	}
    }

  modend = get_modules_end ();
  grub_mm_init_region (modend, (grub_arch_memsize << 20)
		       - (((grub_addr_t) modend) - GRUB_ARCH_LOWMEMVSTART));
  /* FIXME: use upper memory as well.  */

  /* Initialize output terminal (can't be done earlier, as gfxterm
     relies on a working heap.  */
  grub_video_video_init ();
  grub_video_sm712_init ();
  grub_video_bitmap_init ();
  grub_font_manager_init ();
  grub_term_gfxterm_init ();

  grub_at_keyboard_init ();
}

void
grub_machine_fini (void)
{
}

void
grub_exit (void)
{
  while (1);
}

void
grub_halt (void)
{
  while (1);
}

void
grub_reboot (void)
{
  while (1);
}

