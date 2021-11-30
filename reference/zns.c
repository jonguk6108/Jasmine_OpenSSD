/*
 * Lab #5 : ZNS+ Simulator
 *  - Embedded Systems Design, ICE3028 (Fall, 2021)
 *
 * Oct. 21, 2021.
 *
 * TA: Youngjae Lee, Jeeyoon Jung
 * Prof: Dongkun Shin
 * Embedded Software Laboratory
 * Sungkyunkwan University
 * http://nyx.skku.ac.kr
 */
#include "zns.h"
#include <stdio.h>
#include <stdlib.h>
#include "nand.h"

/************ constants ************/
int NBANK;
int NBLK;
int NPAGE;

int DEG_ZONE;
int MAX_OPEN_ZONE;

int NUM_FCG;
int NZONE;
/********** do not touch ***********/
int** ZONE; // [zone number][state, write pointer, state lba]; // 0 empty, 1 open, 2 full, 3 tlopen
u32** BUFFER; // [zone number][value of sectors + lba];
int* ZTF; //[zone number] = blk number;
int** FB; //[NUM_FCG][NBLK] // [queue] = size(numbers of freeblk), freeblk;
int** TL_BITMAP; // [zone number][bit map ( DEG_ZONE * NSECT * NPAGE) , zone_tlnum];
int* TL_WP;
u32** TL_BUFFER;
int OPEN_ZONE;

void zns_init(int nbank, int nblk, int npage, int dzone, int max_open_zone)
{
	// constants
	NBANK = nbank;
	NBLK = nblk;
	NPAGE = npage;
	DEG_ZONE = dzone;
	MAX_OPEN_ZONE = max_open_zone;
	NUM_FCG = NBANK / DEG_ZONE;
  NZONE = NBLK * NUM_FCG;
	// nand
	nand_init(nbank, nblk, npage);

  ZONE = (int **)malloc(sizeof(int*) * NZONE);
  for(int i = 0; i < NZONE; i++)
    ZONE[i] = (int *)malloc(sizeof(int) * 3);
  BUFFER = (u32 **)malloc(sizeof(u32*) * NZONE);
  for(int i = 0; i < NZONE; i++)
    BUFFER[i] = (u32 *)malloc(sizeof(u32) * NSECT);
  FB = (int **)malloc(sizeof(int*) * NUM_FCG);
  for(int i = 0; i < NUM_FCG; i++)
    FB[i] = (int *)malloc(sizeof(int) * (NBLK+1));
  ZTF = (int *)malloc(sizeof(int) * NZONE);
  TL_BITMAP = (int **)malloc(sizeof(int*) * NZONE);
  for(int i = 0; i < NZONE; i++)
    TL_BITMAP[i] = (int *)malloc(sizeof(int) * (DEG_ZONE * NSECT * NPAGE + 1));
  TL_WP = (int *)malloc(sizeof(int) * NZONE);
  TL_BUFFER = (u32 **)malloc(sizeof(u32*) * NZONE);
  for(int i = 0; i < NZONE; i++)
    TL_BUFFER[i] = (u32 *)malloc(sizeof(u32) * NSECT);

  for(int i = 0; i < NZONE; i++) {
    ZONE[i][0] = 0;
    ZONE[i][1] = i * DEG_ZONE * NSECT * NPAGE;
    ZONE[i][2] = i * DEG_ZONE * NSECT * NPAGE;
  }
  for(int i = 0; i < NZONE; i++)
    for(int j = 0; j < NSECT; j++)
      BUFFER[i][j] = - 1;
  for(int i = 0; i < NZONE; i++)
    ZTF[i] = -1;
  for(int i = 0; i < NUM_FCG; i++) 
    for(int j = 0; j <= NBLK; j++)
      FB[i][j] = j;
  for(int i = 0; i < NZONE; i++) {
    for(int j = 0; j < DEG_ZONE * NSECT * NPAGE; j++)
      TL_BITMAP[i][j] = 0;
    TL_BITMAP[i][DEG_ZONE * NSECT * NPAGE] = -1;
  }
  for(int i = 0; i < NZONE; i++)
    TL_WP[i] = 0;
  for(int i = 0; i < NZONE; i++)
    for(int j = 0; j < NSECT; j++)
      TL_BUFFER[i][j] = - 1;

  OPEN_ZONE = 0;
}

void fill_tl(int zone, int c_lba, int tl_num) {     //fill_tl(c_zone, c_lba + 1, tl_num + 1);
  int tl_size = DEG_ZONE * NSECT * NPAGE;
  int lba = c_lba;
  int c_sect = lba % NSECT;
  lba = lba / NSECT;
  int b_offset = lba % DEG_ZONE;
  lba = lba / DEG_ZONE;
  int p_offset = lba % NPAGE;
  int c_fcg = zone % NUM_FCG;
  int c_bank = c_fcg * DEG_ZONE + b_offset;


  if(tl_num >= tl_size)
    return;
  if(TL_BITMAP[zone][tl_num] == 0)
    return;
  
  u32 data[1];
  zns_read(c_lba, 1, data);

  TL_WP[zone]++;
  TL_BUFFER[zone][c_sect] = data[0];

  if(c_sect == NSECT - 1) {
    u32 spare[] = {c_lba};
    nand_write(c_bank, TL_BITMAP[zone][tl_size], p_offset, TL_BUFFER[zone], spare);
  }
  
  fill_tl(zone, c_lba + 1, tl_num + 1);
}

int zns_write(int start_lba, int nsect, u32 *data)
{
  int i_sect = 0;
  while(i_sect < nsect) {
    int c_lba = start_lba + i_sect;
    int lba = start_lba + i_sect;
    int c_sect = lba % NSECT;
    lba = lba / NSECT;
    int b_offset = lba % DEG_ZONE;
    lba = lba / DEG_ZONE;
    int p_offset = lba % NPAGE;
    lba = lba / NPAGE;
    int c_fcg = lba % NUM_FCG;

    int c_zone = lba;
    if(c_zone >= NZONE)
      return -1;
    int c_bank = c_fcg * DEG_ZONE + b_offset;

    if(ZONE[c_zone][0] == 0 || ZONE[c_zone][0] == 1) {

      if(ZONE[c_zone][1] != c_lba)
        return -1;

      if(ZONE[c_zone][0] == 0) {
        if(OPEN_ZONE == MAX_OPEN_ZONE) return -1;
        if(FB[c_fcg][NBLK] == 0) return -1;
        ZTF[c_zone] = FB[c_fcg][0];

        for(int i = 0; i < NBLK-1; i++)
          FB[c_fcg][i] = FB[c_fcg][i+1];
        FB[c_fcg][NBLK-1] = -1;
        FB[c_fcg][NBLK] -= 1; // use free blk!!
        OPEN_ZONE += 1;
        ZONE[c_zone][0] = 1;
      }
     
      ZONE[c_zone][1]++;
      BUFFER[c_zone][c_sect] = data[i_sect];

      if(c_sect == NSECT-1) {
        u32 spare[] = {c_lba};
        nand_write(c_bank, ZTF[c_zone], p_offset, BUFFER[c_zone], spare);
      }

      if(ZONE[c_zone][1] == ZONE[c_zone][2] + DEG_ZONE * NSECT * NPAGE) {//full checking
        ZONE[c_zone][0] = 2;
        OPEN_ZONE -= 1;
      }
    }

    else if(ZONE[c_zone][0] == 2)
      return -1;

    else if(ZONE[c_zone][0] == 3) {
      int tl_num = p_offset * DEG_ZONE * NSECT + b_offset * NSECT + c_sect;
      if(TL_BITMAP[c_zone][tl_num] == 1)
        return -1;
      if( TL_WP[c_zone] != tl_num)
        return -1;

      TL_WP[c_zone]++;
      TL_BUFFER[c_zone][c_sect] = data[i_sect];

      if(c_sect == NSECT-1) {
        u32 spare[] = {c_lba};
        nand_write(c_bank, TL_BITMAP[c_zone][DEG_ZONE * NSECT * NPAGE], p_offset, TL_BUFFER[c_zone], spare);
      }

      fill_tl(c_zone, c_lba + 1, tl_num + 1);

      if(TL_WP[c_zone] == DEG_ZONE * NSECT * NPAGE) {
        zns_reset(c_zone * DEG_ZONE * NSECT * NPAGE);
        ZONE[c_zone][0] = 2;
        ZTF[c_zone] = TL_BITMAP[c_zone][DEG_ZONE * NSECT * NPAGE];
        OPEN_ZONE -= 1;
      }
    }

    i_sect++;
  }
  return 0;
}

void zns_read(int start_lba, int nsect, u32 *data)
{
  int i_sect = 0;
  while(i_sect < nsect) {
    int c_lba = start_lba + i_sect;
    int lba = start_lba + i_sect;
    int c_sect = lba % NSECT;
    lba = lba / NSECT;
    int b_offset = lba % DEG_ZONE;
    lba = lba / DEG_ZONE;
    int p_offset = lba % NPAGE;
    lba = lba / NPAGE;
    int c_fcg = lba % NUM_FCG;

    int c_zone = lba;
    if(c_zone >= NZONE)
      return ;
    int c_bank = c_fcg * DEG_ZONE + b_offset;

    if(ZONE[c_zone][0] == 0) {
      data[i_sect] = -1;
      i_sect++;
      continue;
    }
    else if(ZONE[c_zone][0] == 1 || ZONE[c_zone][0] == 2) {
      if(ZONE[c_zone][1] <= c_lba) {
        data[i_sect] = -1;
        i_sect++;
        continue;
      }

      if( ( (ZONE[c_zone][1]-1) / NSECT) * NSECT <= c_lba && ((ZONE[c_zone][1]-1) % NSECT) != NSECT-1  )
       data[i_sect] = BUFFER[ c_zone ][c_sect];
      else {
        u32 r_data[NSECT];
        u32 spare[1];
        nand_read(c_bank, ZTF[c_zone], p_offset, r_data, spare);
        data[i_sect] = r_data[c_sect];
      }
    }
    else if(ZONE[c_zone][0] == 3) {
      int i_tl = c_lba - c_zone * DEG_ZONE * NSECT * NPAGE;

      if(TL_WP[c_zone] > i_tl) { // this data in tl zone
        if( ((TL_WP[c_zone]-1) / NSECT) * NSECT <= i_tl && ((TL_WP[c_zone]-1) % NSECT) != NSECT-1 )
          data[i_sect] = TL_BUFFER[c_zone][c_sect];
        else {
          u32 r_data[NSECT];
          u32 spare[1];
          nand_read(c_bank, TL_BITMAP[c_zone][DEG_ZONE * NSECT * NPAGE], p_offset, r_data, spare);
          data[i_sect] = r_data[c_sect];
        }
      }
      else {
        u32 r_data[NSECT];
        u32 spare[1];
        nand_read(c_bank, ZTF[c_zone], p_offset, r_data, spare);
        data[i_sect] = r_data[c_sect]; 
      }
    }

    i_sect++;
  }
  return;
}

int zns_reset(int lba)
{
  lba = lba / NSECT;
  int b_offset = lba % DEG_ZONE;
  lba = lba / DEG_ZONE;
  lba = lba / NPAGE;
  int c_fcg = lba % NUM_FCG;

  int c_zone = lba;
  int c_bank = c_fcg * DEG_ZONE + b_offset;

  if(c_zone >= NZONE)
    return -1;
  if(ZONE[c_zone][0] != 2)
    return -1;

  ZONE[c_zone][0] = 0;
  ZONE[c_zone][1] = ZONE[c_zone][2];
  nand_erase(c_bank, ZTF[c_zone]);
  
  FB[c_fcg][ FB[c_fcg][NBLK] ] = ZTF[c_zone];
  FB[c_fcg][NBLK] += 1;
  ZTF[c_zone] = -1;

  return 0;
}

void zns_get_desc(int lba, int nzone, struct zone_desc *descs)
{
  lba = lba / NSECT;
  lba = lba / DEG_ZONE;
  lba = lba / NPAGE;
  int c_zone = lba;
  
  for(int i = 0; i < nzone; i++) {
    if(i + c_zone >= NZONE)
      return;

    if(ZONE[i + c_zone][0] == 3) {
      descs[i].state = 3;
      descs[i].slba = ZONE[i + c_zone][2];
      descs[i].wp = TL_WP[i + c_zone] + ZONE[i+c_zone][2];
      continue;
    }

    descs[i].state = ZONE[i + c_zone][0];
    descs[i].slba = ZONE[i + c_zone][2];
    descs[i].wp = ZONE[i + c_zone][1];
  }
  return;
}

int zns_izc(int src_zone, int dest_zone, int copy_len, int *copy_list)
{
  if(src_zone == dest_zone )
    return -1;
  if(src_zone >= NZONE || dest_zone >= NZONE)
    return -1;
  if(ZONE[src_zone][0] != 2 || ZONE[dest_zone][0] != 0)
    return -1;

  u32 data[1];
  for(int i = 0; i < copy_len; i++) {

    int s_lba = ZONE[src_zone][2] + copy_list[i];
    zns_read(s_lba, 1, data);
    int d_lba = ZONE[dest_zone][2] + i;
    if( zns_write(d_lba, 1, data) == -1)
      return -1;
  }
  zns_reset( ZONE[src_zone][2]);
  return 0;
}

int zns_tl_open(int zone, u8 *valid_arr)
{
  int c_fcg = zone % NUM_FCG;
  if(ZONE[zone][0] != 2)
    return -1;
 
  if(OPEN_ZONE == MAX_OPEN_ZONE) return -1;
  if(FB[c_fcg][NBLK] == 0) return -1;
  TL_BITMAP[zone][DEG_ZONE * NSECT * NPAGE] = FB[c_fcg][0];

  for(int i = 0; i < NBLK -1; i++)
    FB[c_fcg][i] = FB[c_fcg][i+1];
  FB[c_fcg][NBLK-1] = -1;
  FB[c_fcg][NBLK] -= 1;
  OPEN_ZONE += 1;
  ZONE[zone][0] = 3;
  for(int i = 0; i < DEG_ZONE * NSECT * NPAGE; i++)
    TL_BITMAP[zone][i] = valid_arr[i];
  TL_WP[zone] = 0;
  
  fill_tl(zone, zone*DEG_ZONE * NSECT * NPAGE, 0);
  return 0;
}
