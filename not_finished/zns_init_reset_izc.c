///izc 수정함 아직 원복에 복붙 안함


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

void zns_get_desc(UINT32 c_zone, UINT32 nzone, struct zone_desc *descs)
{
	for(UINT i = 0; i < nzone; i++) 
	{
		ASSERT(i + c_zone < NZONE);

		if(get_zone_state(i + c_zone) == 3)
		{
			descs[i].state = 3;
			descs[i].slba = get_zone_slba(i + c_zone);
			descs[i].wp = get_TL_wp(i + c_zone) + get_zone_slba(i + c_zone);
			continue;
		}
		descs[i].state = get_zone_state(i + c_zone);
		descs[i].slba = get_zone_slba(i + c_zone);
		descs[i].wp = get_zone_wp(i + c_zone);
	}
  return;
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
		zns_read(s_lba, NSECT, TL_INTERNAL_BUFFER_ADDR);
		UINT32 d_lba = get_zone_slba(dest_zone) + i * NSECT;
		zns_write(d_lba, NSECT, TL_INTERNAL_BUFFER_ADDR);
	}
	zns_reset(get_zone_slba(src_zone));
	open_zone++;
}

void zns_write(UINT32 const start_lba, UINT32 const num_sectors) 
{
    UINT32 cnt_for_nandwrite = 0;
    UINT32 i_sect = 0;
    UINT32 next_write_buf_id;
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
            if (zone_wp <= c_lba)
            {
                return -1;
            }
            if (zone_state == 0)
            {
                //Q) max_open_zone reset에서는 안만짐??
                //if (OPEN_ZONE == MAX_OPEN_ZONE) return -1;
                //Q) dequeue에서는 사용할 것이 없ㅇ면 리턴을 어케줌??
                //if(FB[c_fcg][NBLK] == 0) return -1; 

                UINT32 dequeue_fbg = dequeue_FBG();
                set_zone_to_FBG(c_zone, dequeue_fbg);

                //OPEN_ZONE += 1;
                set_zone_state(c_zone, 1);
            }

            set_zone_wp(c_zone, get_zone_slba(c_zone) + 1);
            /*-------write to buffer----------*/

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
            set_buffer_sector(c_zone, c_sect, data);

            if (c_sect == NSECT - 1)
            {
                /*------------normal_nandwrite-------------*/
                cnt_for_nandwrite++;
                UINT32 vblk = get_zone_to_FBG(c_zone);
                nand_page_ptread_to_host(c_bank,
                    vblk,
                    p_offset,
                    (c_sect - cnt_for_nandwrite),
                    cnt_for_nandwrite);

                cnt_for_nandwrite = 0;
                /*------------------------------*/
            }

            if (get_zone_wp(c_zone) == get_zone_slba(c_zone) + ZONE_SIZE)
            {
                set_zone_state(c_zone, 2);
                //OPEN_ZONE -= 1;
            }
        }

        else if (zone_state == 2)
            return -1;

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

            /*-------write to buffer----------*/

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
                //OPEN_ZONE -= 1;
            }
        }

        i_sect++;
    }
}