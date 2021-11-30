void zns_tl_open(UINT32 zone, UINT8* valid_arr)
{
	if(get_zone_state(zone) != 2) return;
	if(OPEN_ZONE == MAX_OPEN_ZONE) return;
	
	set_TL_num(zone, dequeue_FBG());
	
	OPEN_ZONE++;
	dequeue_OPEN_ID
}