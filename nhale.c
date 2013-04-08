/*
 * Copyright(C) 2010 Andrew Powell
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "backend.h"
#include "back_linux.h"
#include "bios.h"

//hacker.c/developer.c to trace test byte ptr's and call's?

//NOTICE, NOTE, FIXME, TODO
//TODO: Use uintn_t's where necessary.
//TODO: PCI BIOS?
//TODO: Look at xf86 qt 7.6 (or 7.4?) mouse cursor lag issue?
//TODO: Show MXM version
//TODO: print the BDF PCI info?
//TODO: read EEPROM ID and map to name; chip is SPI, use spi (write) to probe
//TODO: Assert NV_PROM_SIZE <= Physical EEPROM size
//TODO: Use CRC for verification on PRAMIN and PROM dumps
//TODO: Determine memory manufacturer?  I can either use i2c or find it in rom?
//TODO: make a g_debug_print
//TODO: expand bios caps to indicate what values the bios has
//NOTE: 0x100 seems to be important value for nvflash parsing on my 3600M
//TODO: Try to find hidden rom in system bios on laptops with no video bios stored on PROM (integrate with flashrom) ?
//TODO: Make a log.c to log all tables for development
//TODO: Move the script.c here
//TODO: MAKE THE GUI ALREADY!

NVCard *nv_card;

void usage(void)
{
  printf("\nnhale v0.1\n");
  printf("Yet another Nvidia rom editor.  Use this program at your own risk.\n\n");

  printf("Options:\n");
  printf("   --list\t\t\tList all detected nvidia cards and their\n\t\t\t\tindices.\n");
  printf("   -l, --load <filename>\tLoad input file.\n");
  printf("   -s, --save <filename>\tSave output file.\n");
  printf("   -i, --index <num>\t\tUse card at this index for all operations.\n\t\t\t\tFind indices with --list.\n");
  printf("   -n, --no-checksum\t\tDo not correct checksum on file save.\n");
  printf("   -p, --info\t\t\tPrint the rom information.\n");
  printf("   -r, --ram\t\t\tAttempt to shadow bios from Video Ram (PRAMIN)\n\t\t\t\tbefore PROM.\n");
  printf("   -f, --force\t\t\tForce bios writing to unsupported architectures\n");
  printf("   -v, --verbose\t\tPrint verbose information.\n");
  printf("   -h, --help\t\t\tPrint this usage information.\n\n");
}

int main(int argc, char **argv)
{
  int c;
  unsigned int i;
  NVCard card_list[MAX_CARDS];
  struct nvbios bios;
  char *infile = NULL, *outfile = NULL;
  unsigned int card_index = 0;
  unsigned char card_index_flag = 0;
  unsigned int num_cards = 0;
  static int list_flag = 0;
  int print_info = 0;
  int option_index = 0;  // getopt_long stores the option index here

  if(argc == 1)
  {
    usage();
    return 0;
  }

  memset(&bios, 0, sizeof(struct nvbios));  //FIXME?

  static struct option long_options[] =
  {
    {"list",        no_argument,       &list_flag, 1},
    {"load",        required_argument, 0, 'l'},
    {"save",        required_argument, 0, 's'},
    {"index",       required_argument, 0, 'i'},
    {"no-checksum", no_argument,       0, 'n'},
    {"info"       , no_argument,       0, 'p'},
    {"ram"        , no_argument,       0, 'r'},
    {"force"      , no_argument,       0, 'f'},
    {"verbose"    , no_argument,       0, 'v'},
    {"help"       , no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  while((c = getopt_long (argc, argv, "nprfvhl:s:i:", long_options, &option_index)) != -1)
  {
    switch(c)
    {
      case 0:
        break;
      case 'l':
        infile = strdup(optarg);
        break;
      case 's':
        outfile = strdup(optarg);
        break;
      case 'i':
        card_index = atoi(optarg);
        card_index_flag = 1;
        break;
      case 'n':
        bios.no_correct_checksum = 1;
        break;
      case 'p':
        print_info = 1;
        break;
      case 'r':
        bios.pramin_priority = 1;
        break;
      case 'f':
        bios.force = 1;
        break;
      case 'v':
        bios.verbose = 1;
        break;
      case 'h':
        usage();
        break;
      default:
        usage();
        return -1;
    }
  }

  if(optind < argc)
  {
    usage();
    return -1;
  }

  num_cards = probe_devices(card_list);
  if(!infile)
  {
    switch(num_cards)
    {
      case 0:
        printf("No Nvidia cards detected");
        return -1;
      case 1:
        if(card_index)
          fprintf(stderr, "Only detected one card.  Overwriting user-specified index: %d\n", card_index);
        card_index = 0;
        break;
      default:
        if(!card_index_flag)
        {
          if(!list_flag)
          {
            printf("There are multiple Nvidia cards detected on this machine.\n");
            printf("Please use -i or --index to specify which card to use for operations\n");
            return -1;
          }
        }
        if(card_index >= num_cards)
        {
          printf("Invalid card index\n");
          return -1;
        }
    }
  }

  if(list_flag)
  {
    printf("\nIndex\t\tDevice ID\tAdapter Name\n");
    for(i = 0; i < num_cards; i++)
    {
      printf("%02d\t\t%04X\t\t%s\n", i, card_list[i].device_id, card_list[i].adapter_name);
    }
    printf("\n");
  }

  // Nothing left to do
  if(!outfile && !print_info)
    return 0;

  if(!infile)
  {
    nv_card = card_list + card_index;
    if(!map_mem(nv_card->dev_name))
      return -1;
  }

  if(read_bios(&bios, infile))
  {
    if(print_info)
      print_bios_info(&bios);

    if(outfile)
      if(!write_bios(&bios, outfile))
        printf("Error: Unable to dump the rom image\n");
  }

  if(!infile)
    unmap_mem();

  return 0;
}
