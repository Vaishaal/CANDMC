#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <time.h>
#include "string.h"
#include "assert.h"
#include <iostream>
#include <vector>
#include "timer.h"
#include "util.h"

#define MAX_TOT_SYMBOLS_LEN 1000000

int main_argc = 0;
const char * const * main_argv;
MPI_Comm comm;
double excl_time;
double complete_time;
int set_contxt = 0;
int output_file_counter = 0;

CTF_Function_timer::CTF_Function_timer(char const * name_, 
                               double const start_time_,
                               double const start_excl_time_){
  snprintf(name, MAX_NAME_LENGTH, "%s", name_);
  start_time = start_time_;
  start_excl_time = start_excl_time_;
  acc_time = 0.0;
  acc_excl_time = 0.0;
  calls = 0;
}

void CTF_Function_timer::compute_totals(MPI_Comm comm){ 
  PMPI_Allreduce(&acc_time, &total_time, 1, 
                MPI_DOUBLE, MPI_SUM, comm);
  PMPI_Allreduce(&acc_excl_time, &total_excl_time, 1, 
                MPI_DOUBLE, MPI_SUM, comm);
  PMPI_Allreduce(&calls, &total_calls, 1, 
                MPI_INT, MPI_SUM, comm);
}

bool CTF_Function_timer::operator<(CTF_Function_timer const & w) const {
  return total_time > w.total_time;
}

void CTF_Function_timer::print(FILE *         output, 
                           MPI_Comm const comm, 
                           int const      rank,
                           int const      np){
  int i;
  if (rank == 0){
    fprintf(output, "%s", name);
    char * space = (char*)malloc(MAX_NAME_LENGTH-strlen(name)+1);
    for (i=0; i<MAX_NAME_LENGTH-(int)strlen(name); i++){
      space[i] = ' ';
    }
    space[i] = '\0';
    fprintf(output, "%s", space);
    fprintf(output,"%5d   %3d.%03d   %3d.%02d  %3d.%03d   %3d.%02d\n",
            total_calls/np,
            (int)(total_time/np),
            ((int)(1000.*(total_time)/np))%1000,
            (int)(100.*(total_time)/complete_time),
            ((int)(10000.*(total_time)/complete_time))%100,
            (int)(total_excl_time/np),
            ((int)(1000.*(total_excl_time)/np))%1000,
            (int)(100.*(total_excl_time)/complete_time),
            ((int)(10000.*(total_excl_time)/complete_time))%100);
    free(space);
  } 
}

bool comp_name(CTF_Function_timer const & w1, CTF_Function_timer const & w2) {
  return strcmp(w1.name, w2.name)>0;
}

std::vector<CTF_Function_timer> function_timers;

CTF_Timer::CTF_Timer(const char * name){
#ifdef PROFILE
  int i;
  if (function_timers.size() == 0) {
    if (name[0] == 'M' && name[1] == 'P' && 
        name[2] == 'I' && name[3] == '_'){
      exited = 1;
      original = 0;
      return;
    }
    original = 1;
    index = 0;
    excl_time = 0.0;
    function_timers.push_back(CTF_Function_timer(name, MPI_Wtime(), 0.0)); 
  } else {
    for (i=0; i<(int)function_timers.size(); i++){
      if (strcmp(function_timers[i].name, name) == 0){
        /*function_timers[i].start_time = MPI_Wtime();
        function_timers[i].start_excl_time = excl_time;*/
        break;
      }
    }
    index = i;
    original = (index==0);
  }
  if (index == (int)function_timers.size()) {
    function_timers.push_back(CTF_Function_timer(name, MPI_Wtime(), excl_time)); 
  }
  timer_name = name;
  exited = 0;
#endif
}
  
void CTF_Timer::start(){
#ifdef PROFILE
  if (!exited){
    function_timers[index].start_time = MPI_Wtime();
    function_timers[index].start_excl_time = excl_time;
  }
#endif
}

void CTF_Timer::stop(){
#ifdef PROFILE
  if (!exited){
    double delta_time = MPI_Wtime() - function_timers[index].start_time;
    function_timers[index].acc_time += delta_time;
    function_timers[index].acc_excl_time += delta_time - 
          (excl_time- function_timers[index].start_excl_time); 
    excl_time = function_timers[index].start_excl_time + delta_time;
    function_timers[index].calls++;
    exit();
    exited = 1;
  }
#endif
}

CTF_Timer::~CTF_Timer(){ }

void CTF_print_timers(char const * name){
  int rank, np, i, j, len_symbols, nrecv_symbols;

  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &np);


  char all_symbols[MAX_TOT_SYMBOLS_LEN];
  char recv_symbols[MAX_TOT_SYMBOLS_LEN];
  FILE * output = NULL;

  if (rank == 0){
    char filename[300];
    char part[300];
    
    sprintf(filename, "profile.%s.",name);
    srand(time(NULL));
    sprintf(filename+strlen(filename), "%d.", output_file_counter);
    output_file_counter++;
    
    int off;
    if (main_argc > 0){
      for (i=0; i<main_argc; i++){
        for (off=strlen(main_argv[i]); off>=1; off--){
          if (main_argv[i][off-1] == '/') break;
        }
        sprintf(filename+strlen(filename), "%s.", main_argv[i]+off);
      }
    } 
    sprintf(filename+strlen(filename), "-p%d.out", np);
    
    
    output = stdout;// fopen(filename, "w");
    printf("%s\n",filename);
    char heading[MAX_NAME_LENGTH+200];
    for (i=0; i<MAX_NAME_LENGTH; i++){
      part[i] = ' ';
    }
    part[i] = '\0';
    sprintf(heading,"%s",part);
    //sprintf(part,"calls   total sec   exclusive sec\n");
    sprintf(part,"       inclusive         exclusive\n");
    strcat(heading,part);
    fprintf(output, "%s", heading);
    for (i=0; i<MAX_NAME_LENGTH; i++){
      part[i] = ' ';
    }
    part[i] = '\0';
    sprintf(heading,"%s",part);
    sprintf(part, "calls        sec       %%"); 
    strcat(heading,part);
    sprintf(part, "       sec       %%\n"); 
    strcat(heading,part);
    fprintf(output, "%s", heading);

  }
  len_symbols = 0;
  for (i=0; i<(int)function_timers.size(); i++){
    sprintf(all_symbols+len_symbols, "%s", function_timers[i].name);
    len_symbols += strlen(function_timers[i].name)+1;
  }
  if (np > 1){
    for (int lp=1; lp<log2(np)+1; lp++){
      int gap = 1<<lp;
      if (rank%gap == gap/2){
        PMPI_Send(&len_symbols, 1, MPI_INT, rank-gap/2, 1, comm);
        PMPI_Send(all_symbols, len_symbols, MPI_CHAR, rank-gap/2, 2, comm);
      }
      if (rank%gap==0 && rank+gap/2<np){
        MPI_Status stat;
        PMPI_Recv(&nrecv_symbols, 1, MPI_INT, rank+gap/2, 1, comm, &stat);
        PMPI_Recv(recv_symbols, nrecv_symbols, MPI_CHAR, rank+gap/2, 2, comm, &stat);
        for (i=0; i<nrecv_symbols; i+=strlen(recv_symbols+i)+1){
          j=0;
          while (j<len_symbols && strcmp(all_symbols+j, recv_symbols+i) != 0){
            j+=strlen(all_symbols+j)+1;
          }
          
          if (j>=len_symbols){
            sprintf(all_symbols+len_symbols, "%s", recv_symbols+i);
            len_symbols += strlen(recv_symbols+i)+1;
          }
        }
      }
    }
    PMPI_Bcast(&len_symbols, 1, MPI_INT, 0, comm);
    PMPI_Bcast(all_symbols, len_symbols, MPI_CHAR, 0, comm);
    j=0;
    while (j<len_symbols){
      CTF_Timer t(all_symbols+j);
      j+=strlen(all_symbols+j)+1;
    }
  }
  LIBT_ASSERT(len_symbols <= MAX_TOT_SYMBOLS_LEN);

  std::sort(function_timers.begin(), function_timers.end(),comp_name);
  for (i=0; i<(int)function_timers.size(); i++){
    function_timers[i].compute_totals(comm);
  }
  std::sort(function_timers.begin(), function_timers.end());
  complete_time = function_timers[0].total_time;
  if (rank == 0){
    for (i=0; i<(int)function_timers.size(); i++){
      function_timers[i].print(output,comm,rank,np);
    }
  }
  
  /*  if (rank == 0){
    fclose(output);
  } */

}

void CTF_Timer::exit(){
#ifdef PROFILE
  if (set_contxt && original && !exited) {
    if (comm != MPI_COMM_WORLD){
      //function_timers.clear();
      return;
    }
    char const * str = "all";
    CTF_print_timers(str);  
    function_timers.clear();
  }
#endif
}

void CTF_set_main_args(int argc, const char * const * argv){
  main_argv = argv;
  main_argc = argc;
}

void CTF_set_context(MPI_Comm ctxt){
  if (!set_contxt)
    comm = ctxt;
  set_contxt = 1;
}

CTF_Timer_epoch::CTF_Timer_epoch(char const * name_){
#ifdef PROFILE
  name = name_;
#endif
}

void CTF_Timer_epoch::begin(){
#ifdef PROFILE
  tmr_outer = new CTF_Timer(name);
  tmr_outer->start();
  saved_function_timers = function_timers;
  save_excl_time = excl_time;
  excl_time = 0.0;
  function_timers.clear();
  tmr_inner = new CTF_Timer(name);
  tmr_inner->start();
#endif
}

void CTF_Timer_epoch::end(){
#ifdef PROFILE
  tmr_inner->stop();
  function_timers.clear();
  function_timers = saved_function_timers;
  excl_time = save_excl_time;
  tmr_outer->stop();
  delete tmr_inner;
  delete tmr_outer;
#endif
}
