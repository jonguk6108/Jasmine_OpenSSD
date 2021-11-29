void zns_init(void)
{
	for(UINT32 i = 0; i < NZONE; i++)
	{
		set_zone_state(i, 0);
		set_zone_slba(i, i * ZONE_SIZE);
		set_zone_wp(i, i * ZONE_SIZE);
		
		set_zone_to_FBG(i, -1);
		
		for(UINT32 j = 0; j < NSECT; j++)
		{
			set_buffer_sector(i, j, -1);
		}
	}
	
	for(UINT32 i = 0; i< NBLK; i++)
	{
		enqueue_FBG(i);
	}
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

void zns_reset(UINT32 lba)
{
	UINT32 c_zone = lba / ZONE_SIZE;
	UINT32 c_bank = (lba / NSECT) % DEG_ZONE;
	
	ASSERT(c_zone < NZONE);
	ASSERT(get_zone_state(c_zone) == 2)
	
	set_zone_state(c_zone, 0);
	set_zone_wp(c_zone, get_zone_slba(c_zone));
	//nand_erase
	
	enqueue_FBG(get_zone_to_FBG(c_zone));
	set_zone_to_FBG(c_zone, -1);

}

void zns_izc(UINT32 src_zone, UINT32 dest_zone, UINT32 copy_len, UINT32 *copy_list)
{
	ASSERT(src_zone != dest_zone);
	ASSERT(src_zone < NZONE && dest_zone < NZONE);
	ASSERT(get_zone_state(src_zone) == 2 && get_zone_state(dest_zone) == 0);
	
	UINT32 data[NSECT];
	for(UINT32 i = 0; i < copy_len; i++)
	{
		UINT32 s_lba = get_zone_slba(src_zone) + copy_list[i] * NSECT;
		zns_read(s_lba, NSECT, data);
		UINT32 d_lba = get_zone_slba(dest_zone) + i * NSECT;
		zns_write(d_lba, NSECT, data);
	}
	zns_reset(get_zone_slba(src_zone));
}