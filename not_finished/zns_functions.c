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
	ASSERT(zone_number < NBLK);
	ASSERT(sector_offset < SECTORS_PER_PAGE);

	write_dram_32(ZONE_BUFFER_ADDR + (zone_number * SECTORS_PER_PAGE + sector_offset) * sizeof(UINT32), data);
}

UINT32 get_zone_to_FBG(UINT32 zone_number)
{
	ASSERT(zone_number < NBLK);
	UINT32 zone_FBG;
	zone_FBG = read_dram_32(ZONE_TO_FBG_ADDR + zone_number*sizeof(UINT32));
	
	ASSERT(zone_FBG < NBLK);
	
	return zone_FBG;
}
void set_zone_to_FBG(zone_number, UINT32 FBG)
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
	UINT32 block_num = read_dram_32(FBQ_ADDR + wp * sizeof(UINT32));
	ASSERT(block_num < NBLK);
	return block_num;
}

// ZNS+
/*
UINT8 get_TL_bitmap(zone_number, sector_offset)
{
	ASSERT(zone_number < NBLK);
}
set_TL_bitmap(zone_number, sector_offset,  UINT8 data)
{
	ASSERT(zone_number < NBLK);
}
UINT32 get_TL_wp(zone_number)
{
	ASSERT(zone_number < NBLK);
}
set_TL_wp(zone_number, UINT32 wp)
{
	ASSERT(zone_number < NBLK);
}
UINT32 get_TL_buffer(zone_number, sector_offset)
{
	ASSERT(zone_number < NBLK);
}
set_TL_buffer(zone_number, sector_offset, UINT32 data)
{
	ASSERT(zone_number < NBLK);
}
*/