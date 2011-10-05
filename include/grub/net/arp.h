/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011  Free Software Foundation, Inc.
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

#ifndef GRUB_NET_ARP_HEADER
#define GRUB_NET_ARP_HEADER	1
#include <grub/misc.h>
#include <grub/net.h>

extern grub_err_t grub_net_arp_receive(struct grub_net_buff *nb);

extern grub_err_t grub_net_arp_resolve(struct grub_net_network_level_interface *inf,
			      const grub_net_network_level_address_t *addr, 
			      grub_net_link_level_address_t *hw_addr);

#endif 
