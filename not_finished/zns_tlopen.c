//read, write에서 tl buffer, tl bitmap 수정 필요 : tlbuffer는 기존 buffer로, tl_bitmap index가 zone -> open_id

void zns_tl_open(UINT32 zone, UINT8* valid_arr)
{
	if(get_zone_state(zone) != 2) return;
	if(OPEN_ZONE == MAX_OPEN_ZONE) return;
	
	set_TL_src_to_dest_zone(zone, dequeue_FBG());
	
	OPEN_ZONE++;
	UINT8 open_id = dequeue_open_id();
	set_zone_to_id(zone, open_id);
	
	set zone_state(zone, 3);
	for(UINT32 i = 0; i < DEG_ZONE * NPAGE; i++)
	{
		set_TL_bitmap(open_id, i, valid_arr[i]);
	}
	set_TL_wp(zone, 0);
	
	fill_tl(zone, zone*ZONE_SIZE, 0);
}

void fill_tl(int zone, int c_lba, int tl_num)
{
	UINT32 c_sect = c_lba % NSECT;
	UINT32 c_bank = (c_lba / NSECT) % DEG_ZONE;
	UINT32 p_offset = (c_lba / NSECT / DEG_ZONE) % NPAGE;
	if(tl_num >= DEG_ZONE * NPAGE) return;
	UINT8 open_id = get_zone_to_ID(zone_number);
	if(get_TL_bitmap(open_id, tl_num) == 0) return;
	
	zns_read(c_lba, NSECT, TL_INTERNAL_BUFFER_ADDR);
	set_TL_wp(zone, get_TL_wp(zone) + NSECT);
	//nand_write(c_bank, get_TL_src_to_dest_zone(zone), p_offset, TL_INTERNAL_BUFFER_ADDR, spare); /// 여기는 고칠 필요가 있어용
	
	fill_tl(zone, c_lba + ZONE_SIZE, tl_num + 1);
}