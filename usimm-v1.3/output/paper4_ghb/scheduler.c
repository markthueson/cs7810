#include <stdio.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"

extern long long int CYCLE_VAL;
#define MAXGHBSIZE 512
#define MAXINDEXTABLE 1024

//GHB variables (global)
int GHBhead;
int GHBmaxed;

struct ToBeIssued
{
	int issue;
	int rank;
	int bank;
	long long int row;
};
struct ToBeIssued tbi[MAX_NUM_CHANNELS];
struct GHBentry
{
	int number;
	int thread_id;
	int channel;
	int rank;
	int bank;
	long long int row;
	long long int instruction_pc;
	long long int physical_address;
	struct GHBentry * link;
};

//variables required for stats print
int number_of_spec_activates;
int number_of_hits;

int activates[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];

//GHB
struct GHBentry GHB[MAXGHBSIZE];
//previous read queue sizes
int prev_rqsize[MAX_NUM_CHANNELS];

//index table
struct GHBentry * IndexTable[MAXINDEXTABLE];
//ggenerate index for inex table
int generateindex (  int thread_id, long long int instruction_pc, long long int physical_address)
{
	long long int xorred;
	//printf("thread_id is %X, instr_pc is %llX, addr is %llX \n",thread_id, instruction_pc, physical_address);
	xorred = instruction_pc ^ physical_address;
	xorred = xorred & 0x00000000000000FF;
	thread_id = thread_id & 0x00000003;
	thread_id = thread_id << 8;
	//printf("index is %d %X\n",thread_id + (int)xorred);
	return (thread_id + (int)xorred);
}

//push entry into GHB
void push ( dram_address_t dram_addr,int thread_id, long long int instruction_pc, long long int physical_address )
{
	struct GHBentry * loop = NULL;
	int GHBnewhead = (GHBhead+1)%MAXGHBSIZE; //head incremented
	
	
	if(GHBhead == MAXGHBSIZE)
		GHBmaxed = 1;

	int index;
	
	if(GHBmaxed == 1)  //if GHB is maxed, then tail entry must be removed
	{
		index = generateindex(GHB[GHBnewhead].thread_id, GHB[GHBnewhead].instruction_pc, GHB[GHBnewhead].physical_address);
		loop = IndexTable[index];
		if(loop == &GHB[GHBnewhead])
		{
			IndexTable[index] = NULL;
		}
		else
		{

				if(loop->link == &GHB[GHBnewhead])
				{
					loop->link = NULL;
				
				}
			
		}
	}
	
	
	//insert GHB entry
	GHB[GHBnewhead].thread_id = thread_id;
	GHB[GHBnewhead].channel = dram_addr.channel;
	GHB[GHBnewhead].rank = dram_addr.rank;
	GHB[GHBnewhead].bank = dram_addr.bank;
	GHB[GHBnewhead].row = dram_addr.row;
	GHB[GHBnewhead].physical_address = physical_address;
	GHB[GHBnewhead].instruction_pc = instruction_pc;
	index = generateindex(thread_id, instruction_pc, physical_address);
	GHB[GHBnewhead].link = IndexTable[index];
	IndexTable[index] = &GHB[GHBnewhead];

	GHBhead = GHBnewhead;
	




}

void init_scheduler_vars()
{
	int i;
	// initialize all scheduler variables here
	for (i=0; i<MAX_NUM_CHANNELS;i++)
	{
		prev_rqsize[i] = 0;
	}
	number_of_spec_activates=0;
	number_of_hits=0;

	
	int o;
		int p;
		
		for(i=0;i<MAX_NUM_CHANNELS;i++){
		for (o=0; o<MAX_NUM_RANKS; o++) {
	  		for (p=0; p<MAX_NUM_BANKS; p++) {
	     			activates[i][o][p]=0;
					
	  		}
		}
		}

	GHBhead = -1;
	GHBmaxed = 0;
	for (i=0; i<MAXINDEXTABLE ; i++)
	{
		IndexTable[i] = NULL;
	}
	for (i=0; i<MAXGHBSIZE ; i++)
	{
		GHB[i].number = i;
	}

		for (i=0 ; i<MAX_NUM_CHANNELS ; i++)
	{
		tbi[i].issue = 0;
	}
	return;
}

// write queue high water mark; begin draining writes if write queue exceeds this value
#define HI_WM 40

// end write queue drain once write queue has this many writes in it
#define LO_WM 20

// 1 means we are in write-drain mode for that channel
int drain_writes[MAX_NUM_CHANNELS];

/* Each cycle it is possible to issue a valid command from the read or write queues
   OR
   a valid precharge command to any bank (issue_precharge_command())
   OR 
   a valid precharge_all bank command to a rank (issue_all_bank_precharge_command())
   OR
   a power_down command (issue_powerdown_command()), programmed either for fast or slow exit mode
   OR
   a refresh command (issue_refresh_command())
   OR
   a power_up command (issue_powerup_command())
   OR
   an activate to a specific row (issue_activate_command()).

   If a COL-RD or COL-WR is picked for issue, the scheduler also has the
   option to issue an auto-precharge in this cycle (issue_autoprecharge()).

   Before issuing a command it is important to check if it is issuable. For the RD/WR queue resident commands, checking the "command_issuable" flag is necessary. To check if the other commands (mentioned above) can be issued, it is important to check one of the following functions: is_precharge_allowed, is_all_bank_precharge_allowed, is_powerdown_fast_allowed, is_powerdown_slow_allowed, is_powerup_allowed, is_refresh_allowed, is_autoprecharge_allowed, is_activate_allowed.
   */

void schedule(int channel)
{
	request_t * rd_ptr = NULL;
	request_t * wr_ptr = NULL;
	request_t * updater = NULL;
	int i=0;
	
	//find position in read queue where new entries start
	LL_FOREACH(read_queue_head[channel], updater)
	{
		if(i == prev_rqsize[channel])
			break;
		else
			i++;			
	}

	
	//push new entries into the GHB
	for(;updater;updater = updater->next)
	{
		
		push(updater->dram_addr,updater->thread_id, updater->instruction_pc, updater->physical_address);				
	
	} 
	prev_rqsize[channel] = read_queue_length[channel];


	// if in write drain mode, keep draining writes until the
	// write queue occupancy drops to LO_WM
	if (drain_writes[channel] && (write_queue_length[channel] > LO_WM)) {
	  drain_writes[channel] = 1; // Keep draining.
	}
	else {
	  drain_writes[channel] = 0; // No need to drain.
	}

	// initiate write drain if either the write queue occupancy
	// has reached the HI_WM , OR, if there are no pending read
	// requests
	if(write_queue_length[channel] > HI_WM)
	{
		drain_writes[channel] = 1;
	}
	else {
	  if (!read_queue_length[channel])
	    drain_writes[channel] = 1;
	}

	int j;
	
	int o;
		int p;
		
		int Isused[MAX_NUM_RANKS][MAX_NUM_BANKS];
		for (o=0; o<MAX_NUM_RANKS; o++) {
	  		for (p=0; p<MAX_NUM_BANKS; p++) {
	     			Isused[o][p]=0;
					
	  		}
		}

	// If in write drain mode, look through all the write queue
	// elements (already arranged in the order of arrival), and
	// issue the command for the first request that is ready
	if(drain_writes[channel])
	{

		LL_FOREACH(write_queue_head[channel], wr_ptr)
		{
			if(wr_ptr->command_issuable)
			{
				issue_request_command(wr_ptr);
				break;
			}
		}
		return;
	}

	// Draining Reads
	// look through the queue and find the first request whose
	// command can be issued in this cycle and issue it 
	// Simple FCFS 
	if(!drain_writes[channel])
	{
		LL_FOREACH(read_queue_head[channel],rd_ptr)
		{
			if(rd_ptr->command_issuable)
			{
				issue_request_command(rd_ptr);
				break;
			}
		}
		return;
	}
		//try from GHB

		int index;
		struct GHBentry * loop = NULL;
		struct GHBentry *  head = NULL;


		head = &GHB[GHBhead];

		int r;
		for(r=0;r<3;r++)
		{

		loop = head->link;
		head = head->link;

 
		if(loop!=NULL)
			index = ((loop->number)+1)%MAXGHBSIZE;
		else
			return;
		
		for(j=0;j<20;j++)
		{
			if(index == GHBhead)
				break;
			
			if(Isused[GHB[index].rank][GHB[index].bank] != 0 || channel != GHB[index].channel)
			{
				index = (index + 1)%MAXGHBSIZE;
				continue;
			}
			
			
			if (dram_state[GHB[index].channel][GHB[index].rank][GHB[index].bank].state == ROW_ACTIVE)
			{
				if(dram_state[GHB[index].channel][GHB[index].rank][GHB[index].bank].active_row != GHB[index].row)
				{
					if(is_precharge_allowed(GHB[index].channel,GHB[index].rank,GHB[index].bank))
					{
						issue_precharge_command(GHB[index].channel,GHB[index].rank,GHB[index].bank);
						
						activates[GHB[index].channel][GHB[index].rank][GHB[index].bank] = 0 ;
				
						tbi[channel].issue = 0;	
						return;
					}
				}
				
			}
			index = (index + 1)%MAXGHBSIZE;
		}
		if(tbi[channel].issue == 1)
		{
			if(!Isused[tbi[channel].rank][tbi[channel].bank] && dram_state[channel][tbi[channel].rank][tbi[channel].bank].state == PRECHARGING && is_activate_allowed(channel, tbi[channel].rank, tbi[channel].bank))
			{
				issue_activate_command(channel, tbi[channel].rank, tbi[channel].bank, tbi[channel].row);
				//free(next_dram_addr);
				activates[channel][tbi[channel].rank][tbi[channel].bank] =1;
				number_of_spec_activates = number_of_spec_activates+1;
								
				tbi[channel].issue = 0;
				return;
			}
			if(!Isused[tbi[channel].rank][tbi[channel].bank]  && dram_state[channel][tbi[channel].rank][tbi[channel].bank].state == ROW_ACTIVE && is_precharge_allowed(channel, tbi[channel].rank, tbi[channel].bank))
			{
				issue_precharge_command(channel, tbi[channel].rank, tbi[channel].bank);
				tbi[channel].issue = 0;
				activates[channel][tbi[channel].rank][tbi[channel].bank] =0;			
				//free(next_dram_addr);
				return;
			}
				
		}		

		}	
}

void scheduler_stats()
{
  /* Nothing to print for now. */
}

