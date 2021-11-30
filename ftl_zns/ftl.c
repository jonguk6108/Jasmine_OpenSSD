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
// GreedyFTL source file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//
// - support POR
//  + fixed metadata area (Misc. block/Map block)
//  + logging entire FTL metadata when each ATA commands(idle/ready/standby) was issued
//

#include "jasmine.h"

//----------------------------------
// macro
//----------------------------------
#define VC_MAX              0xCDCD
#define MISCBLK_VBN         0x1 // vblock #1 <- misc metadata
#define MAPBLKS_PER_BANK    (((PAGE_MAP_BYTES / NUM_BANKS) + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE)
#define META_BLKS_PER_BANK  (1 + 1 + MAPBLKS_PER_BANK) // include block #0, misc block

// the number of sectors of misc. metadata info.
#define NUM_MISC_META_SECT  ((sizeof(misc_metadata) + BYTES_PER_SECTOR - 1)/ BYTES_PER_SECTOR)
#define NUM_VCOUNT_SECT     ((VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR)

//----------------------------------
// metadata structure
//----------------------------------
typedef struct _ftl_statistics
{
    UINT32 gc_cnt;
	UINT32 gc_write;
	UINT32 host_write;
	UINT32 nand_write;
    UINT32 page_wcount; // page write count
}ftl_statistics;

typedef struct _misc_metadata
{
    UINT32 cur_write_vpn; // physical page for new write
    UINT32 cur_miscblk_vpn; // current write vpn for logging the misc. metadata
    UINT32 cur_mapblk_vpn[MAPBLKS_PER_BANK]; // current write vpn for logging the age mapping info.
    UINT32 gc_vblock; // vblock number for garbage collection
    UINT32 free_blk_cnt; // total number of free block count
    UINT32 lpn_list_of_cur_vblock[PAGES_PER_BLK]; // logging lpn list of current write vblock for GC
}misc_metadata; // per bank

//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
static misc_metadata  g_misc_meta[NUM_BANKS];
static ftl_statistics g_ftl_statistics[NUM_BANKS];
static UINT32		  g_bad_blk_count[NUM_BANKS];
UINT32 rp,wp;
UINT32 wp_open, rp_open;
UINT32 OPEN_ZONE;
UINT32 rand_write_blks;

// SATA read/write buffer pointer id
UINT32 				  g_ftl_read_buf_id;
UINT32 				  g_ftl_write_buf_id;

//----------------------------------
// NAND layout
//----------------------------------
// block #0: scan list, firmware binary image, etc.
// block #1: FTL misc. metadata
// block #2 ~ #31: page mapping table
// block #32: a free block for gc
// block #33~: user data blocks

//----------------------------------
// macro functions
//----------------------------------
#define is_full_all_blks(bank)  (g_misc_meta[bank].free_blk_cnt == 1)
#define inc_full_blk_cnt(bank)  (g_misc_meta[bank].free_blk_cnt--)
#define dec_full_blk_cnt(bank)  (g_misc_meta[bank].free_blk_cnt++)
#define inc_mapblk_vpn(bank, mapblk_lbn)    (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn]++)
#define inc_miscblk_vpn(bank)               (g_misc_meta[bank].cur_miscblk_vpn++)

// page-level striping technique (I/O parallelism)
#define get_num_bank(lpn)             ((lpn) % NUM_BANKS)
#define get_bad_blk_cnt(bank)         (g_bad_blk_count[bank])
#define get_cur_write_vpn(bank)       (g_misc_meta[bank].cur_write_vpn)
#define set_new_write_vpn(bank, vpn)  (g_misc_meta[bank].cur_write_vpn = vpn)
#define get_gc_vblock(bank)           (g_misc_meta[bank].gc_vblock)
#define set_gc_vblock(bank, vblock)   (g_misc_meta[bank].gc_vblock = vblock)
#define set_lpn(bank, page_num, lpn)  (g_misc_meta[bank].lpn_list_of_cur_vblock[page_num] = lpn)
#define get_lpn(bank, page_num)       (g_misc_meta[bank].lpn_list_of_cur_vblock[page_num])
#define get_miscblk_vpn(bank)         (g_misc_meta[bank].cur_miscblk_vpn)
#define set_miscblk_vpn(bank, vpn)    (g_misc_meta[bank].cur_miscblk_vpn = vpn)
#define get_mapblk_vpn(bank, mapblk_lbn)      (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn])
#define set_mapblk_vpn(bank, mapblk_lbn, vpn) (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn] = vpn)
#define CHECK_LPAGE(lpn)              ASSERT((lpn) < NUM_LPAGES)
#define CHECK_VPAGE(vpn)              ASSERT((vpn) < (rand_write_blks * PAGES_PER_BLK))

//----------------------------------
// FTL internal function prototype
//----------------------------------
static void   format(void);
static void   write_format_mark(void);
static void   sanity_check(void);
static void   load_pmap_table(void);
static void   load_misc_metadata(void);
static void   init_metadata_sram(void);
static void   load_metadata(void);
static void   logging_pmap_table(void);
static void   logging_misc_metadata(void);
static void   write_page(UINT32 const lpn, UINT32 const sect_offset, UINT32 const num_sectors);
static void   set_vpn(UINT32 const lpn, UINT32 const vpn);
static void   garbage_collection(UINT32 const bank);
static void   set_vcount(UINT32 const bank, UINT32 const vblock, UINT32 const vcount);
static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblock);
static BOOL32 check_format_mark(void);
static UINT32 get_vcount(UINT32 const bank, UINT32 const vblock);
static UINT32 get_vpn(UINT32 const lpn);
static UINT32 get_vt_vblock(UINT32 const bank);
static UINT32 assign_new_write_vpn(UINT32 const bank);
static void zns_read(UINT32 const start_lba, UINT32 const num_sectors, UINT32 const read_buffer_addr, UINT32 const swch);
static void zns_write(UINT32 const start_lba, UINT32 const num_sectors, UINT32 const write_buffer_addr, UINT32 const swch);
static void zns_init(void);
static void zns_get_desc(UINT32 c_zone, UINT32 nzone);
static UINT8 get_zone_state(UINT32 zone_number);
static void set_zone_state(UINT32 zone_number, UINT8 state);
static UINT32 get_zone_wp(UINT32 zone_number);
static void set_zone_wp(UINT32 zone_number, UINT32 wp);
static UINT32 get_zone_slba(UINT32 zone_number);
static void set_zone_slba(UINT32 zone_number, UINT32 slba);
static UINT32 get_buffer_sector(UINT32 zone_number, UINT32 sector_offset);
static void set_buffer_sector(UINT32 zone_number, UINT32 sector_offset, UINT32 data);
static UINT32 get_zone_to_FBG(UINT32 zone_number);
static void set_zone_to_FBG(UINT32 zone_number, UINT32 FBG);
static void enqueue_FBG(UINT32 block_num);
static UINT32 dequeue_FBG(void);
static void zns_reset(UINT32 c_zone);
static void search_bad_blk_zone(void);
static void enqueue_open_id(UINT8 open_zone_id);
static UINT8 dequeue_open_id(void); 
static UINT8 get_zone_to_ID(UINT32 zone_number);
static void set_zone_to_ID(UINT32 zone_number, UINT8 id);

void nand_page_ptprogram_from_host_zns_write(UINT32 const bank, UINT32 const vblock, UINT32 const page_num, UINT32 const sect_offset, UINT32 const num_sectors, UINT32 const write_buffer_addr);
void nand_page_ptread_to_host_zns_read(UINT32 const bank, UINT32 const vblock, UINT32 const page_num, UINT32 const sect_offset, UINT32 const num_sectors, UINT32 const read_buffer_addr);
/*
static void zns_izc(UINT32 src_zone, UINT32 dest_zone, UINT32 copy_len, UINT32 *copy_list);
static UINT8 get_TL_bitmap(UINT32 zone_number, UINT32 page_offset);
static void set_TL_bitmap(UINT32 zone_number, UINT32 page_offset, UINT8 data);
static UINT32 get_TL_wp(UINT32 zone_number);
static void set_TL_wp(UINT32 zone_number, UINT32 wp);
static UINT32 get_TL_buffer(UINT32 zone_number, UINT32 sector_offset);
static void set_TL_buffer(UINT32 zone_number,UINT32 sector_offset, UINT32 data);
static UINT32 get_TL_num(UINT32 zone_number);
static void set_TL_num(UINT32 zone_number, UINT32 num);*/


static void sanity_check(void)
{
    UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES + FTL_BUF_BYTES
        + HIL_BUF_BYTES + TEMP_BUF_BYTES + BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + VCOUNT_BYTES;
    
    uart_printf("DRAM_BASE: 0x%x / %u",DRAM_BASE,DRAM_BASE);
    uart_printf("COPY_BUF_ADDR: 0x%x / %u", COPY_BUF_ADDR, COPY_BUF_ADDR);
    uart_printf("DRAM_TOP : 0x%x / %u", DRAM_TOP, DRAM_TOP);
    uart_printf("DRAM_SIZE: 0x%x / %u", DRAM_SIZE, DRAM_SIZE);
    uart_printf("OTHE_SIZE: 0x%x / %u", DRAM_BYTES_OTHER, DRAM_BYTES_OTHER);
    uart_printf("diff SIZE: 0x%x / %u", DRAM_SIZE - DRAM_BYTES_OTHER, DRAM_SIZE - DRAM_BYTES_OTHER);

    uart_printf("RD_BUF_BYTES   : 0x%x / %u", RD_BUF_BYTES, RD_BUF_BYTES);
    uart_printf("WR_BUF_BYTES   : 0x%x / %u", WR_BUF_BYTES, WR_BUF_BYTES);
    uart_printf("COPY_BUF_BYTES : 0x%x / %u", COPY_BUF_BYTES, COPY_BUF_BYTES);
    uart_printf("FTL_BUF_BYTES  : 0x%x / %u", FTL_BUF_BYTES, FTL_BUF_BYTES);
    uart_printf("HIL_BUF_BYTES  : 0x%x / %u", HIL_BUF_BYTES, HIL_BUF_BYTES);
    uart_printf("TEMP_BUF_BYTES : 0x%x / %u", TEMP_BUF_BYTES, TEMP_BUF_BYTES);
    uart_printf("BAD_BLK_BMP_BYTES: 0x%x / %u", BAD_BLK_BMP_BYTES, BAD_BLK_BMP_BYTES);
    uart_printf("PAGE_MAP_BYTES: 0x%x / %u", PAGE_MAP_BYTES, PAGE_MAP_BYTES);
    uart_printf("VCOUNT_BYTES: 0x%x / %u", VCOUNT_BYTES, VCOUNT_BYTES);
    uart_printf("ZONE_STATE_BYTES: 0x%x / %u", ZONE_STATE_BYTES, ZONE_STATE_BYTES);
    uart_printf("ZONE_WP_BYTES: 0x%x / %u", ZONE_WP_BYTES, ZONE_WP_BYTES);
    uart_printf("ZONE_SLBA_BYTES: 0x%x / %u", ZONE_SLBA_BYTES, ZONE_SLBA_BYTES);
    uart_printf("ZONE_BUFFER_BYTES: 0x%x / %u", ZONE_BUFFER_BYTES, ZONE_BUFFER_BYTES);
    uart_printf("ZONE_TO_FBG_BYTES: 0x%x / %u", ZONE_TO_FBG_BYTES, ZONE_TO_FBG_BYTES);
    uart_printf("FBQ_BYTES: 0x%x / %u", FBQ_BYTES, FBQ_BYTES);


    uart_printf("1 : %d %u %d %u \n", DRAM_BYTES_OTHER, DRAM_BYTES_OTHER, DRAM_SIZE - DRAM_BYTES_OTHER, DRAM_SIZE-DRAM_BYTES_OTHER);

    if ((dram_requirement > DRAM_SIZE) || // DRAM metadata size check
        (sizeof(misc_metadata) > BYTES_PER_PAGE)) // misc metadata size check
    {
        led_blink();
        while (1);
    }
    uart_printf("no matter\n");
}
static void build_bad_blk_list(void)
{
	UINT32 bank, num_entries, result, vblk_offset;
	scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;

	mem_set_dram(BAD_BLK_BMP_ADDR, NULL, BAD_BLK_BMP_BYTES);

	disable_irq();

	flash_clear_irq();

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
		SETREG(FCP_BANK, REAL_BANK(bank));
		SETREG(FCP_OPTION, FO_E);
		SETREG(FCP_DMA_ADDR, (UINT32) scan_list);
		SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);
		SETREG(FCP_COL, 0);
		SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);
		SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);

		SETREG(FCP_ISSUE, NULL);
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		while (BSP_FSM(bank) != BANK_IDLE);

		num_entries = NULL;
		result = OK;

		if (BSP_INTR(bank) & FIRQ_DATA_CORRUPT)
		{
			result = FAIL;
		}
		else
		{
			UINT32 i;

			num_entries = read_dram_16(&(scan_list->num_entries));

			if (num_entries > SCAN_LIST_ITEMS)
			{
				result = FAIL;
			}
			else
			{
				for (i = 0; i < num_entries; i++)
				{
					UINT16 entry = read_dram_16(scan_list->list + i);
					UINT16 pblk_offset = entry & 0x7FFF;

					if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK)
					{
						#if OPTION_REDUCED_CAPACITY == FALSE
						result = FAIL;
						#endif
					}
					else
					{
						write_dram_16(scan_list->list + i, pblk_offset);
					}
				}
			}
		}

		if (result == FAIL)
		{
			num_entries = 0;  // We cannot trust this scan list. Perhaps a software bug.
		}
		else
		{
			write_dram_16(&(scan_list->num_entries), 0);
		}

		g_bad_blk_count[bank] = 0;

		for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
		{
			BOOL32 bad = FALSE;

			#if OPTION_2_PLANE
			{
				UINT32 pblk_offset;

				pblk_offset = vblk_offset * NUM_PLANES;

                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}

				pblk_offset = vblk_offset * NUM_PLANES + 1;

                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}
			}
			#else
			{
                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, vblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}
			}
			#endif

			if (bad)
			{
				g_bad_blk_count[bank]++;
				set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
			}
		}
	}
}

void ftl_open(void)
{
    // debugging example 1 - use breakpoint statement!
    /* *(UINT32*)0xFFFFFFFE = 10; */

    /* UINT32 volatile g_break = 0; */
    /* while (g_break == 0); */

	led(0);
    sanity_check();
    //----------------------------------------
    // read scan lists from NAND flash
    // and build bitmap of bad blocks
    //----------------------------------------
	build_bad_blk_list();

    //----------------------------------------
	// If necessary, do low-level format
	// format() should be called after loading scan lists, because format() calls is_bad_block().
    //----------------------------------------
 	//if (check_format_mark() == FALSE) 
	if (TRUE)
	{
        uart_print("do format");
		format();
        uart_print("end format");
	}
    // load FTL metadata
    else
    {

        load_metadata();
    }
	g_ftl_read_buf_id = 0;
	g_ftl_write_buf_id = 0;
    uart_printf("here1\n");
    // This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
    flash_clear_irq();
    uart_printf("here2\n");
    SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
    uart_printf("here3\n");
	enable_irq();
    uart_printf("here4\n");
	wp = 0; rp = 0; 
    wp_open = 0; rp_open = 0;
	OPEN_ZONE = 0;	
	search_bad_blk_zone();
    uart_printf("here5\n");
	UINT32 zone_number = -1;
	for(int i = 0; i < 8; i++)
	{
		zone_number = dequeue_FBG();
	}
	rand_write_blks = zone_number + 1;
    uart_printf("here6\n");
	zns_init();
    uart_printf("here7\n");
    /****FTL 세팅값 ******/
    uart_printf("\n----------------------");
    uart_printf("NUM_LSECTORS : %d", NUM_LSECTORS);
    uart_printf("NUM_BANKS : %d", NUM_BANKS);
    uart_printf("VBLKS_PER_BANK : %d", VBLKS_PER_BANK);
    uart_printf("NUM_VBLKS : %d", NUM_VBLKS);
    uart_printf("PAGES_PER_VBLK : %d", PAGES_PER_VBLK);
    uart_printf("BYTES_PER_SECTOR : %d", BYTES_PER_SECTOR);
    uart_printf("----------------------");
    /********************/
}
void search_bad_blk_zone(void)
{
	UINT32 vcount , j ,i ;
	for(j = 0; j < VBLKS_PER_BANK; j++)
	{
		BOOL32 flag = TRUE;
		for( i = 0; i < NUM_BANKS; i++)
		{
			 vcount = read_dram_16(VCOUNT_ADDR + ((i * VBLKS_PER_BANK) + j) * sizeof(UINT16));
			if(vcount == VC_MAX)
			{				
				flag = FALSE;
				break;
			}
		}
		if(flag)
		{
			enqueue_FBG(j);
		}
	}
}
void ftl_flush(void)
{
    /* ptimer_start(); */
    logging_pmap_table();
    logging_misc_metadata();
    /* ptimer_stop_and_uart_print(); */
}
// Testing FTL protocol APIs
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors)
{
    ASSERT(lba + num_sectors <= NUM_LSECTORS);
    ASSERT(num_sectors > 0);

    ftl_write(lba, num_sectors);
}

void zns_init(void)
{
	for(UINT32 i = 0; i < NZONE; i++)
	{
		set_zone_state(i, 0);
       // uart_printf("init1\n");
		set_zone_slba(i, i * ZONE_SIZE);
       // uart_printf("init2\n");
		set_zone_wp(i, i * ZONE_SIZE);
       // uart_printf("init3\n");
		
		set_zone_to_FBG(i, -1);

       // uart_printf("%d\n",i);
		for(UINT32 j = 0; j < NSECT; j++)
		{
        //    uart_printf("%d %d\n", i,j);
			set_buffer_sector(i, j, -1);
		}
	}
	
	for(UINT8 i = 0; i < MAX_OPEN_ZONE; i++)
	{
		enqueue_open_id(i);
	}
	
	/*
	for(UINT32 i = 0; i< NBLK; i++)
	{
		enqueue_FBG(i);
	}
	*/
	//ZNS+
	/*
	for(UINT32 i = 0; i < NZONE; i++)
	{	
		for(UINT32 j = 0; j < DEG_ZONE * NPAGE; j++)
		{
			set_TL_bitmap(i, j, 0);
		}
		for(UINT32 j = 0; j < NSECT; j++)
		{
			set_TL_buffer(i, j,-1);
		}
		set_TL_num(i, -1);
		set_TL_wp(i, 0);
	}
	*/
}
void zns_write(UINT32 const start_lba, UINT32 const num_sectors, UINT32 const write_buffer_addr, UINT32 const swch)
{
    UINT32 i_sect = 0;
    UINT32 next_write_buf_id;
    UINT32 _write_buffer_addr = write_buffer_addr;
    
    while (i_sect < num_sectors)
    {
        /*------------------------------------------*/
        UINT32 c_lba = start_lba + i_sect;
        UINT32 lba = start_lba + i_sect;
        UINT32 c_sect = lba % NSECT;
        lba = lba / NSECT;
        UINT32 b_offset = lba % DEG_ZONE;
        lba = lba / DEG_ZONE;
        UINT32 p_offset = lba % NPAGE;
        lba = lba / NPAGE;
        UINT32 c_fcg = lba % NUM_FCG;
        /*------------------------------------------*/

        UINT32 c_zone = lba;
        if (c_zone >= NZONE) return;
        UINT32 c_bank = c_fcg * DEG_ZONE + b_offset;

        /*------zone_state,wp,slba 읽어오기---------*/
        UINT8 zone_state = get_zone_state(c_zone);
        UINT32 zone_wp = get_zone_wp(c_zone);
        UINT32 zone_slba = get_zone_slba(c_zone);
        /*------------------------------------------*/

        if (zone_state == 0 || zone_state == 1)
        {
            ASSERT(zone_wp > c_lba);
            if (zone_state == 0)
            {
                //Q) max_open_zone reset에서는 안만짐??
                if (OPEN_ZONE == MAX_OPEN_ZONE) return;
                //Q) dequeue에서는 사용할 것이 없ㅇ면 리턴을 어케줌??
                //if(FB[c_fcg][NBLK] == 0) return -1; 

                UINT32 dequeue_fbg = dequeue_FBG();
				UINT32 open_id = dequeue_open_id();
                set_zone_to_FBG(c_zone, dequeue_fbg);
				set_zone_to_ID(c_zone, open_id);
                OPEN_ZONE += 1;
                set_zone_state(c_zone, 1);
            }

            set_zone_wp(c_zone, get_zone_slba(c_zone) + 1);
            /*-------write to buffer----------*/

            // g_ftl_write 저거 옮기기?
            if (c_sect == 0)
            {
                //buf에서 한번 쓰고있으면 SATA_RBUF가 움직이면 안댐
                //생각해보기: SATA가 움직일수 있다?

                if (swch == 0)
                    next_write_buf_id = (write_buffer_addr + 1) % NUM_WR_BUFFERS;
                else
                    _write_buffer_addr = write_buffer_addr + BYTES_PER_PAGE;

                if (swch == 0) {
                    #if OPTION_FTL_TEST == 0
                    while (next_write_buf_id == GETREG(SATA_WBUF_PTR));	// wait if the read buffer is full (slow host)
                    #endif
                }
            }

            UINT32 data;
            if (swch == 0)
                mem_set_dram(data, WR_BUF_PTR(_write_buffer_addr) + c_sect * BYTES_PER_SECTOR
                    , 1 * BYTES_PER_SECTOR);
            else
                mem_set_dram(data, _write_buffer_addr + c_sect * BYTES_PER_SECTOR
                    , 1 * BYTES_PER_SECTOR);
            set_buffer_sector(c_zone, c_sect, data);

            if (c_sect == NSECT - 1)
            {
                /*------------normal_nandwrite-------------*/
                UINT32 vblk = get_zone_to_FBG(c_zone);
                if (swch == 0)
                    nand_page_ptprogram_from_host(c_bank,
                        vblk,
                        p_offset,
                        c_sect,
                        1);
                else
                    nand_page_ptprogram_from_host_zns_write(c_bank,
                        vblk,
                        p_offset,
                        c_sect,
                        _write_buffer_addr,
                        1);
                /*------------------------------*/
            }

            if (get_zone_wp(c_zone) == get_zone_slba(c_zone) + ZONE_SIZE)
            {
                set_zone_state(c_zone, 2);
				UINT8 open_id = get_zone_to_ID(c_zone);
                set_zone_to_ID(c_zone, -1);
				enqueue_open_id(open_id);
                OPEN_ZONE -= 1;
            }

            if (c_sect == 0)
                _write_buffer_addr = next_write_buf_id;
        }

        ASSERT(zone_state != 2);

        /*
         else if (zone_state == 3)
        {
            UINT32 i_tl = p_offset * DEG_ZONE * NSECT + b_offset * NSECT + c_sect;
            //if(TL_BITMAP[c_zone][tl_num] == 1)
            //return -1;
            // 

            UINT32 TL_WP = get_TL_wp(c_zone);
            //if (TL_WP[c_zone] != tl_num)
                //return -1;
            set_TL_wp(c_zone, TL_WP + 1);


            // g_ftl_write 저거 옮기기?
            if (c_sect == 0 || i_sect == 0)
            {
                //buf에서 한번 쓰고있으면 SATA_RBUF가 움직이면 안댐
                //생각해보기: SATA가 움직일수 있다?
                next_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
#if OPTION_FTL_TEST == 0
                while (next_write_buf_id == GETREG(SATA_WBUF_PTR));	// wait if the read buffer is full (slow host)
#endif
            }

            UINT32 data;
            mem_set_dram(data, WR_BUF_PTR(g_ftl_read_buf_id) + c_sect * BYTES_PER_SECTOR
                , 1 * BYTES_PER_SECTOR);
            set_TL_buffer(c_zone, c_sect, data);

            //fill_tl(c_zone, c_lba + 1, tl_num + 1);

            if (TL_WP == DEG_ZONE * NSECT * NPAGE) {
                zns_reset(c_zone);
                set_zone_state(c_zone, 2);
                //ZTF[c_zone] = TL_BITMAP[c_zone][DEG_ZONE * NSECT * NPAGE];
                OPEN_ZONE -= 1;
            }
        }
        */

        i_sect++;
    }
}
void zns_read(UINT32 const start_lba, UINT32 const num_sectors, UINT32 const read_buffer_addr, UINT32 const swch)
{
    UINT32 i_sect = 0;
    UINT32 next_read_buf_id;
    UINT32 _read_buffer_addr = read_buffer_addr;
    while (i_sect < num_sectors)
    {
        /*------------------------------------------*/
        UINT32 c_lba = start_lba + i_sect;
        UINT32 lba = start_lba + i_sect;
        UINT32 c_sect = lba % NSECT;
        lba = lba / NSECT;
        UINT32 b_offset = lba % DEG_ZONE;
        lba = lba / DEG_ZONE;
        UINT32 p_offset = lba % NPAGE;
        lba = lba / NPAGE;
        UINT32 c_fcg = lba % NUM_FCG;
        /*------------------------------------------*/

        UINT32 c_zone = lba;
        if (c_zone >= NZONE) return;
        UINT32 c_bank = c_fcg * DEG_ZONE + b_offset;

        /*------zone_state,wp,slba 읽어오기---------*/
        UINT8 zone_state = get_zone_state(c_zone);
        UINT32 zone_wp = get_zone_wp(c_zone);
        UINT32 zone_slba = get_zone_slba(c_zone);
        /*------------------------------------------*/

        if (zone_state == 0)
        {
            /*---------read_commnad-----------*/
            //첫 sect가 쓰이거나, 한페이지를 다 쓸경우 +1
            //c_sect != 0 , i_sect == 0 일때도 다음 id에 적는다 왜냐면, offset 틀에 때문에????????
            //c_sect == NSECT -1 ->  다음 id를 구하고  -> 현재 id에 값을 쓰고 다음 id로 넘겨준다.
            if (c_sect == NSECT-1)
            {
                //g_ftl_read_buf_id = 이제 쓸 곳
                if (swch == 0)
                    next_read_buf_id = (_read_buffer_addr + 1) % NUM_RD_BUFFERS;
                else
                    next_read_buf_id = _read_buffer_addr + BYTES_PER_PAGE;
                if (swch == 0) {
                    #if OPTION_FTL_TEST == 0
                    while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
                    #endif
                }
            }
            //normal
            if (swch == 0) 
            {
                mem_set_dram(RD_BUF_PTR(_read_buffer_addr) + c_sect * BYTES_PER_SECTOR,
                    0xFFFFFFFF, 1 * BYTES_PER_SECTOR);
                flash_finish();
                SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
                SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit
            }

            //izc,tl
            else 
            {
                mem_set_dram(_read_buffer_addr + c_sect * BYTES_PER_SECTOR,
                    0xFFFFFFFF, 1 * BYTES_PER_SECTOR);
            }
            if (c_sect == NSECT - 1)
                _read_buffer_addr = next_read_buf_id;
            /*----------read--------------*/

            i_sect++;
            continue;
        }
        else if (zone_state == 1 || zone_state == 2)
        {
            if (zone_wp <= c_lba)
            {
                //data[i_sect] = -1;

                /*---------read_commnad-----------*/
                if (c_sect == NSECT - 1)
                {
                    if (swch == 0)
                        next_read_buf_id = (_read_buffer_addr + 1) % NUM_RD_BUFFERS;
                    else
                        next_read_buf_id += BYTES_PER_PAGE;

                    if (swch == 0) 
                    {
                    #if OPTION_FTL_TEST == 0
                        while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
                    #endif
                    }
                }

                if (swch == 0) {
                    mem_set_dram(RD_BUF_PTR(_read_buffer_addr) + c_sect * BYTES_PER_SECTOR,
                        0xFFFFFFFF, 1 * BYTES_PER_SECTOR);
                    flash_finish();
                    SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
                    SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit
                }
                else {
                    mem_set_dram(_read_buffer_addr + c_sect * BYTES_PER_SECTOR,
                        0xFFFFFFFF, 1 * BYTES_PER_SECTOR);
                }

                if (c_sect == NSECT - 1)
                    _read_buffer_addr = next_read_buf_id;
                /*----------read--------------*/


                i_sect++;
                continue;
            }
            //데이터가 버퍼에 있을때
            if (((zone_wp - 1) / NSECT) * NSECT <= c_lba && ((zone_wp - 1) % NSECT) != NSECT - 1)
            {

                UINT32 data = get_buffer_sector(c_zone, c_sect);

                /*---------read_commnad-----------*/
                if (c_sect == NSECT - 1)
                {

                    if (swch == 0)
                        next_read_buf_id = (_read_buffer_addr + 1) % NUM_RD_BUFFERS;
                    else
                        next_read_buf_id += BYTES_PER_PAGE;

                    if (swch == 0)
                    {
                        #if OPTION_FTL_TEST == 0
                        while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
                        #endif
                    }
                }

                if (swch == 0) {
                    mem_set_dram(RD_BUF_PTR(_read_buffer_addr) + c_sect * BYTES_PER_SECTOR,
                        data, 1 * BYTES_PER_SECTOR);
                    flash_finish();
                    SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
                    SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit
                }
                else {
                    mem_set_dram(_read_buffer_addr + c_sect * BYTES_PER_SECTOR,
                        data, 1 * BYTES_PER_SECTOR);
                }

                if (c_sect == NSECT - 1)
                    _read_buffer_addr = next_read_buf_id;
                /*----------read--------------*/

            }
            //데이터가 버퍼에 없을때 즉, nand에서 읽어와야댐 문제점: nand_page_pthread는 sect단위가아니고 page단위로 하는 듯.
            else
            {
                /*------------normal_nandread-------------*/

                UINT32 vblk = get_zone_to_FBG(c_zone);

                if (swch == 0)
                    nand_page_ptread_to_host(c_bank,
                        vblk,
                        p_offset,
                        (c_sect),
                        1);

                else
                    nand_page_ptread_to_host_zns_read(c_bank,
                        vblk,
                        p_offset,
                        (c_sect),
                        _read_buffer_addr,
                        1);
                /*------------------------------*/
            }
        }

        /*
        else if (zone_state == 3)
        {
            UINT32 i_tl = c_lba - c_zone * DEG_ZONE * NSECT * NPAGE;
            UINT32 TL_WP = get_TL_wp(c_zone);
            if (TL_WP > i_tl)
            { // this data in tl zone
                if (((TL_WP - 1) / NSECT) * NSECT <= i_tl && ((TL_WP - 1) % NSECT) != NSECT - 1)
                {
                    UINT32 data = get_TL_buffer(c_zone, c_sect);

                    if (c_sect == 0)
                    {
                        next_read_buf_id = (read_buffer_addr + 1) % NUM_RD_BUFFERS;
                        #if OPTION_FTL_TEST == 0
                        while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
                        #endif
                    }
                    mem_set_dram(RD_BUF_PTR(read_buffer_addr) + c_sect * BYTES_PER_SECTOR,
                        0xFFFFFFFF, 1 * BYTES_PER_SECTOR);

                    if (swch == 0) {
                        flash_finish();
                        SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
                        SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit
                    }

                    if (c_sect == 0)
                        read_buffer_addr = next_read_buf_id;

                }
                else
                {
                    cnt_for_nandread++;
                    if (c_sect == 0 && i_sect != 0)
                    {
                        UINT32 vblk = get_TL_num(c_zone);
                        nand_page_ptread_to_host(c_bank,
                            vblk,
                            p_offset,
                            (c_sect - cnt_for_nandread),
                            cnt_for_nandread);

                        cnt_for_nandread = 0;
                    }
                }
            }
            else
            {
                cnt_for_nandread++;
                if (c_sect == 0 && i_sect != 0)
                {
                    UINT32 vblk = get_zone_to_FBG(c_zone);
                    nand_page_ptread_to_host(c_bank,
                        vblk,
                        p_offset,
                        (c_sect - cnt_for_nandread),
                        cnt_for_nandread);

                    cnt_for_nandread = 0;
                }
            }
        }
        */
        

        i_sect++;
    }
    return;
}

void nand_page_ptread_to_host_zns_read(UINT32 const bank, UINT32 const vblock, UINT32 const page_num, UINT32 const sect_offset, UINT32 const num_sectors, UINT32 const read_buffer_addr)
{
    UINT32 row;

    ASSERT(bank < NUM_BANKS);
    ASSERT(vblock < VBLKS_PER_BANK);
    ASSERT(page_num < PAGES_PER_BLK);

    row = (vblock * PAGES_PER_BLK) + page_num;

    SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
    SETREG(FCP_DMA_ADDR, read_buffer_addr);
    SETREG(FCP_DMA_CNT, num_sectors * BYTES_PER_SECTOR);

    SETREG(FCP_COL, sect_offset);
#if OPTION_FTL_TEST == TRUE
    SETREG(FCP_OPTION, FO_P | FO_E);
#else
    SETREG(FCP_OPTION, FO_P | FO_E | FO_B_SATA_R);
#endif
    SETREG(FCP_ROW_L(bank), row);
    SETREG(FCP_ROW_H(bank), row);

    g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

#if OPTION_FTL_TEST == FALSE
    {
        while (1)
        {
            UINT32 sata_id = GETREG(SATA_RBUF_PTR);

            if (g_ftl_read_buf_id != sata_id)
                break;
        }
    }
#endif
    flash_issue_cmd(bank, RETURN_ON_ISSUE);
}

void nand_page_ptprogram_from_host_zns_write(UINT32 const bank, UINT32 const vblock, UINT32 const page_num, UINT32 const sect_offset, UINT32 const num_sectors, UINT32 const write_buffer_addr)
{
    UINT32 row;

    ASSERT(bank < NUM_BANKS);
    ASSERT(vblock < VBLKS_PER_BANK);
    ASSERT(page_num < PAGES_PER_BLK);

    row = (vblock * PAGES_PER_BLK) + page_num;

    SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
    //이건아닌듯
#if OPTION_FTL_TEST == TRUE
    SETREG(FCP_OPTION, FO_P | FO_E | FO_B_W_DRDY);
#else
    SETREG(FCP_OPTION, FO_P | FO_E | FO_B_SATA_W);
#endif
    SETREG(FCP_DMA_ADDR, (write_buffer_addr));
    SETREG(FCP_DMA_CNT, num_sectors * BYTES_PER_SECTOR);

    SETREG(FCP_COL, sect_offset);
    SETREG(FCP_ROW_L(bank), row);
    SETREG(FCP_ROW_H(bank), row);

    flash_issue_cmd(bank, RETURN_ON_ISSUE);

    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
}

void ftl_read(UINT32 const lba, UINT32 const num_sectors)
{
    UINT32 remain_sects, num_sectors_to_read;
    UINT32 lpn, sect_offset;
    UINT32 bank, vpn;

    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;
    remain_sects = num_sectors;
    bank = get_num_bank(lpn);

	if(lba == 7 && num_sectors == 7){
		UINT32 next_read_buf_id = (g_ftl_read_buf_id+1) % NUM_RD_BUFFERS;
		while(next_read_buf_id == GETREG(SATA_RBUF_PTR));
		
		flash_finish();
		
		SETREG(BM_STACK_RDSET, next_read_buf_id);
		SETREG(BM_STACK_RESET, 0x02);
		g_ftl_read_buf_id = next_read_buf_id;
		
        //print stats
        uart_printf("gc_cnt : %d\n", g_ftl_statistics[bank].gc_cnt);
        uart_printf("gc_write : %d\n", g_ftl_statistics[bank].gc_write);
        uart_printf("host_write : %d\n", g_ftl_statistics[bank].host_write);
        uart_printf("nand_write : %d\n", g_ftl_statistics[bank].nand_write);
        uart_printf("page_wcount : %d\n", g_ftl_statistics[bank].page_wcount);



		return;
	}

    //seq_zone
    if (lba >= 6*ZONE_SIZE) 
    {
        zns_read(lba, num_sectors,g_ftl_read_buf_id, 0);
    }
    //random_zone
    else 
    {
        while (remain_sects != 0)
        {
            if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
            {
                num_sectors_to_read = remain_sects;
            }
            else
            {
                num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;
            }
            bank = get_num_bank(lpn); // page striping
            vpn  = get_vpn(lpn);
            CHECK_VPAGE(vpn);

            if (vpn != NULL)
            {
                nand_page_ptread_to_host(bank,
                                         vpn / PAGES_PER_BLK,
                                         vpn % PAGES_PER_BLK,
                                         sect_offset,
                                         num_sectors_to_read);
            }
            // The host is requesting to read a logical page that has never been written to.
            else
            {
			    UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

			    #if OPTION_FTL_TEST == 0
			    while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
			    #endif

                // fix bug @ v.1.0.6
                // Send 0xFF...FF to host when the host request to read the sector that has never been written.
                // In old version, for example, if the host request to read unwritten sector 0 after programming in sector 1, Jasmine would send 0x00...00 to host.
                // However, if the host already wrote to sector 1, Jasmine would send 0xFF...FF to host when host request to read sector 0. (ftl_read() in ftl_xxx/ftl.c)
			    mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) + sect_offset*BYTES_PER_SECTOR,
                             0xFFFFFFFF, num_sectors_to_read*BYTES_PER_SECTOR);

                flash_finish();

			    SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
			    SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit

			    g_ftl_read_buf_id = next_read_buf_id;
            }
            sect_offset   = 0;
            remain_sects -= num_sectors_to_read;
            lpn++;
        }
    }
}


void ftl_write(UINT32 const lba, UINT32 const num_sectors)
{
    UINT32 remain_sects, num_sectors_to_write;
    UINT32 lpn, sect_offset;

    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;
    remain_sects = num_sectors;

	g_ftl_statistics[get_num_bank(lpn)].host_write++;

    if (lba == 7 && num_sectors == 11) // zone reset
    {
        while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR
        UINT32 zone = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR);
        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer
        flash_finish();
        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
        SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit

        //call zns_reset
        return;
    }
    if (lba == 7 && num_sectors == 13) {
        while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR
        UINT32 zone_number = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR);
        UINT32 zone_cnt = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR + sizeof(int));
        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer
        flash_finish();
        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
        SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit

        zns_get_desc(zone_number, zone_cnt);
        return;
    }
    if (lba == 1 && num_sectors == 31) {
        //make DRAM addr for IZC (32KB DRAM)
        while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR
        UINT32 src_zone = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR);
        UINT32 dst_zone = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR + sizeof(int));
        UINT32 copy_len = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR + 2 * sizeof(int));
        uart_printf("INTERNEL ZONE COMPACTION src zone is %d dst zone is %d copy len is %d\r\n", src_zone, dst_zone, copy_len);
        //mem_copy(IZC_ADDR,(WR_BUF_PTR(g_ftl_write_buf_id)+lba*BYTES_PER_SECTOR + 3*sizeof(int)), copy_len * sizeof(int));
        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer
        flash_finish();
        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
        SETREG(BM_STACK_RESET, 0x01);
        return;
    }
    if (lba == 3 && num_sectors == 29) {
        //make DRAM addr for TL open (32KB DRAM)
        while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR
        UINT32 src_zone = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) + lba * BYTES_PER_SECTOR);
        //mem_copy(TL_ADDR,(WR_BUF_PTR(g_ftl_write_buf_id)+lba*BYTES_PER_SECTOR + 1*sizeof(int)), SECTORS_PER_PAGE*PAGES_PER_BLK*NUM_BANKS);
        //TL_OPEN(src_zone, TL_ADDR);
        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer
        flash_finish();
        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
        SETREG(BM_STACK_RESET, 0x01);
        return;
    }
    if (lba >= 6 * ZONE_SIZE) {
        zns_write(lba, num_sectors, g_ftl_write_buf_id, 0);
    }
    else {
        while (remain_sects != 0)
        {
            if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
            {
                num_sectors_to_write = remain_sects;
            }
            else
            {
                num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
            }
            // single page write individually
            write_page(lpn, sect_offset, num_sectors_to_write);

            sect_offset = 0;
            remain_sects -= num_sectors_to_write;
            lpn++;
        }
    }
}
static void write_page(UINT32 const lpn, UINT32 const sect_offset, UINT32 const num_sectors)
{
    CHECK_LPAGE(lpn);
    ASSERT(sect_offset < SECTORS_PER_PAGE);
    ASSERT(num_sectors > 0 && num_sectors <= SECTORS_PER_PAGE);

    UINT32 bank, old_vpn, new_vpn;
    UINT32 vblock, page_num, page_offset, column_cnt;

    bank        = get_num_bank(lpn); // page striping
    page_offset = sect_offset;
    column_cnt  = num_sectors;

    new_vpn  = assign_new_write_vpn(bank);
    old_vpn  = get_vpn(lpn);

    CHECK_VPAGE (old_vpn);
    CHECK_VPAGE (new_vpn);
    ASSERT(old_vpn != new_vpn);

    g_ftl_statistics[bank].page_wcount++;

    // if old data already exist,
    if (old_vpn != NULL)
    {
        vblock   = old_vpn / PAGES_PER_BLK;
        page_num = old_vpn % PAGES_PER_BLK;

        //--------------------------------------------------------------------------------------
        // `Partial programming'
        // we could not determine whether the new data is loaded in the SATA write buffer.
        // Thus, read the left/right hole sectors of a valid page and copy into the write buffer.
        // And then, program whole valid data
        //--------------------------------------------------------------------------------------
        if (num_sectors != SECTORS_PER_PAGE)
        {
            // Performance optimization (but, not proved)
            // To reduce flash memory access, valid hole copy into SATA write buffer after reading whole page
            // Thus, in this case, we need just one full page read + one or two mem_copy
            if ((num_sectors <= 8) && (page_offset != 0))
            {
                // one page async read
                nand_page_read(bank,
                               vblock,
                               page_num,
                               FTL_BUF(bank));
                // copy `left hole sectors' into SATA write buffer
                if (page_offset != 0)
                {
                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id),
                             FTL_BUF(bank),
                             page_offset * BYTES_PER_SECTOR);
                }
                // copy `right hole sectors' into SATA write buffer
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {
                    UINT32 const rhole_base = (page_offset + column_cnt) * BYTES_PER_SECTOR;

                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id) + rhole_base,
                             FTL_BUF(bank) + rhole_base,
                             BYTES_PER_PAGE - rhole_base);
                }
            }
            // left/right hole async read operation (two partial page read)
            else
            {
                // read `left hole sectors'
                if (page_offset != 0)
                {
                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     0,
                                     page_offset,
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
                // read `right hole sectors'
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {
                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     page_offset + column_cnt,
                                     SECTORS_PER_PAGE - (page_offset + column_cnt),
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
            }
        }
        // full page write
        page_offset = 0;
        column_cnt  = SECTORS_PER_PAGE;
        // invalid old page (decrease vcount)
        set_vcount(bank, vblock, get_vcount(bank, vblock) - 1);
    }
    vblock   = new_vpn / PAGES_PER_BLK;
    page_num = new_vpn % PAGES_PER_BLK;
    ASSERT(get_vcount(bank,vblock) < (PAGES_PER_BLK - 1));

    // write new data (make sure that the new data is ready in the write buffer frame)
    // (c.f FO_B_SATA_W flag in flash.h)
    nand_page_ptprogram_from_host(bank,
                                  vblock,
                                  page_num,
                                  page_offset,
                                  column_cnt);
	g_ftl_statistics[bank].nand_write++;
    // update metadata
    set_lpn(bank, page_num, lpn);
    set_vpn(lpn, new_vpn);
    set_vcount(bank, vblock, get_vcount(bank, vblock) + 1);
}

void zns_reset(UINT32 c_zone)
{
	UINT32 lba = c_zone / ZONE_SIZE;
	UINT32 c_bank = (lba / NSECT) % DEG_ZONE;
	
	ASSERT(c_zone < NZONE);
	ASSERT(get_zone_state(c_zone) == 2)
	
	set_zone_state(c_zone, 0);
	set_zone_wp(c_zone, get_zone_slba(c_zone));
	nand_block_erase(c_bank, get_zone_to_FBG(c_zone));
	
	enqueue_FBG(get_zone_to_FBG(c_zone));
	set_zone_to_FBG(c_zone, -1);

}

void zns_get_desc(UINT32 c_zone, UINT32 nzone)
{
	for(UINT32 i = 0; i < nzone; i++) 
	{
		ASSERT(i + c_zone < NZONE);

		uart_printf("ZONE %d descriptor", c_zone + i);
        /*
        if(get_zone_state(i + c_zone) == 3)
        {
            uart_printf("State : TL_OPEN");
            uart_printf("Slba : %d", get_zone_slba(i + c_zone));
            uart_printf("Wp : %d", get_TL_wp(i + c_zone) + get_zone_slba(i + c_zone));

            //descs[i].state = 3;
            //descs[i].slba = get_zone_slba(i + c_zone);
            //descs[i].wp = get_TL_wp(i + c_zone) + get_zone_slba(i + c_zone);
            continue;
        }
        */

		//descs[i].state = get_zone_state(i + c_zone);
		//descs[i].slba = get_zone_slba(i + c_zone);
		//descs[i].wp = get_zone_wp(i + c_zone);
        if(get_zone_state(i + c_zone) == 0) uart_printf("State : EMPTY");
		else if(get_zone_state(i + c_zone) == 1) uart_printf("State : OPEN");
		else if(get_zone_state(i + c_zone) == 2) uart_printf("State : FULL");
		
		uart_printf("Slba : %d", get_zone_slba(i + c_zone));
		uart_printf("Wp : %d", get_zone_slba(i + c_zone));
		
		
        //todo : uart_print desc
	}
  return;
}
/*
void zns_izc(UINT32 src_zone, UINT32 dest_zone, UINT32 copy_len, UINT32 *copy_list)
{
	ASSERT(src_zone != dest_zone);
	ASSERT(src_zone < NZONE && dest_zone < NZONE);
	ASSERT(get_zone_state(src_zone) == 2 && get_zone_state(dest_zone) == 0);
	
	UINT32 data[NSECT];
	for(UINT32 i = 0; i < copy_len; i++)
	{
		UINT32 s_lba = get_zone_slba(src_zone) + copy_list[i] * NSECT;
		zns_read(s_lba, NSECT, TL_INTERNAL_BUFFER_ADDR);
		UINT32 d_lba = get_zone_slba(dest_zone) + i * NSECT;
		zns_write(d_lba, NSECT, TL_INTERNAL_BUFFER_ADDR, 1);
	}
	zns_reset(get_zone_slba(src_zone));
	OPEN_ZONE++;
}
*/


// get vpn from PAGE_MAP
static UINT32 get_vpn(UINT32 const lpn)
{
    CHECK_LPAGE(lpn);
    return read_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32));
}
// set vpn to PAGE_MAP
static void set_vpn(UINT32 const lpn, UINT32 const vpn)
{
    CHECK_LPAGE(lpn);
    ASSERT(vpn >= (META_BLKS_PER_BANK * PAGES_PER_BLK) && vpn < (rand_write_blks * PAGES_PER_BLK));

    write_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32), vpn);
}
// get valid page count of vblock
static UINT32 get_vcount(UINT32 const bank, UINT32 const vblock)
{
    UINT32 vcount;

    ASSERT(bank < NUM_BANKS);
    ASSERT((vblock >= META_BLKS_PER_BANK) && (vblock < rand_write_blks));

    vcount = read_dram_16(VCOUNT_ADDR + (((bank * rand_write_blks) + vblock) * sizeof(UINT16)));
    /*
    if (!(vcount < PAGES_PER_BLK) || (vcount == VC_MAX)) {
        uart_printf("vcount : %d", vcount);
        while (1);
    }*/
    ASSERT((vcount < PAGES_PER_BLK) || (vcount == VC_MAX));
   
    

    return vcount;
}
// set valid page count of vblock
static void set_vcount(UINT32 const bank, UINT32 const vblock, UINT32 const vcount)
{
    ASSERT(bank < NUM_BANKS);
    ASSERT((vblock >= META_BLKS_PER_BANK) && (vblock < VBLKS_PER_BANK));
    ASSERT((vcount < PAGES_PER_BLK) || (vcount == VC_MAX));

    write_dram_16(VCOUNT_ADDR + (((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16)), vcount);
}
static UINT32 assign_new_write_vpn(UINT32 const bank)
{
    ASSERT(bank < NUM_BANKS);

    UINT32 write_vpn;
    UINT32 vblock;

    write_vpn = get_cur_write_vpn(bank);
    vblock    = write_vpn / PAGES_PER_BLK;

    // NOTE: if next new write page's offset is
    // the last page offset of vblock (i.e. PAGES_PER_BLK - 1),
    if ((write_vpn % PAGES_PER_BLK) == (PAGES_PER_BLK - 2))
    {
        // then, because of the flash controller limitation
        // (prohibit accessing a spare area (i.e. OOB)),
        // thus, we persistenly write a lpn list into last page of vblock.
        mem_copy(FTL_BUF(bank), g_misc_meta[bank].lpn_list_of_cur_vblock, sizeof(UINT32) * PAGES_PER_BLK);
        // fix minor bug
        nand_page_ptprogram(bank, vblock, PAGES_PER_BLK - 1, 0,
                            ((sizeof(UINT32) * PAGES_PER_BLK + BYTES_PER_SECTOR - 1 ) / BYTES_PER_SECTOR), FTL_BUF(bank));

        mem_set_sram(g_misc_meta[bank].lpn_list_of_cur_vblock, 0x00000000, sizeof(UINT32) * PAGES_PER_BLK);

        inc_full_blk_cnt(bank);

        // do garbage collection if necessary
        if (is_full_all_blks(bank))
        {
            garbage_collection(bank);
            return get_cur_write_vpn(bank);
        }
        do
        {
            vblock++;

            ASSERT(vblock != VBLKS_PER_BANK);
        }while (get_vcount(bank, vblock) == VC_MAX);
    }
    // write page -> next block
    if (vblock != (write_vpn / PAGES_PER_BLK))
    {
        write_vpn = vblock * PAGES_PER_BLK;
    }
    else
    {
        write_vpn++;
    }
    set_new_write_vpn(bank, write_vpn);

    return write_vpn;
}
static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblk_offset)
{
    if (tst_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset) == FALSE)
    {
        return FALSE;
    }
    return TRUE;
}
//------------------------------------------------------------
// if all blocks except one free block are full,
// do garbage collection for making at least one free page
//-------------------------------------------------------------
static void garbage_collection(UINT32 const bank)
{
    ASSERT(bank < NUM_BANKS);
    g_ftl_statistics[bank].gc_cnt++;

    UINT32 src_lpn;
    UINT32 vt_vblock;
    UINT32 free_vpn;
    UINT32 vcount; // valid page count in victim block
    UINT32 src_page;
    UINT32 gc_vblock;

    g_ftl_statistics[bank].gc_cnt++;

    vt_vblock = get_vt_vblock(bank);   // get victim block
    vcount    = get_vcount(bank, vt_vblock);
    gc_vblock = get_gc_vblock(bank);
    free_vpn  = gc_vblock * PAGES_PER_BLK;

    uart_printf("garbage_collection bank %d, vblock %d",bank, vt_vblock); 

    ASSERT(vt_vblock != gc_vblock);
    ASSERT(vt_vblock >= META_BLKS_PER_BANK && vt_vblock < VBLKS_PER_BANK);
    ASSERT(vcount < (PAGES_PER_BLK - 1));
    ASSERT(get_vcount(bank, gc_vblock) == VC_MAX);
    ASSERT(!is_bad_block(bank, gc_vblock));

    // 1. load p2l list from last page offset of victim block (4B x PAGES_PER_BLK)
    // fix minor bug
    nand_page_ptread(bank, vt_vblock, PAGES_PER_BLK - 1, 0,
                     ((sizeof(UINT32) * PAGES_PER_BLK + BYTES_PER_SECTOR - 1 ) / BYTES_PER_SECTOR), FTL_BUF(bank), RETURN_WHEN_DONE);
    mem_copy(g_misc_meta[bank].lpn_list_of_cur_vblock, FTL_BUF(bank), sizeof(UINT32) * PAGES_PER_BLK);
    // 2. copy-back all valid pages to free space
    for (src_page = 0; src_page < (PAGES_PER_BLK - 1); src_page++)
    {
        // get lpn of victim block from a read lpn list
        src_lpn = get_lpn(bank, src_page);
        CHECK_VPAGE(get_vpn(src_lpn));

        // determine whether the page is valid or not
        if (get_vpn(src_lpn) !=
            ((vt_vblock * PAGES_PER_BLK) + src_page))
        {
            // invalid page
            continue;
        }
        ASSERT(get_lpn(bank, src_page) != INVALID);
        CHECK_LPAGE(src_lpn);
        // if the page is valid,
        // then do copy-back op. to free space
        nand_page_copyback(bank,
                           vt_vblock,
                           src_page,
                           free_vpn / PAGES_PER_BLK,
                           free_vpn % PAGES_PER_BLK);
		g_ftl_statistics[bank].gc_write++;
        ASSERT((free_vpn / PAGES_PER_BLK) == gc_vblock);
        // update metadata
        set_vpn(src_lpn, free_vpn);
        set_lpn(bank, (free_vpn % PAGES_PER_BLK), src_lpn);

        free_vpn++;
    }
#if OPTION_ENABLE_ASSERT
    if (vcount == 0)
    {
        ASSERT(free_vpn == (gc_vblock * PAGES_PER_BLK));
    }
#endif
    // 3. erase victim block
    nand_block_erase(bank, vt_vblock);
    ASSERT((free_vpn % PAGES_PER_BLK) < (PAGES_PER_BLK - 2));
    ASSERT((free_vpn % PAGES_PER_BLK == vcount));

    uart_printf("gc page count : %d", vcount); 

    // 4. update metadata
    set_vcount(bank, vt_vblock, VC_MAX);
    set_vcount(bank, gc_vblock, vcount);
    set_new_write_vpn(bank, free_vpn); // set a free page for new write
    set_gc_vblock(bank, vt_vblock); // next free block (reserve for GC)
    dec_full_blk_cnt(bank); // decrease full block count
    uart_print("garbage_collection end");
}
//-------------------------------------------------------------
// Victim selection policy: Greedy
//
// Select the block which contain minumum valid pages
//-------------------------------------------------------------
static UINT32 get_vt_vblock(UINT32 const bank)
{
    ASSERT(bank < NUM_BANKS);

    UINT32 vblock;
    // search the block which has mininum valid pages
	vblock = mem_search_min_max(VCOUNT_ADDR + (bank * VBLKS_PER_BANK * sizeof(UINT16)),
                                sizeof(UINT16),
                                rand_write_blks,
                                MU_CMD_SEARCH_MIN_DRAM);


    ASSERT(is_bad_block(bank, vblock) == FALSE);
    ASSERT(vblock >= META_BLKS_PER_BANK && vblock < VBLKS_PER_BANK);
    ASSERT(get_vcount(bank, vblock) < (PAGES_PER_BLK - 1));

    return vblock;
}
static void format(void)
{
    UINT32 bank, vblock, vcount_val;

    ASSERT(NUM_MISC_META_SECT > 0);
    ASSERT(NUM_VCOUNT_SECT > 0);

    uart_printf("Total FTL DRAM metadata size: %d KB", DRAM_BYTES_OTHER / 1024);

    uart_printf("VBLKS_PER_BANK: %d", VBLKS_PER_BANK);
    uart_printf("LBLKS_PER_BANK: %d", NUM_LPAGES / PAGES_PER_BLK / NUM_BANKS);
    uart_printf("META_BLKS_PER_BANK: %d", META_BLKS_PER_BANK);

    //----------------------------------------
    // initialize DRAM metadata
    //----------------------------------------
    mem_set_dram(PAGE_MAP_ADDR, NULL, PAGE_MAP_BYTES);
    mem_set_dram(VCOUNT_ADDR, NULL, VCOUNT_BYTES);

    //----------------------------------------
    // erase all blocks except vblock #0
    //----------------------------------------
	for (vblock = MISCBLK_VBN; vblock < VBLKS_PER_BANK; vblock++)
	{
		for (bank = 0; bank < NUM_BANKS; bank++)
		{
            vcount_val = VC_MAX;
            if (is_bad_block(bank, vblock) == FALSE)
			{
				nand_block_erase(bank, vblock);
                vcount_val = 0;
            }
            write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16),
                          vcount_val);
        }
    }
    //----------------------------------------
    // initialize SRAM metadata
    //----------------------------------------
    init_metadata_sram();

    // flush metadata to NAND
    logging_pmap_table();
    logging_misc_metadata();

    write_format_mark();
	led(1);
    uart_print("format complete");
}
static void init_metadata_sram(void)
{
    UINT32 bank;
    UINT32 vblock;
    UINT32 mapblk_lbn;

    //----------------------------------------
    // initialize misc. metadata
    //----------------------------------------
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        g_misc_meta[bank].free_blk_cnt = VBLKS_PER_BANK - META_BLKS_PER_BANK;
        g_misc_meta[bank].free_blk_cnt -= get_bad_blk_cnt(bank);
        // NOTE: vblock #0,1 don't use for user space
        write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + 0) * sizeof(UINT16), VC_MAX);
        write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + 1) * sizeof(UINT16), VC_MAX);

        //----------------------------------------
        // assign misc. block
        //----------------------------------------
        // assumption: vblock #1 = fixed location.
        // Thus if vblock #1 is a bad block, it should be allocate another block.
        set_miscblk_vpn(bank, MISCBLK_VBN * PAGES_PER_BLK - 1);
        ASSERT(is_bad_block(bank, MISCBLK_VBN) == FALSE);

        vblock = MISCBLK_VBN;

        //----------------------------------------
        // assign map block
        //----------------------------------------
        mapblk_lbn = 0;
        while (mapblk_lbn < MAPBLKS_PER_BANK)
        {
            vblock++;
            ASSERT(vblock < VBLKS_PER_BANK);
            if (is_bad_block(bank, vblock) == FALSE)
            {
                set_mapblk_vpn(bank, mapblk_lbn, vblock * PAGES_PER_BLK);
                write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
                mapblk_lbn++;
            }
        }
        //----------------------------------------
        // assign free block for gc
        //----------------------------------------
        do
        {
            vblock++;
            // NOTE: free block should not be secleted as a victim @ first GC
            write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
            // set free block
            set_gc_vblock(bank, vblock);

            ASSERT(vblock < VBLKS_PER_BANK);
        }while(is_bad_block(bank, vblock) == TRUE);
        //----------------------------------------
        // assign free vpn for first new write
        //----------------------------------------
        do
        {
            vblock++;
            // 현재 next vblock부터 새로운 데이터를 저장을 시작
            set_new_write_vpn(bank, vblock * PAGES_PER_BLK);
            ASSERT(vblock < VBLKS_PER_BANK);
        }while(is_bad_block(bank, vblock) == TRUE);
    }
}
// logging misc + vcount metadata
static void logging_misc_metadata(void)
{
    UINT32 misc_meta_bytes = NUM_MISC_META_SECT * BYTES_PER_SECTOR; // per bank
    UINT32 vcount_addr     = VCOUNT_ADDR;
    UINT32 vcount_bytes    = NUM_VCOUNT_SECT * BYTES_PER_SECTOR; // per bank
    UINT32 vcount_boundary = VCOUNT_ADDR + VCOUNT_BYTES; // entire vcount data
    UINT32 bank;

    flash_finish();

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        inc_miscblk_vpn(bank);

        // note: if misc. meta block is full, just erase old block & write offset #0
        if ((get_miscblk_vpn(bank) / PAGES_PER_BLK) != MISCBLK_VBN)
        {
            nand_block_erase(bank, MISCBLK_VBN);
            set_miscblk_vpn(bank, MISCBLK_VBN * PAGES_PER_BLK); // vpn = 128
        }
        // copy misc. metadata to FTL buffer
        mem_copy(FTL_BUF(bank), &g_misc_meta[bank], misc_meta_bytes);

        // copy vcount metadata to FTL buffer
        if (vcount_addr <= vcount_boundary)
        {
            mem_copy(FTL_BUF(bank) + misc_meta_bytes, vcount_addr, vcount_bytes);
            vcount_addr += vcount_bytes;
        }
    }
    // logging the misc. metadata to nand flash
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        nand_page_ptprogram(bank,
                            get_miscblk_vpn(bank) / PAGES_PER_BLK,
                            get_miscblk_vpn(bank) % PAGES_PER_BLK,
                            0,
                            NUM_MISC_META_SECT + NUM_VCOUNT_SECT,
                            FTL_BUF(bank));
    }
    flash_finish();
}
static void logging_pmap_table(void)
{
    UINT32 pmap_addr  = PAGE_MAP_ADDR;
    UINT32 pmap_bytes = BYTES_PER_PAGE; // per bank
    UINT32 mapblk_vpn;
    UINT32 bank;
    UINT32 pmap_boundary = PAGE_MAP_ADDR + PAGE_MAP_BYTES;
    BOOL32 finished = FALSE;

    for (UINT32 mapblk_lbn = 0; mapblk_lbn < MAPBLKS_PER_BANK; mapblk_lbn++)
    {
        flash_finish();

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (finished)
            {
                break;
            }
            else if (pmap_addr >= pmap_boundary)
            {
                finished = TRUE;
                break;
            }
            else if (pmap_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                finished = TRUE;
                pmap_bytes = (pmap_boundary - pmap_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR ;
            }
            inc_mapblk_vpn(bank, mapblk_lbn);

            mapblk_vpn = get_mapblk_vpn(bank, mapblk_lbn);

            // note: if there is no free page, then erase old map block first.
            if ((mapblk_vpn % PAGES_PER_BLK) == 0)
            {
                // erase full map block
                nand_block_erase(bank, (mapblk_vpn - 1) / PAGES_PER_BLK);

                // next vpn of mapblk is offset #0
                set_mapblk_vpn(bank, mapblk_lbn, ((mapblk_vpn - 1) / PAGES_PER_BLK) * PAGES_PER_BLK);
                mapblk_vpn = get_mapblk_vpn(bank, mapblk_lbn);
            }
            // copy the page mapping table to FTL buffer
            mem_copy(FTL_BUF(bank), pmap_addr, pmap_bytes);

            // logging update page mapping table into map_block
            nand_page_ptprogram(bank,
                                mapblk_vpn / PAGES_PER_BLK,
                                mapblk_vpn % PAGES_PER_BLK,
                                0,
                                pmap_bytes / BYTES_PER_SECTOR,
                                FTL_BUF(bank));
            pmap_addr += pmap_bytes;
        }
        if (finished)
        {
            break;
        }
    }
    flash_finish();
}
// load flushed FTL metadta
static void load_metadata(void)
{
    load_misc_metadata();
    load_pmap_table();
}
// misc + VCOUNT
static void load_misc_metadata(void)
{
    UINT32 misc_meta_bytes = NUM_MISC_META_SECT * BYTES_PER_SECTOR;
    UINT32 vcount_bytes    = NUM_VCOUNT_SECT * BYTES_PER_SECTOR;
    UINT32 vcount_addr     = VCOUNT_ADDR;
    UINT32 vcount_boundary = VCOUNT_ADDR + VCOUNT_BYTES;

    UINT32 load_flag = 0;
    UINT32 bank, page_num;
    UINT32 load_cnt = 0;

    flash_finish();

	disable_irq();
	flash_clear_irq();	// clear any flash interrupt flags that might have been set

    // scan valid metadata in descending order from last page offset
    for (page_num = PAGES_PER_BLK - 1; page_num != ((UINT32) -1); page_num--)
    {
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (load_flag & (0x1 << bank))
            {
                continue;
            }
            // read valid metadata from misc. metadata area
            nand_page_ptread(bank,
                             MISCBLK_VBN,
                             page_num,
                             0,
                             NUM_MISC_META_SECT + NUM_VCOUNT_SECT,
                             FTL_BUF(bank),
                             RETURN_ON_ISSUE);
        }
        flash_finish();

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (!(load_flag & (0x1 << bank)) && !(BSP_INTR(bank) & FIRQ_ALL_FF))
            {
                load_flag = load_flag | (0x1 << bank);
                load_cnt++;
            }
            CLR_BSP_INTR(bank, 0xFF);
        }
    }
    ASSERT(load_cnt == NUM_BANKS);

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        // misc. metadata
        mem_copy(&g_misc_meta[bank], FTL_BUF(bank), sizeof(misc_metadata));

        // vcount metadata
        if (vcount_addr <= vcount_boundary)
        {
            mem_copy(vcount_addr, FTL_BUF(bank) + misc_meta_bytes, vcount_bytes);
            vcount_addr += vcount_bytes;

        }
    }
	enable_irq();
}
static void load_pmap_table(void)
{
    UINT32 pmap_addr = PAGE_MAP_ADDR;
    UINT32 temp_page_addr;
    UINT32 pmap_bytes = BYTES_PER_PAGE; // per bank
    UINT32 pmap_boundary = PAGE_MAP_ADDR + (NUM_LPAGES * sizeof(UINT32));
    UINT32 mapblk_lbn, bank;
    BOOL32 finished = FALSE;

    flash_finish();

    for (mapblk_lbn = 0; mapblk_lbn < MAPBLKS_PER_BANK; mapblk_lbn++)
    {
        temp_page_addr = pmap_addr; // backup page mapping addr

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (finished)
            {
                break;
            }
            else if (pmap_addr >= pmap_boundary)
            {
                finished = TRUE;
                break;
            }
            else if (pmap_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                finished = TRUE;
                pmap_bytes = (pmap_boundary - pmap_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR;
            }
            // read page mapping table from map_block
            nand_page_ptread(bank,
                             get_mapblk_vpn(bank, mapblk_lbn) / PAGES_PER_BLK,
                             get_mapblk_vpn(bank, mapblk_lbn) % PAGES_PER_BLK,
                             0,
                             pmap_bytes / BYTES_PER_SECTOR,
                             FTL_BUF(bank),
                             RETURN_ON_ISSUE);
            pmap_addr += pmap_bytes;
        }
        flash_finish();

        pmap_bytes = BYTES_PER_PAGE;
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (temp_page_addr >= pmap_boundary)
            {
                break;
            }
            else if (temp_page_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                pmap_bytes = (pmap_boundary - temp_page_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR;
            }
            // copy page mapping table to PMAP_ADDR from FTL buffer
            mem_copy(temp_page_addr, FTL_BUF(bank), pmap_bytes);

            temp_page_addr += pmap_bytes;
        }
        if (finished)
        {
            break;
        }
    }
}
static void write_format_mark(void)
{
	// This function writes a format mark to a page at (bank #0, block #0).

	#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;

	mem_set_dram(FTL_BUF_ADDR, 0, BYTES_PER_SECTOR);

	SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E | FO_B_W_DRDY);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// DRAM -> flash
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because we have waited for all the banks to become idle before returning from format().
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_IN_PROG command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the write operation
	while (BSP_FSM(0) != BANK_IDLE);
}
static BOOL32 check_format_mark(void)
{
	// This function reads a flash page from (bank #0, block #0) in order to check whether the SSD is formatted or not.

	#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;
	UINT32 temp;

	flash_clear_irq();	// clear any flash interrupt flags that might have been set

	SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// flash -> DRAM
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because scan list loading has been completed just before this function is called.
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_READ_OUT command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the read operation
	while (BSP_FSM(0) != BANK_IDLE);

	// Now that the read operation is complete, we can check interrupt flags.
	temp = BSP_INTR(0) & FIRQ_ALL_FF;

	// clear interrupt flags
	CLR_BSP_INTR(0, 0xFF);

	if (temp != 0)
	{
		return FALSE;	// the page contains all-0xFF (the format mark does not exist.)
	}
	else
	{
		return TRUE;	// the page contains something other than 0xFF (it must be the format mark)
	}
}

// BSP interrupt service routine
void ftl_isr(void)
{
    UINT32 bank;
    UINT32 bsp_intr_flag;

    uart_print("BSP interrupt occured...");
    // interrupt pending clear (ICU)
    SETREG(APB_INT_STS, INTR_FLASH);

    for (bank = 0; bank < NUM_BANKS; bank++) {
        while (BSP_FSM(bank) != BANK_IDLE);
        // get interrupt flag from BSP
        bsp_intr_flag = BSP_INTR(bank);

        if (bsp_intr_flag == 0) {
            continue;
        }
        UINT32 fc = GETREG(BSP_CMD(bank));
        // BSP clear
        CLR_BSP_INTR(bank, bsp_intr_flag);

        // interrupt handling
		if (bsp_intr_flag & FIRQ_DATA_CORRUPT) {
            uart_printf("BSP interrupt at bank: 0x%x", bank);
            uart_print("FIRQ_DATA_CORRUPT occured...");
		}
		if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L)) {
            uart_printf("BSP interrupt at bank: 0x%x", bank);
			if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG) {
                uart_print("find runtime bad block when block program...");
			}
			else {
                uart_printf("find runtime bad block when block erase...vblock #: %d", GETREG(BSP_ROW_H(bank)) / PAGES_PER_BLK);
				ASSERT(fc == FC_ERASE);
			}
		}
    }
}

UINT8 get_zone_state(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_state;
	zone_state = read_dram_8(ZONE_STATE_ADDR + zone_number*sizeof(UINT8));
	
	ASSERT(zone_state >=0 && zone_state <= 3);
	
	return zone_state;
}
void set_zone_state(UINT32 zone_number, UINT8 state)
{
	ASSERT(zone_number < NBLK);
	ASSERT(state >=0 && state <= 3);
	write_dram_8(ZONE_STATE_ADDR + zone_number*sizeof(UINT8), state);
}
UINT32 get_zone_wp(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_wp, zone_slba;
	zone_wp = read_dram_32(ZONE_WP_ADDR + zone_number*sizeof(UINT32));
	zone_slba = read_dram_32(ZONE_SLBA_ADDR + zone_number*sizeof(UINT32));
	ASSERT(zone_wp >= zone_slba && zone_wp < zone_slba + ZONE_SIZE);
	
	return zone_wp;
}
void set_zone_wp(UINT32 zone_number, UINT32 wp)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_slba = read_dram_32(ZONE_SLBA_ADDR + zone_number*sizeof(UINT32));
	ASSERT(zone_wp >= zone_slba && zone_wp < zone_slba + ZONE_SIZE);
	
	write_dram_32(ZONE_WP_ADDR + zone_number*sizeof(UINT32), wp);
}
UINT32 get_zone_slba(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_slba;
	zone_slba = read_dram_32(ZONE_SLBA_ADDR + zone_number*sizeof(UINT32));
	
	return zone_slba;
}
void set_zone_slba(UINT32 zone_number, UINT32 slba)
{
	ASSERT(zone_number < NBLK);
	write_dram_32(ZONE_SLBA_ADDR + zone_number*sizeof(UINT32), slba);
}

UINT32 get_buffer_sector(UINT32 zone_number, UINT32 sector_offset)
{
	ASSERT(zone_number < NBLK);
	ASSERT(sector_offset < SECTORS_PER_PAGE);
	
	UINT32 buf_data;
	buf_data = read_dram_32(ZONE_BUFFER_ADDR + (zone_number * SECTORS_PER_PAGE + sector_offset) * sizeof(UINT32));
	
	return buf_data;
}
void set_buffer_sector(UINT32 zone_number, UINT32 sector_offset, UINT32 data)
{
    //uart_printf("her1\n");
	ASSERT(zone_number < NBLK);
   // uart_printf("here2\n");
	ASSERT(sector_offset < SECTORS_PER_PAGE);
   // uart_printf("here3\n");
	write_dram_32(ZONE_BUFFER_ADDR + (zone_number * SECTORS_PER_PAGE + sector_offset) * sizeof(UINT32), data);
  //  uart_printf("here4\n");
}

UINT32 get_zone_to_FBG(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_FBG;
	zone_FBG = read_dram_32(ZONE_TO_FBG_ADDR + zone_number*sizeof(UINT32));
	
	ASSERT(zone_FBG < NBLK);
	
	return zone_FBG;
}
void set_zone_to_FBG(UINT32 zone_number, UINT32 FBG)
{
	ASSERT(zone_number < NBLK);
	ASSERT(zone_FBG < NBLK);
	write_dram_32(ZONE_TO_FBG_ADDR + zone_number*sizeof(UINT32), FBG);
}

void enqueue_FBG(UINT32 block_num)
{
	ASSERT(block_num < NBLK);
	wp = wp % NBLK;
	write_dram_32(FBQ_ADDR + wp * sizeof(UINT32), block_num);
	wp++;
}
UINT32 dequeue_FBG(void)
{
	rp = rp % NBLK;
	UINT32 block_num = read_dram_32(FBQ_ADDR + rp * sizeof(UINT32));
	rp++;
	ASSERT(block_num < NBLK);
	return block_num;
}

void enqueue_open_id(UINT8 open_zone_id)
{
	wp_open = wp_open % MAX_OPEN_ZONE;
	write_dram_8(OPEN_ZONE_Q_ADDR + wp_open * sizeof(UINT8), open_zone_id);
	wp_open++;
}

UINT8 dequeue_open_id(void)
{
	rp_open = rp_open % MAX_OPEN_ZONE;
	UINT8 id = read_dram_8(OPEN_ZONE_Q_ADDR + rp_open * sizeof(UINT8));
	rp_open++;
	return id;
}

UINT8 get_zone_to_ID(UINT32 zone_number)
{
	return read_dram_8(ZONE_TO_ID_ADDR + zone_number * sizeof(UINT8));
}
void set_zone_to_ID(UINT32 zone_number, UINT8 id)
{
	write_dram_8(ZONE_TO_ID_ADDR + zone_number * sizeof(UINT8), id);
}
// ZNS+
/*
UINT8 get_TL_bitmap(UINT32 zone_number, UINT32 page_offset)
{
	ASSERT(zone_number < NBLK);
	ASSERT(page_offset < NPAGE * DEG_ZONE);
	UINT8 data = read_dram_8(TL_BITMAP_ADDR + (zone_number * (NPAGE * DEG_ZONE) + page_offset)*sizeof(UINT8));
	ASSERT(data == 1 || data == 0);
	return data;
}
void set_TL_bitmap(UINT32 zone_number, UINT32 page_offset,  UINT8 data)
{
	ASSERT(zone_number < NBLK);
	ASSERT(page_offset < NPAGE * DEG_ZONE);
	ASSERT(data == 1 || data == 0);
	write_dram_8(TL_BITMAP_ADDR + zone_number * (NPAGE * DEG_ZONE) + page_offset, data);
}
UINT32 get_TL_wp(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 wp = read_dram_32(TL_WP_ADDR + zone_number*sizeof(UINT32));
	ASSERT(wp >= get_zone_slba(zone_number) && wp < get_zone_slba(zone_number) + ZONE_SIZE);
	return wp;
}
void set_TL_wp(UINT32 zone_number, UINT32 wp)
{
	ASSERT(zone_number < NBLK);
	ASSERT(wp >= get_zone_slba(zone_number) && wp < get_zone_slba(zone_number) + ZONE_SIZE);
	write_dram_32(TL_WP_ADDR + zone_number*sizeof(UINT32), wp);
}
UINT32 get_TL_buffer(UINT32 zone_number, UINT32 sector_offset)
{
	ASSERT(zone_number < NBLK);
	ASSERT(sector_offset < NSECT);
	UINT32 data = read_dram_32(TL_BUFFER_ADDR + (zone_number * NSECT + sector_offset) * sizeof(UINT32));
	return data;
}
set_TL_buffer(zone_number, sector_offset, UINT32 data)
{
	ASSERT(zone_number < NBLK);
	ASSERT(sector_offset < NSECT);
	write_dram_32(TL_BUFFER_ADDR + (zone_number * NSECT + sector_offset) * sizeof(UINT32), data);
}
UINT32 get_TL_num(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 data = read_dram_32(TL_NUM_ADDR + zone_number * sizeof(UINT32));
	return data;
}
void set_TL_num(UINT32 zone_number, UINT32 num)
{
	ASSERT(zone_number < NBLK);
	write_dram_32(TL_NUM_ADDR + zone_number * sizeof(UINT32), num);
}
*/
