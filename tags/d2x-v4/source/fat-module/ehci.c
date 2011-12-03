/*   
	Custom IOS Module (FAT)

	Copyright (C) 2008 neimod.
	Copyright (C) 2009 WiiGator.
	Copyright (C) 2010 Waninkoko.
	Copyright (C) 2011 davebaol.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>
#include <string.h>

#include "ehci.h"
#include "mem.h"
#include "syscalls.h"
#include "timer.h"
#include "types.h"


/* IOCTL commands */
#define UMS_BASE			(('U'<<24) | ('M'<<16) | ('S'<<8))
#define USB_IOCTL_UMS_INIT	        (UMS_BASE + 0x1)
#define USB_IOCTL_UMS_GET_CAPACITY      (UMS_BASE + 0x2)
#define USB_IOCTL_UMS_READ_SECTORS      (UMS_BASE + 0x3)
#define USB_IOCTL_UMS_WRITE_SECTORS	(UMS_BASE + 0x4)
#define USB_IOCTL_UMS_READ_STRESS	(UMS_BASE + 0x5)
#define USB_IOCTL_UMS_SET_VERBOSE	(UMS_BASE + 0x6)

/* Constants */
#define USB_MAX_SECTORS			64

/* Variables */
static char fs[] ATTRIBUTE_ALIGN(32) = "/dev/usb2";
static s32  fd = -1;

static u32 sectorSz = 0;

/* Buffers */
static ioctlv __iovec[3]   ATTRIBUTE_ALIGN(32);
static u32    __buffer1[1] ATTRIBUTE_ALIGN(32);
static u32    __buffer2[1] ATTRIBUTE_ALIGN(32);


bool __ehci_Read(u32 sector, u32 numSectors, void *buffer)
{
	ioctlv *vector = __iovec;
	u32    *offset = __buffer1;
	u32    *length = __buffer2;

	u32 cnt, len = (sectorSz * numSectors);
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	/* Sector info */
	*offset = sector;
	*length = numSectors;

	/* Setup vector */
	vector[0].data = offset;
	vector[0].len  = sizeof(u32);
	vector[1].data = length;
	vector[1].len  = sizeof(u32);
	vector[2].data = buffer;
	vector[2].len  = len;

	/* Flush cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_after_write(vector[cnt].data, vector[cnt].len);

	os_sync_after_write(vector, sizeof(ioctlv) * 3);

	/* Read data */
	ret = os_ioctlv(fd, USB_IOCTL_UMS_READ_SECTORS, 2, 1, vector);
	if (ret)
		return false;

	/* Invalidate cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_before_read(vector[cnt].data, vector[cnt].len);

	return true;
}

bool __ehci_Write(u32 sector, u32 numSectors, void *buffer)
{
	ioctlv *vector = __iovec;
	u32    *offset = __buffer1;
	u32    *length = __buffer2;

	u32 cnt, len = (sectorSz * numSectors);
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	/* Sector info */
	*offset = sector;
	*length = numSectors;

	/* Setup vector */
	vector[0].data = offset;
	vector[0].len  = sizeof(u32);
	vector[1].data = length;
	vector[1].len  = sizeof(u32);
	vector[2].data = buffer;
	vector[2].len  = len;

	/* Flush cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_after_write(vector[cnt].data, vector[cnt].len);

	os_sync_after_write(vector, sizeof(ioctlv) * 3);

	/* Write data */
	ret = os_ioctlv(fd, USB_IOCTL_UMS_WRITE_SECTORS, 3, 0, vector);
	if (ret)
		return false;

	/* Invalidate cache */
	for (cnt = 0; cnt < 3; cnt++)
		os_sync_before_read(vector[cnt].data, vector[cnt].len);

	return true;
}

// FIX:
// This function has been modified in d2x v4beta1.
// Now it returns an unsigned int to support HDD greater than 1TB.
// If 0 is returned then an error has occurred.
u32 __ehci_GetCapacity(u32 *_sectorSz)
{
	ioctlv *vector = __iovec;
	u32    *buffer = __buffer1;

	if (fd >= 0) {
		u32 nbSector;

		/* Setup vector */
		vector[0].data = buffer;
		vector[0].len  = sizeof(u32);

		os_sync_after_write(vector, sizeof(ioctlv));

		/* Get capacity */
		nbSector = os_ioctlv(fd, USB_IOCTL_UMS_GET_CAPACITY, 0, 1, vector);

		/* Flush cache */
		os_sync_after_write(buffer, sizeof(u32));

		/* Set sector size */
		sectorSz = buffer[0];

		if (nbSector && _sectorSz)
			*_sectorSz = sectorSz;

		return nbSector;
	}

	return 0;
}


bool ehci_Init(void)
{
	u32 cnt = 10;
	s32 ret = 1;

	/* Already open */
	if (fd >= 0)
		return true;

	/* Open USB device */
	fd = os_open(fs, 0);
	if (fd < 0)
		return false;

	/* Initialize USB storage */
	while ((cnt-- > 0) && ret) {
		/* Init UMS */
		ret = os_ioctlv(fd, USB_IOCTL_UMS_INIT, 0, 0, NULL);
		if (!ret)
			break;

		/* Sleep */
		msleep(1);
	}

	/* Error */
	if (ret)
		goto err;

	/* Get device capacity */
	ret = __ehci_GetCapacity(NULL);
	if (ret == 0)
		goto err;

	return true;

err:
	/* Close USB device */
	ehci_Shutdown();

	return false;
}

bool ehci_Shutdown(void)
{
	/* Close USB device */
	if (fd >= 0)
		os_close(fd);

	/* Reset descriptor */
	fd = -1;

	return true;
}

bool ehci_IsInserted(void)
{
	u32 nbSector;

	/* Get device capacity */
	nbSector = __ehci_GetCapacity(NULL);

	return (nbSector != 0);
}

bool ehci_ReadSectors(u32 sector, u32 numSectors, void *buffer)
{
	u32 cnt = 0;
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	while (cnt < numSectors) {
		void *ptr = (char *)buffer + (sectorSz * cnt);

		u32  _sector     = sector + cnt;
		u32  _numSectors = numSectors - cnt;

		/* Limit sector count */
		if (_numSectors > USB_MAX_SECTORS)
			_numSectors = USB_MAX_SECTORS;

		/* Read sectors */
		ret = __ehci_Read(_sector, _numSectors, ptr);
		if (!ret)
			return false;

		/* Increase counter */
		cnt += _numSectors;
	}

	return true;
}

bool ehci_WriteSectors(u32 sector, u32 numSectors, void *buffer)
{
	u32 cnt = 0;
	s32 ret;

	/* Device not opened */
	if (fd < 0)
		return false;

	while (cnt < numSectors) {
		void *ptr = (char *)buffer + (sectorSz * cnt);

		u32  _sector     = sector + cnt;
		u32  _numSectors = numSectors - cnt;

		/* Limit sector count */
		if (_numSectors > USB_MAX_SECTORS)
			_numSectors = USB_MAX_SECTORS;

		/* Write sectors */
		ret = __ehci_Write(_sector, _numSectors, ptr);
		if (!ret)
			return false;

		/* Increase counter */
		cnt += _numSectors;
	}

	return true;
}

// Function added in d2x v4beta4 to support hard drives
// with sector size up to 4KB.
u32 ehci_GetSectorSize()
{
	/* Return sector size or 0 if device not opened */
	return (fd < 0) ? 0 : sectorSz;
}

bool ehci_ClearStatus(void)
{
	return true;
}
