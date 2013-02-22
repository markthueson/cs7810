#include <stdio.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"

extern long long int CYCLE_VAL;

long long int row_buffer_hits = 0;
long long int read_while_wait = 0;
int write_wait_cycles = 0;

void init_scheduler_vars()
{
	// initialize all scheduler variables here

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
	    write_wait_cycles += 1;
	  if(write_wait_cycles > 6)
	  {
	    write_wait_cycles = 0;
	    drain_writes[channel] = 1;
	  }
	}


	// If in write drain mode, look through all the write queue
	// elements (already arranged in the order of arrival), and
	// issue the command for the first request that is ready
	if(drain_writes[channel])
	{
		request_t * my_wr = NULL;
		int hit = 0;
		//LL_FOREACH(read_queue_head[channel],rd_ptr)
		for(wr_ptr=write_queue_head[channel];!hit && wr_ptr;wr_ptr=wr_ptr->next)
		{
			if(wr_ptr->command_issuable)
			{
				if(!my_wr)
				{
					my_wr = wr_ptr;
					if(my_wr->next_command == COL_WRITE_CMD)
					{
						row_buffer_hits += 1;
						hit = 1;
					}
				}
				else if(!hit && wr_ptr->next_command == COL_WRITE_CMD)
				{
					row_buffer_hits += 1;
					hit = 1;
					my_wr = wr_ptr;
				}
			}
		}
		if(my_wr)
			issue_request_command(my_wr);
		return;
	}

	// Draining Reads
	// look through the queue and find the first request whose
	// command can be issued in this cycle and issue it 
	// Simple FCFS 
	if(!drain_writes[channel])
	{
		request_t * my_rd = NULL;
		int hit = 0;
		//LL_FOREACH(read_queue_head[channel],rd_ptr)
		for(rd_ptr=read_queue_head[channel];!hit && rd_ptr;rd_ptr=rd_ptr->next)
		{
			if(rd_ptr->command_issuable)
			{
				if(!my_rd)
				{
					my_rd = rd_ptr;
					if(my_rd->next_command == COL_READ_CMD)
					{
						row_buffer_hits += 1;
						hit = 1;
					}
				}
				else if(!hit && rd_ptr->next_command == COL_READ_CMD)
				{
					row_buffer_hits += 1;
					hit = 1;
					my_rd = rd_ptr;
				}
			}
		}
		if(my_rd)
		{
			if(write_wait_cycles != 0)
				read_while_wait += 1;
			write_wait_cycles = 0;
			issue_request_command(my_rd);
		}
		return;
	}
}

void scheduler_stats()
{
  printf("Number of buffer hits: %lld\n", row_buffer_hits);
  printf("Number of reads from wait: %lld\n", read_while_wait);
}

