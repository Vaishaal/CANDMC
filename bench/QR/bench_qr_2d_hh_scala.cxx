/* Copyright (c) Edgar Solomonik 2015, all rights reserved. This code is part of the CANDMC repository, protected under a two-clause BSD license. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <string>
#include <ios>
#include <fstream>
#include <iostream>

#include "CANDMC.h"

using namespace std;

/**
 * \brief Benchmark TSQR and HH reconstruction 
 *
 * \param[in] m number of rows in A
 * \param[in] k number of columns in A
 * \param[in] b2 outer block size of A
 * \param[in] b block size of A
 * \param[in] nprow number of procesor rows
 * \param[in] npcol number of procesor columns
 * \param[in] niter number of iterations
 * \param[in] myRank rank in communicator column
 * \param[in] numPes number of processes in column
 * \param[in] req_id request id to use for send/recv
 * \param[in] comm MPI communicator for column
 **/
void scala_qr_2d_bench( int64_t const m,
                  int64_t const k, 
                  int64_t const b2, 
                  int64_t const b, 
                  int64_t const nprow, 
                  int64_t const npcol, 
                  int64_t const niter, 
                  int64_t const myRank, 
                  int64_t const numPes, 
                  int64_t const req_id, 
                  CommData_t  cdt_glb){
  if (myRank == 0)  
    printf("benchmarking Scalapack with Aggregation...\n");
#ifndef USE_SCALAPACK
  assert(0);
#else
  double *A;
  double time;
  int64_t i,mb,iter,kb;

  int64_t seed_offset = 99900;
  CommData_t cdt_row, cdt_col;
  SETUP_SUB_COMM(cdt_glb, (cdt_row), 
                 myRank/nprow, 
                 myRank%nprow, 
                 npcol);
  SETUP_SUB_COMM(cdt_glb, (cdt_col), 
                 myRank%nprow, 
                 myRank/nprow, 
                 nprow);


  mb = m / nprow;
  kb = k / npcol;
  srand48(seed_offset);
  
  assert(0==(posix_memalign((void**)&A,
          ALIGN_BYTES,
          mb*kb*sizeof(double))));
  int icontxt, iam, inprocs, info;
  char cC = 'C';
  int desc_A[9];
  Cblacs_pinfo(&iam,&inprocs);
  Cblacs_get(-1, 0, &icontxt);
  int pr = cdt_col.np;
  int pc = cdt_row.np;
  Cblacs_gridinit(&icontxt, &cC, pr, pc);
  cdescinit(desc_A, m, k,
		        b, b,
		        0, 0,
		        icontxt, m/pr, 
				    &info);
  assert(info==0);

  time = MPI_Wtime();
  for (iter=0; iter<niter; iter++){
    for (i=0; i<mb*kb; i++){
      A[i] = drand48();
    }
    /* IMPORTANT: currently need to change sign of last R entry to align with LAPACK */
    pview pv;
    pv.rrow = 0;
    pv.rcol = 0;
    pv.crow = cdt_row;
    pv.ccol = cdt_col;
    pv.cworld = cdt_glb;
    QR_2D_2D(A,mb,m,k,b2,b,&pv,NULL,0, desc_A, A);
  }
  MPI_Barrier(cdt_glb.cm);
  time = MPI_Wtime()-time;

  if (myRank == 0)
    printf("2D QR with ScaLAPACK panel and aggregation on a " PRId64 "-by-" PRId64 " matrix took %lf seconds/iteration, at %lf GFlops\n",
            m, k, time/niter, (2.*m*k*k-(2./3.)*k*k*k)/(time/niter)*1.E-9);

  free(A);
#endif
}

int main(int argc, char **argv) {
  int myRank, numPes;
  int64_t m, k, b, b2, niter, nprow, npcol, transp_fact;

  CommData_t cdt_glb;
  INIT_COMM(numPes, myRank, 1, cdt_glb);

  if (argc == 2) {
    printf("Usage: mpirun -np <num procs> ./exe <number of rows> <number of columns> ");
    printf("<block size> <number of processor rows> <outer block size> <number of iterations> <column major?>\n");
    ABORT;
  }
  if (argc > 4) nprow = atoi(argv[4]);
  else {
    nprow = sqrt(numPes);
    while (numPes%nprow!=0) nprow++;
  }
  npcol = numPes/nprow;
  if (argc > 1) m = atoi(argv[1]);
  else m = 32*nprow;
  if (argc > 2) k = atoi(argv[2]);
  else k = 16*nprow;
  if (argc > 3) b = atoi(argv[3]);
  else b = MIN(m,MIN(k,8));
  if (argc > 5) b2 = atoi(argv[5]);
  else b2 = m;
  if (argc > 6) niter = atoi(argv[6]);
  else niter = 10;
  if (argc > 7) transp_fact = atoi(argv[7]);
  else transp_fact = 1;
  if (myRank == 0){
    printf("m=" PRId64 ", k=" PRId64 ", b = " PRId64 ", b2 = " PRId64 ", nprow = " PRId64 ", npcol = " PRId64 ", niter = " PRId64 ", transp_fact = " PRId64 "\n",
            m,k,b,b2,nprow,npcol,niter,transp_fact);
  }

#ifdef TAU
  TAU_PROFILE_TIMER(timer, "main", "int (int, char**)", TAU_USER);
  TAU_PROFILE_START(timer);
  TAU_PROFILE_INIT(argc, argv);
  TAU_PROFILE_SET_NODE(myRank);
  TAU_PROFILE_SET_CONTEXT(0);
#endif

  if (transp_fact == 1)
    scala_qr_2d_bench(m, k, b2, b, nprow, npcol, niter, myRank, numPes, 0, cdt_glb);
  else {
    CommData_t cdt_glb_transp; 
    int myrow, mycol;
    myrow = myRank / transp_fact;
    mycol = myRank % transp_fact;
    SETUP_SUB_COMM(cdt_glb, (cdt_glb_transp), (mycol*transp_fact+myrow), 0, numPes);
    scala_qr_2d_bench(m, k, b2, b, nprow, npcol, niter, myRank, numPes, 0, cdt_glb_transp);
    
  }
  TAU_PROFILE_STOP(timer);

  COMM_EXIT;
  return 0;
}
