// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//
// GreedyFTL header file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//

#ifndef FTL_H
#define FTL_H

/////////////////
// ZONE 
/////////////////

#define DEG_ZONE  NUM_BANKS
#define NUM_FCG  (NUM_BANKS / DEG_ZONE)
#define ZONE_SIZE (PAGES_PER_VBLK*DEG_ZONE*SECTORS_PER_PAGE)
#define NPAGE PAGES_PER_VBLK
#define NSECT SECTORS_PER_PAGE
#define NBLK  VBLKS_PER_BANK
#define NZONE (NUM_FCG*NBLK)

#define MAX_OPEN_ZONE 100

/////////////////
// DRAM buffers
/////////////////

#define NUM_RW_BUFFERS		((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS		(((NUM_RW_BUFFERS / 8) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS		(NUM_RW_BUFFERS - NUM_RD_BUFFERS)
#define NUM_COPY_BUFFERS	NUM_BANKS_MAX
#define NUM_FTL_BUFFERS		NUM_BANKS
#define NUM_HIL_BUFFERS		1
#define NUM_TEMP_BUFFERS	1

#define DRAM_BYTES_OTHER	((NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS) * BYTES_PER_PAGE + BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + VCOUNT_BYTES + ZONE_STATE_BYTES + ZONE_WP_BYTES + ZONE_SLBA_BYTES +ZONE_BUFFER_BYTES +ZONE_TO_FBG_BYTES + FBQ_BYTES + OPEN_ZONE_Q_BYTES + ZONE_TO_ID_BYTES)

#define WR_BUF_PTR(BUF_ID)	(WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID)	(RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)

#define _COPY_BUF(RBANK)	(COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)		_COPY_BUF(REAL_BANK(BANK))
#define FTL_BUF(BANK)       (FTL_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))

///////////////////////////////
// DRAM segmentation
///////////////////////////////

#define RD_BUF_ADDR			DRAM_BASE										// base address of SATA read buffers
#define RD_BUF_BYTES		(NUM_RD_BUFFERS * BYTES_PER_PAGE)

#define WR_BUF_ADDR			(RD_BUF_ADDR + RD_BUF_BYTES)					// base address of SATA write buffers
#define WR_BUF_BYTES		(NUM_WR_BUFFERS * BYTES_PER_PAGE)

#define COPY_BUF_ADDR		(WR_BUF_ADDR + WR_BUF_BYTES)					// base address of flash copy buffers
#define COPY_BUF_BYTES		(NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_BUF_ADDR		(COPY_BUF_ADDR + COPY_BUF_BYTES)				// a buffer dedicated to FTL internal purpose
#define FTL_BUF_BYTES		(NUM_FTL_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR		(FTL_BUF_ADDR + FTL_BUF_BYTES)					// a buffer dedicated to HIL internal purpose
#define HIL_BUF_BYTES		(NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR		(HIL_BUF_ADDR + HIL_BUF_BYTES)					// general purpose buffer
#define TEMP_BUF_BYTES		(NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define BAD_BLK_BMP_ADDR	(TEMP_BUF_ADDR + TEMP_BUF_BYTES)				// bitmap of initial bad blocks
#define BAD_BLK_BMP_BYTES	(((NUM_VBLKS / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)

#define PAGE_MAP_ADDR		(BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)			// page mapping table
#define PAGE_MAP_BYTES		((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define VCOUNT_ADDR			(PAGE_MAP_ADDR + PAGE_MAP_BYTES)
#define VCOUNT_BYTES		((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define ZONE_STATE_ADDR		(VCOUNT_ADDR + VCOUNT_BYTES)
#define ZONE_STATE_BYTES	((NBLK * sizeof(UINT8) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)
#define ZONE_WP_ADDR		(ZONE_STATE_ADDR + ZONE_STATE_BYTES)
#define ZONE_WP_BYTES		((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)
#define ZONE_SLBA_ADDR		(ZONE_WP_ADDR + ZONE_WP_BYTES)
#define ZONE_SLBA_BYTES		((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define ZONE_BUFFER_ADDR	(ZONE_SLBA_ADDR + ZONE_SLBA_BYTES)
#define ZONE_BUFFER_BYTES	((MAX_OPEN_ZONE * BYTES_PER_PAGE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define ZONE_TO_FBG_ADDR	(ZONE_BUFFER_ADDR + ZONE_BUFFER_BYTES)
#define ZONE_TO_FBG_BYTES	((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define FBQ_ADDR			(ZONE_TO_FBG_ADDR + ZONE_TO_FBG_BYTES)
#define FBQ_BYTES			((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define OPEN_ZONE_Q_ADDR 	FBQ_ADDR + FBQ_BYTES
#define OPEN_ZONE_Q_BYTES	MAX_OPEN_ZONE * sizeof(UINT8)

#define ZONE_TO_ID_ADDR		OPEN_ZONE_Q_ADDR + OPEN_ZONE_Q_BYTES
#define ZONE_TO_ID_BYTES	MAX_OPEN_ZONE * sizeof(UINT8)



#define DRAM_TOP			ZONE_TO_TL_ID_ADDR + ZONE_TO_TL_ID_BYTES
	
// ZNS+
/*

#define TL_BITMAP_ADDR		(ZONE_TO_TL_ID_ADDR + ZONE_TO_TL_ID_BYTES)
#define TL_BITMAP_BYTES		((NBLK * (NPAGE*DEG_ZONE) * sizeof(UINT8) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define TL_WP_ADDR			(TL_BITMAP_ADDR + TL_BITMAP_BYTES)
#define TL_WP_BYTES			((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define TL_NUM_ADDR			(TL_WP_ADDR + TL_WP_BYTES)
#define TL_NUM_BYTES		((NBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define TL_INTERNAL_BUFFER_ADDR			(TL_NUM_ADDR + TL_NUM_BYTES)
#define TL_INTERNAL_BUFFER_BYTES		(BYTES_PER_PAGE)

*/


///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open(void);
void ftl_read(UINT32 const lba, UINT32 const num_sectors);
void ftl_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
void ftl_isr(void);

#endif //FTL_H
