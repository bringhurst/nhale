/*
 * Copyright(C) 2010 Andrew Powell
 *
 * Copyright(C) 2001-2007 Roderick Colenbrander
 * The original author
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <netinet/in.h> /* needed for htonl */
#include <sys/mman.h> 
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "back_linux.h"
#include "info.h"

#define PCI_GET_BUS(devbusfn) ((devbusfn >> 8) & 0xff)
#define PCI_GET_DEVICE(devbusfn) ((devbusfn & 0xff) >> 3)
#define PCI_GET_FUNCTION(devbusfn) (devbusfn & 0x7)

/* Check if we are using the closed source Nvidia drivers */
int check_driver()
{
  FILE *proc;
  char buffer[80];

  proc = fopen("/proc/modules", "r");

  /* Don't crash when there's no /proc/modules */
  if(proc == NULL)
    return 0;

  while(fgets(buffer, 80, proc) != NULL)
  {
    char name[80];
    int size;
    int used;

    /* Check to see if NVdriver/nvidia is loaded and if it is used.
    /  For various versions the driver isn't initialized when X hasn't
    /  been started and it can crash then.
    */
    if(sscanf(buffer,"%s %d %d",name, &size, &used) != 3) continue;

    if(strcmp(name, "NVdriver") == 0)
    {
      fclose(proc);
      if(used)
        return 1;

      return 0;
    }

    if(strcmp(name, "nvidia") == 0)
    {
      fclose(proc);
      if(used)
        return 2;

      return 0;
    }
  }
  fclose(proc);

  return 0;
}

unsigned int probe_devices(NVCard *nvcard_list)
{
  int dev, irq, reg_addr, i=0;
  unsigned short devbusfn;
  char buf[256];
  FILE *proc;

  proc = fopen("/proc/bus/pci/devices", "r");
  if(!proc)
  {
    printf("Can't open /proc/bus/pci/devices to detect your videocard.");
    return 0;
  }

  while(fgets(buf, sizeof(buf)-1, proc))
  {
    if(sscanf(buf,"%hx %x %x %x",&devbusfn, &dev, &irq, &reg_addr) != 4) continue;

    /* Check if the card contains an Nvidia chipset */
    if((dev>>16) == 0x10de)
    {
      /*
      When we enter this block of code we know that the device contains some
      chip designed by Nvidia. In the past Nvidia only produced videochips but
      now they also make various other devices. Because of this we need to find
      out if the device is a videocard or not. There are two ways to do this. We can
      create a list of all Nvidia videochips or we can check the pci header of the device.
      We will read the pci header from /proc/bus/pci/(bus)/(function).(device). When
      the card is in our card database we report the name of the card and else we say
      it is an unknown card.
      */

      if(!IsVideoCard(devbusfn))
        continue;

      if(i == MAX_CARDS)
      {
        fprintf(stderr, "Error: stopped probing for video cards after discovering %d video cards\n", MAX_CARDS);
        return i;
      }

      nvcard_list[i].device_id = 0x0000ffff & dev;
      nvcard_list[i].arch = get_gpu_arch(nvcard_list[i].device_id);
      get_card_name(nvcard_list[i].device_id, nvcard_list[i].adapter_name);

      /*
      Thanks to all different driver version this is needed now.
      When nv_driver > 1 the nvidia kernel module is loaded. 
      For driver versions < 1.0-40xx the register offset could be set to 0.
      Thanks to a rewritten kernel module in 1.0-40xx the register offset needs
      to be set again to the real offset.
      */
      switch(check_driver())
      {
        case 0:
          nvcard_list[i].dev_name = strdup("/dev/mem");
          nvcard_list[i].reg_address = reg_addr;
          break;
        case 1:
          nvcard_list[i].dev_name = (char *)calloc(13, sizeof(char));
          sprintf(nvcard_list[i].dev_name, "/dev/nvidia%d", i);
          nvcard_list[i].reg_address = 0;
          break;
        case 2:
          nvcard_list[i].dev_name = (char *)calloc(13, sizeof(char));
          sprintf(nvcard_list[i].dev_name, "/dev/nvidia%d", i);
          nvcard_list[i].reg_address = reg_addr;
          break;
      }

      i++;
    }
  }
  fclose(proc);

  return i;
}

/* Check if the device is a videocard */
int IsVideoCard(unsigned short devbusfn)
{
  int32_t pci_class = pciReadLong(devbusfn, 0x9);
  /* When the id isn't 0x03 the card isn't a vga card return 0 */
  return (((htonl(pci_class) >> 8) & 0xf) == 0x03);
}

int32_t pciReadLong(unsigned short devbusfn, long offset)
{
  char file[25];
  FILE *device;
  short bus = PCI_GET_BUS(devbusfn);
  short dev = PCI_GET_DEVICE(devbusfn);
  short function = PCI_GET_FUNCTION(devbusfn);

  snprintf(file, sizeof(file), "/proc/bus/pci/%02x/%02x.%x", bus, dev, function);
  if((device = fopen(file, "r")) != NULL)
  {
    int32_t buffer;
    fseek(device, offset, SEEK_SET);
    fread(&buffer, sizeof(int32_t), 1, device);
    fclose(device);

    return buffer;
  }

  return -1;
}

int map_mem(const char *dev_name)
{
  int fd;

  if( (fd = open(dev_name, O_RDWR)) == -1 )
  {
    printf("Can't open %s", dev_name);
    return 0;
  }

  /* Map the registers of the nVidia chip */
  /* normally pmc is till 0x2000 but extended it for nv40 */
  nv_card->PMC     = (unsigned int *)map_dev_mem(fd, nv_card->reg_address + 0x000000, 0x2ffff);
  nv_card->PDISPLAY = (unsigned int *)map_dev_mem(fd, nv_card->reg_address + NV_PDISPLAY_OFFSET, NV_PDISPLAY_SIZE);
  nv_card->PRAMIN  = (unsigned int *)map_dev_mem(fd, nv_card->reg_address + NV_PRAMIN_OFFSET, NV_PRAMIN_SIZE);
  nv_card->PROM    = (unsigned char *)map_dev_mem(fd, nv_card->reg_address + NV_PROM_OFFSET, NV_PROM_SIZE);

  close(fd);
  return 1;
}

void unmap_mem()
{
  unmap_dev_mem((unsigned long)nv_card->PMC, 0xffff);
  unmap_dev_mem((unsigned long)nv_card->PDISPLAY, NV_PDISPLAY_SIZE);
  unmap_dev_mem((unsigned long)nv_card->PRAMIN, NV_PRAMIN_SIZE);
  unmap_dev_mem((unsigned long)nv_card->PROM, NV_PROM_SIZE);
}

/* -------- mmap on devices -------- */
/* This piece of code is from nvtv a linux program for tvout */
/* The author of nvtv got this from xfree86's os-support/linux/lnx_video.c */
/* and he modified it a little  */
void *map_dev_mem (int fd, unsigned long Base, unsigned long Size)
{
  void *base;
  int mapflags = MAP_SHARED;
  unsigned long realBase, alignOff;

  realBase = Base & ~(getpagesize() - 1);
  alignOff = Base - realBase;

  base = mmap((caddr_t)0, Size + alignOff, PROT_READ|PROT_WRITE,
  mapflags, fd, (off_t)realBase);
  return (void *) ((char *)base + alignOff);
}

void unmap_dev_mem (unsigned long Base, unsigned long Size)
{
  unsigned long alignOff = Base - (Base & ~(getpagesize() - 1));
  munmap((caddr_t)(Base - alignOff), (Size + alignOff));
}
