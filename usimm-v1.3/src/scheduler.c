#include <stdio.h>
#include "utlist.h"
#include "utils.h"
#include "params.h"
#include <stdlib.h>

#include "memory_controller.h"

#define MAXINDEXTABLE 1024




struct ToBeIssued
{
	int issue;
	int rank;
	int bank;
	long long int row;
};

struct ToBeIssued tbi[MAX_NUM_CHANNELS];

//variables required for stats print
int number_of_spec_activates;
int number_of_hits;

int activates[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];
		

extern long long int CYCLE_VAL;

//previous read queue sizes
int prev_rqsize[MAX_NUM_CHANNELS];




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
	if(drain_writes[channel])
	{
		
		//go through write queue
		LL_FOREACH(write_queue_head[channel], wr_ptr)
		{
			//label isused accordingly
			if(Isused[wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank]==0)
				Isused[wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank]=2;
			
			//first ready served
			if(wr_ptr->command_issuable && wr_ptr->next_command == COL_WRITE_CMD)
			{
				if(activates[channel][wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank] == 1)
					number_of_hits ++;
				issue_request_command(wr_ptr);
				tbi[channel].issue = 0;
				return;
			}
			
		}
		LL_FOREACH(write_queue_head[channel], wr_ptr)
		{
			if(wr_ptr->command_issuable)
			{
				if(wr_ptr->next_command == PRE_CMD)
					activates[channel][wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank] = 0 ;
				issue_request_command(wr_ptr);
				tbi[channel].issue = 0;
				return;
			}
			
		}
	}
		LL_FOREACH(read_queue_head[channel],rd_ptr)
		{
			if(Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]==0)
				Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]=1;
			else if (Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]==2)
				Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]=3;


			if(rd_ptr->command_issuable && rd_ptr->next_command == COL_READ_CMD && !drain_writes[channel] && Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]<2)
			{
				if(activates[channel][rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank] == 1)
					number_of_hits ++;
				issue_request_command(rd_ptr);
				prev_rqsize[channel] = prev_rqsize[channel] - 1;
				tbi[channel].issue = 0;
				return;
			}
		}
		

		LL_FOREACH(read_queue_head[channel],rd_ptr)
		{
			if(rd_ptr->command_issuable && Isused[rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]<2)
			{
				if(rd_ptr->next_command == PRE_CMD)
					activates[channel][rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank] = 0 ;
				issue_request_command(rd_ptr);
				tbi[channel].issue = 0;
				return;
			}
		}
		//precharge from write queue
		LL_FOREACH(write_queue_head[channel], wr_ptr)
		{
			if(wr_ptr->command_issuable && wr_ptr->next_command == PRE_CMD && Isused[wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank]%2==0)
			{
				activates[channel][wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank] =0;
				issue_request_command(wr_ptr);
				tbi[channel].issue = 0;
				return;
			}
			
		}
		

}

void scheduler_stats()
{
	printf("\nNumber of speculative activates = %d ", number_of_spec_activates);
	printf("\nNumber of row hits = %d ", number_of_hits);
	

  /* Nothing to print for now. */
}
