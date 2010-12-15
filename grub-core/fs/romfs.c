/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#include <grub/file.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/disk.h>
#include <grub/fs.h>
#include <grub/fshelp.h>

struct grub_romfs_superblock
{
  char magic[8];
#define GRUB_ROMFS_MAGIC "-rom1fs-"
  grub_uint32_t total_size;
  grub_uint32_t chksum;
  char label[0];
};

struct grub_romfs_file_header
{
  grub_uint32_t next_file;
  grub_uint32_t spec;
  grub_uint32_t size;
  grub_uint32_t chksum;
  char name[0];
};

struct grub_romfs_data
{
  grub_disk_addr_t first_file;
  grub_disk_t disk;
};

struct grub_fshelp_node
{
  grub_disk_addr_t addr;
  struct grub_romfs_data *data;
  grub_disk_addr_t data_addr;
  /* Not filled for root.  */
  struct grub_romfs_file_header file;
};

#define GRUB_ROMFS_ALIGN 16
#define GRUB_ROMFS_TYPE_MASK 7
#define GRUB_ROMFS_TYPE_HARDLINK 0
#define GRUB_ROMFS_TYPE_DIRECTORY 1
#define GRUB_ROMFS_TYPE_REGULAR 2

static grub_err_t
do_checksum (void *in, grub_size_t insize)
{
  grub_uint32_t *a = in;
  grub_size_t sz = insize / 4;
  grub_uint32_t *b = a + sz;
  grub_uint32_t csum = 0;

  while (a < b)
    csum += grub_be_to_cpu32 (*a++);
  if (csum)
    return grub_error (GRUB_ERR_BAD_FS, "invalid checksum");
  return GRUB_ERR_NONE;
}

static struct grub_romfs_data *
grub_romfs_mount (grub_device_t dev)
{
  union {
    struct grub_romfs_superblock sb;
    char d[512];
  } sb;
  grub_err_t err;
  char *ptr;
  grub_disk_addr_t sec = 0;
  struct grub_romfs_data *data;
  if (!dev->disk)
    {
      grub_error (GRUB_ERR_BAD_FS, "not a disk");
      return NULL;
    }
  err = grub_disk_read (dev->disk, 0, 0, sizeof (sb), &sb);
  if (err == GRUB_ERR_OUT_OF_RANGE)
    err = grub_errno = GRUB_ERR_BAD_FS;
  if (err)
    return NULL;
  if (grub_be_to_cpu32 (sb.sb.total_size) < sizeof (sb))
    {
      grub_error (GRUB_ERR_BAD_FS, "too short filesystem");
      return NULL;
    }
  err = do_checksum (&sb, sizeof (sb) < grub_be_to_cpu32 (sb.sb.total_size) ?
		     sizeof (sb) : grub_be_to_cpu32 (sb.sb.total_size));
  if (err)
    return NULL;
  for (ptr = sb.sb.label; (void *) ptr < (void *) (&sb + 1)
	 && ptr < sb.d + grub_be_to_cpu32 (sb.sb.total_size); ptr++)
    if (!*ptr)
      break;
  if ((void *) ptr == &sb + 1)
    for (sec++; ; sec++)
      {
	err = grub_disk_read (dev->disk, sec, 0, sizeof (sb), &sb);
	if (err == GRUB_ERR_OUT_OF_RANGE)
	  err = grub_errno = GRUB_ERR_BAD_FS;
	if (err)
	  return NULL;
	for (ptr = sb.d; (void *) ptr < (void *) (&sb + 1)
	       && ptr < sb.d + grub_be_to_cpu32 (sb.sb.total_size); ptr++)
	  if (!*ptr)
	    break;
      }
  data = grub_malloc (sizeof (*data));
  if (!data)
    return NULL;
  data->first_file = ALIGN_UP (ptr - sb.d, GRUB_ROMFS_ALIGN) + sec * 512;
  data->disk = dev->disk;
  return data;
}

static int
grub_romfs_iterate_dir (grub_fshelp_node_t dir,
			int NESTED_FUNC_ATTR
			(*hook) (const char *filename,
				 enum grub_fshelp_filetype filetype,
				 grub_fshelp_node_t node))
{
  grub_disk_addr_t caddr;
  struct grub_romfs_file_header hdr;
  grub_size_t a = 0;
  char *name = NULL;
  unsigned nptr;
  unsigned i, j;
  for (caddr = dir->data_addr; caddr;
       caddr = grub_be_to_cpu32 (hdr.next_file) & ~(GRUB_ROMFS_ALIGN - 1))
    {
      grub_disk_addr_t naddr = caddr + sizeof (hdr);
      grub_uint32_t csum = 0;
      enum grub_fshelp_filetype filetype = GRUB_FSHELP_UNKNOWN;
      struct grub_fshelp_node *node;
      grub_err_t err;

      err = grub_disk_read (dir->data->disk, caddr >> GRUB_DISK_SECTOR_BITS,
			    caddr & (GRUB_DISK_SECTOR_SIZE - 1),
			    sizeof (hdr), &hdr);
      if (err)
	{
	  grub_free (name);
	  return 1;
	}
      for (nptr = 0; ; nptr++, naddr += 16)
	{
	  if (a >= nptr)
	    {
	      char *on;
	      a = 2 * (nptr + 1);
	      on = name;
	      name = grub_realloc (name, a * 16);
	      if (!name)
		{
		  grub_free (on);
		  return 1;
		}
	    }
	  err = grub_disk_read (dir->data->disk, naddr >> GRUB_DISK_SECTOR_BITS,
				naddr & (GRUB_DISK_SECTOR_SIZE - 1),
				16, name + 16 * nptr);
	  if (err)
	    return 1;
	  for (j = 0; j < 16; j++)
	    if (!name[16 * nptr + j])
	      break;
	  if (j != 16)
	    break;
	}
      for (i = 0; i < sizeof (hdr) / sizeof (grub_uint32_t); i++)
	csum += grub_be_to_cpu32 (((grub_uint32_t *) &hdr)[i]);
      for (i = 0; i < (nptr + 1) * 4; i++)
	csum += grub_be_to_cpu32 (((grub_uint32_t *) name)[i]);
      if (csum != 0)
	{
	  grub_error (GRUB_ERR_BAD_FS, "invalid checksum");
	  grub_free (name);
	  return 1;
	}
      node = grub_malloc (sizeof (*node));
      if (!node)
	return 1;
      node->addr = caddr;
      node->data_addr = caddr + (nptr + 1) * 16 + sizeof (hdr);
      node->data = dir->data;
      node->file = hdr;
      switch (grub_be_to_cpu32 (hdr.next_file) & GRUB_ROMFS_TYPE_MASK)
	{
	case GRUB_ROMFS_TYPE_REGULAR:
	  filetype = GRUB_FSHELP_REG;
	  break;
	case GRUB_ROMFS_TYPE_DIRECTORY:
	  node->data_addr = grub_be_to_cpu32 (hdr.spec);
	  filetype = GRUB_FSHELP_DIR;
	  break;
	case GRUB_ROMFS_TYPE_HARDLINK:
	  {
	    grub_disk_addr_t laddr;
	    node->addr = laddr = grub_be_to_cpu32 (hdr.spec);
	    err = grub_disk_read (dir->data->disk,
				  laddr >> GRUB_DISK_SECTOR_BITS,
				  laddr & (GRUB_DISK_SECTOR_SIZE - 1),
				  sizeof (node->file), &node->file);
	    if (err)
	      return 1;
	    if ((grub_be_to_cpu32 (node->file.next_file) & GRUB_ROMFS_TYPE_MASK)
		== GRUB_ROMFS_TYPE_REGULAR)
	      {
		laddr += sizeof (hdr);
		while (1)
		  {
		    char buf[16];
		    err = grub_disk_read (dir->data->disk, 
					  laddr >> GRUB_DISK_SECTOR_BITS,
					  laddr & (GRUB_DISK_SECTOR_SIZE - 1),
					  16, buf);
		    if (err)
		      return 1;
		    for (i = 0; i < 16; i++)
		      if (!buf[i])
			break;
		    if (i != 16)
		      break;
		    laddr += 16;
		  }
		node->data_addr = laddr + 16;
		filetype = GRUB_FSHELP_REG;
	      }
	  if ((grub_be_to_cpu32 (node->file.next_file) & GRUB_ROMFS_TYPE_MASK)
	      == GRUB_ROMFS_TYPE_DIRECTORY)
	    {
	      node->data_addr = grub_be_to_cpu32 (node->file.spec);
	      filetype = GRUB_FSHELP_DIR;
	    }

	  break;
	  }
	}

      if (hook (name, filetype, node))
	{
	  grub_free (name);
	  return 1;
	}
    }
  grub_free (name);
  return 0;
}

static grub_err_t
grub_romfs_dir (grub_device_t device, const char *path,
		int (*hook) (const char *filename,
			     const struct grub_dirhook_info *info))
{
  struct grub_romfs_data *data = 0;
  struct grub_fshelp_node *fdiro = 0, start;

  auto int NESTED_FUNC_ATTR iterate (const char *filename,
				     enum grub_fshelp_filetype filetype,
				     grub_fshelp_node_t node);

  int NESTED_FUNC_ATTR iterate (const char *filename,
				enum grub_fshelp_filetype filetype,
				grub_fshelp_node_t node)
    {
      struct grub_dirhook_info info;
      grub_memset (&info, 0, sizeof (info));

      info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
      grub_free (node);
      return hook (filename, &info);
    }

  data = grub_romfs_mount (device);
  if (! data)
    goto fail;

  start.addr = data->first_file;
  start.data_addr = data->first_file;
  start.data = data;
  grub_fshelp_find_file (path, &start, &fdiro, grub_romfs_iterate_dir,
			 NULL, GRUB_FSHELP_DIR);
  if (grub_errno)
    goto fail;

  grub_romfs_iterate_dir (fdiro, iterate);

 fail:
  grub_free (data);

  return grub_errno;
}

static grub_err_t
grub_romfs_open (struct grub_file *file, const char *name)
{
  struct grub_romfs_data *data = 0;
  struct grub_fshelp_node *fdiro = 0, start;

  data = grub_romfs_mount (file->device);
  if (! data)
    goto fail;

  start.addr = data->first_file;
  start.data_addr = data->first_file;
  start.data = data;

  grub_fshelp_find_file (name, &start, &fdiro, grub_romfs_iterate_dir,
			 NULL, GRUB_FSHELP_REG);
  if (grub_errno)
    goto fail;

  file->size = grub_be_to_cpu32 (fdiro->file.size);
  file->data = fdiro;

 fail:
  grub_free (data);

  return grub_errno;
}

static grub_ssize_t
grub_romfs_read (grub_file_t file, char *buf, grub_size_t len)
{
  struct grub_fshelp_node *data = file->data;

  /* XXX: The file is stored in as a single extent.  */
  data->data->disk->read_hook = file->read_hook;
  grub_disk_read (data->data->disk,
		  (data->data_addr + file->offset) >> GRUB_DISK_SECTOR_BITS,
		  (data->data_addr + file->offset) & (GRUB_DISK_SECTOR_SIZE - 1),		  
		  len, buf);
  data->data->disk->read_hook = NULL;

  if (grub_errno)
    return -1;

  return len;
}

static grub_err_t
grub_romfs_close (grub_file_t file)
{
  grub_free (file->data);

  return GRUB_ERR_NONE;
}

static struct grub_fs grub_romfs_fs =
  {
    .name = "romfs",
    .dir = grub_romfs_dir,
    .open = grub_romfs_open,
    .read = grub_romfs_read,
    .close = grub_romfs_close,
#ifdef GRUB_UTIL
    .reserved_first_sector = 0,
#endif
    .next = 0
  };

GRUB_MOD_INIT(romfs)
{
  grub_fs_register (&grub_romfs_fs);
}

GRUB_MOD_FINI(romfs)
{
  grub_fs_unregister (&grub_romfs_fs);
}
