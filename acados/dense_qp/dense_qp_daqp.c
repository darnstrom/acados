/*
 * Copyright 2019 Gianluca Frison, Dimitris Kouzoupis, Robin Verschueren,
 * Andrea Zanelli, Niels van Duijkeren, Jonathan Frey, Tommaso Sartor,
 * Branimir Novoselnik, Rien Quirynen, Rezart Qelibari, Dang Doan,
 * Jonas Koenemann, Yutao Chen, Tobias Schöls, Jonas Schlagenhauf, Moritz Diehl
 *
 * This file is part of acados.
 *
 * The 2-Clause BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.;
 */


// external
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
// blasfeo
#include "blasfeo/include/blasfeo_d_aux.h"
#include "blasfeo/include/blasfeo_d_blas.h"

// daqp
#include "daqp/include/types.h"
#include "daqp/include/api.h"
#include "daqp/include/daqp.h"
#include "daqp/include/utils.h"

// acados
#include "acados/dense_qp/dense_qp_common.h"
#include "acados/dense_qp/dense_qp_daqp.h"
#include "acados/utils/mem.h"
#include "acados/utils/timing.h"
#include "acados/utils/print.h"
#include "acados/utils/math.h"

#include "acados_c/dense_qp_interface.h"

/************************************************
 * auxiliary 
 ************************************************/

static void acados_daqp_get_dims(dense_qp_dims *dims, int *n_ptr, int *m_ptr,int *ms_ptr){
  *n_ptr = dims->nv; 
  *m_ptr = dims->nv+dims->ng; 
  *ms_ptr = dims->nv; 
}


/************************************************
 * opts
 ************************************************/

acados_size_t dense_qp_daqp_opts_calculate_size(void *config_, dense_qp_dims *dims)
{
    acados_size_t size = 0;
    size += sizeof(dense_qp_daqp_opts);
    size += sizeof(DAQPSettings);

    return size;
}



void *dense_qp_daqp_opts_assign(void *config_, dense_qp_dims *dims, void *raw_memory)
{
    dense_qp_daqp_opts *opts;

    char *c_ptr = (char *) raw_memory;

    opts = (dense_qp_daqp_opts *) c_ptr;
    c_ptr += sizeof(dense_qp_daqp_opts);

	opts->daqp_opts = (DAQPSettings *) c_ptr;
	c_ptr += sizeof(DAQPSettings);

    assert((char *) raw_memory + dense_qp_daqp_opts_calculate_size(config_, dims) == c_ptr);

    return (void *) opts;
}



void dense_qp_daqp_opts_initialize_default(void *config_, dense_qp_dims *dims, void *opts_)
{
    dense_qp_daqp_opts *opts = (dense_qp_daqp_opts *) opts_;
	daqp_default_settings(opts->daqp_opts);
	opts->warm_start=1;
    return;
}



void dense_qp_daqp_opts_update(void *config_, dense_qp_dims *dims, void *opts_)
{
    return;
}



void dense_qp_daqp_opts_set(void *config_, void *opts_, const char *field, void *value)
{
    dense_qp_daqp_opts *opts = opts_;
    if (!strcmp(field, "tol_stat"))
    {
	  // DAQP always "aims" at a stationary point 
    }
    else if (!strcmp(field, "tol_eq"))
    {
	  // Equality constraints are explicitly 
	  // handled by the working set
    }
    else if (!strcmp(field, "tol_ineq"))
    {
        double *tol = value;
        opts->daqp_opts->primal_tol = *tol;
    }
    else if (!strcmp(field, "tol_comp"))
    {
	  // Complementary slackness is implicitly 
	  // handled by the worlking set
    }
    if (!strcmp(field, "iter_max"))
    {
        int *iter_max= value;
        opts->daqp_opts->iter_limit = *iter_max;
    }
    else if (!strcmp(field, "warm_start"))
    {
	    int *warm_start = value;
		opts->warm_start = *warm_start;
	}
    else
    {
        printf("\nerror: dense_qp_daqp_opts_set: wrong field: %s\n", field);
        exit(1);
    }

    return;
}



/************************************************
 * memory
 ************************************************/

static acados_size_t daqp_workspace_calculate_size(int n, int m, int ms)
{
  acados_size_t size = 0;

  size += sizeof(DAQPWorkspace);
  size += sizeof(DAQPProblem);
  //size += sizeof(DAQPSettings);

  size += n * n * sizeof(c_float); // H
  size += 1 * n * sizeof(c_float); // f
  size += n * (m-ms) * sizeof(c_float); // A
  size += 2 * m * sizeof(c_float); // bupper/blower 
  size += 1 * m * sizeof(int); // sense 

  size += n * (m-ms) * sizeof(c_float); // M
  size += 2 * m * sizeof(c_float); // dupper/dlower 
  size += (n+1)*n/2 * sizeof(c_float); // Rinv 
  size += n * sizeof(c_float); // v 
  size += m * sizeof(int); // sense 
  size += m * sizeof(c_float); // scaling

  size += 2 * n * sizeof(c_float); // x & xold
  size += 2*(n+1) * sizeof(c_float); // lam & lam_star 
  size += n * sizeof(c_float); // u 

  size += (n+2)*(n+1)/2 * sizeof(c_float); // L
  size += (n+1) * sizeof(c_float); // D 

  size += 2*(n+1) * sizeof(c_float); //xldl & zldl

  size += (n+1) * sizeof(int); // WS 

  return size;
}


acados_size_t dense_qp_daqp_memory_calculate_size(void *config_, dense_qp_dims *dims, void *opts_)
{
	int n,m,ms;
	acados_daqp_get_dims(dims, &n,&m,&ms);
	int nb = dims->nb;
	int ns = dims->ns;

    acados_size_t size = sizeof(dense_qp_daqp_memory);

	size += daqp_workspace_calculate_size(n,m,ms);
	
	size += nb * 2 * sizeof(c_float); // lb_tmp & ub_tmp
	size += nb * 1 * sizeof(int); // idbx
	size += n * 1 * sizeof(int); // idxv_to_idxb;
	size += ns * 1 * sizeof(int); // idbs

    make_int_multiple_of(8, &size);

    return size;
}

static void *daqp_workspace_assign(int n, int m, int ms, void *raw_memory){
  DAQPWorkspace *work;
  char *c_ptr = (char *) raw_memory;

  work = (DAQPWorkspace *) c_ptr;
  c_ptr += sizeof(DAQPWorkspace);

  work->qp = (DAQPProblem *) c_ptr;
  c_ptr += sizeof(DAQPProblem);

  //work->settings = (DAQPSettings *) c_ptr;
  //c_ptr += sizeof(DAQPSettings);

  align_char_to(8, &c_ptr);

  // double

  work->qp->H = (c_float*) c_ptr;
  c_ptr += n * n * sizeof(c_float);
  
  work->qp->f = (c_float*) c_ptr;
  c_ptr += 1 * n * sizeof(c_float);
  
  work->qp->A = (c_float*) c_ptr;
  c_ptr += n * (m-ms) * sizeof(c_float);

  work->qp->bupper = (c_float*) c_ptr;
  c_ptr += 1 * m * sizeof(c_float);
  
  work->qp->blower = (c_float*) c_ptr;
  c_ptr += 1 * m * sizeof(c_float);

  work->M = (c_float *) c_ptr;
  c_ptr += n * (m - ms) * sizeof(c_float);

  work->dupper = (c_float *) c_ptr;
  c_ptr += 1 * m * sizeof(c_float);

  work->dlower = (c_float *) c_ptr;
  c_ptr += 1 * m * sizeof(c_float);
  
  work->Rinv = (c_float *) c_ptr;
  c_ptr += (n + 1) * n / 2 * sizeof(c_float);

  work->v = (c_float *) c_ptr;
  c_ptr += n * sizeof(c_float);
  
  work->scaling = (c_float *) c_ptr;
  c_ptr += m * sizeof(c_float);

  work->x = (c_float *) c_ptr;
  c_ptr += n * sizeof(c_float);
  
  work->xold = (c_float *) c_ptr;
  c_ptr += n * sizeof(c_float);

  work->lam = (c_float *) c_ptr;
  c_ptr += (n+1) * sizeof(c_float);
  
  work->lam_star = (c_float *) c_ptr;
  c_ptr += (n+1) * sizeof(c_float);

  work->u = (c_float *) c_ptr;
  c_ptr += n * sizeof(c_float);

  work->L = (c_float *) c_ptr;
  c_ptr += (n+2)*(n+1)/2 * sizeof(c_float);
  
  work->D = (c_float *) c_ptr;
  c_ptr += (n + 1) * sizeof(c_float);
  
  work->xldl = (c_float *) c_ptr;
  c_ptr += (n + 1) * sizeof(c_float);
  
  work->zldl = (c_float *) c_ptr;
  c_ptr += (n + 1) * sizeof(c_float);

  // ints 
  work->qp->sense = (int *) c_ptr;
  c_ptr += 1 * m * sizeof(int);

  work->sense = (int *) c_ptr;
  c_ptr += m * sizeof(int);
  
  work->WS= (int *) c_ptr;
  c_ptr += (n + 1) * sizeof(int);

  // Initialize constants of workspace
  work->qp->nb = 0;
  work->qp->bin_ids = NULL;

  work->n = n;
  work->m = m;
  work->ms = ms;
  work->fval = -1;
  work->n_active = 0;
  work->iterations = 0;
  work->sing_ind  = 0;
  work->soft_slack = 0;

  work->bnb = NULL; // No need to solve MIQP 

  // Make sure sense is clean 
  for(int ii=0; ii<m; ii++) work->sense[ii] = 0;

  return work;
}


void *dense_qp_daqp_memory_assign(void *config_, dense_qp_dims *dims, void *opts_,
                                     void *raw_memory)
{
    dense_qp_daqp_memory *mem;

	int n,m,ms;
	acados_daqp_get_dims(dims, &n,&m,&ms);
	int nb = dims->nb;
	int ns = dims->ns;


    // char pointer
    char *c_ptr = (char *) raw_memory;

    mem = (dense_qp_daqp_memory *) c_ptr;
    c_ptr += sizeof(dense_qp_daqp_memory);

    assert((size_t) c_ptr % 8 == 0 && "memory not 8-byte aligned!");

	// Assign raw memory to workspace
	mem->daqp_work = daqp_workspace_assign(n,m,ms,c_ptr);
	c_ptr += daqp_qp_calculate_size(n,m,ms);
	c_ptr += daqp_workspace_calculate_size(n,m,ms);

    assert((size_t) c_ptr % 8 == 0 && "double not 8-byte aligned!");

	mem->lb_tmp = (c_float *) c_ptr;
	c_ptr += nb * 1 * sizeof(c_float);

	mem->ub_tmp = (c_float *) c_ptr;
	c_ptr += nb * 1 * sizeof(c_float);

	mem->idxb = (int *) c_ptr;
	c_ptr += nb * 1 * sizeof(int);

	mem->idxv_to_idxb = (int *) c_ptr;
	c_ptr += n * 1 * sizeof(int);

	mem->idxs= (int *) c_ptr;
	c_ptr += ns * 1 * sizeof(int);

    assert((char *) raw_memory + dense_qp_daqp_memory_calculate_size(config_, dims, opts_) >=
           c_ptr);

    return mem;
}



void dense_qp_daqp_memory_get(void *config_, void *mem_, const char *field, void* value)
{
    // qp_solver_config *config = config_;
    dense_qp_daqp_memory *mem = mem_;

    if (!strcmp(field, "time_qp_solver_call"))
    {
        double *tmp_ptr = value;
        *tmp_ptr = mem->time_qp_solver_call;
    }
    else if (!strcmp(field, "iter"))
    {
        int *tmp_ptr = value;
        *tmp_ptr = mem->iter;
    }
    else
    {
        printf("\nerror: dense_qp_daqp_memory_get: field %s not available\n", field);
        exit(1);
    }

    return;

}

static void dense_qp_daqp_update_memory(dense_qp_in *qp_in, const dense_qp_daqp_opts *opts, dense_qp_daqp_memory *mem){
  // extract dense qp size
  DAQPWorkspace * work = mem->daqp_work;
  int nv  = qp_in->dim->nv;
  int nb  = qp_in->dim->nb;
  int ns  = qp_in->dim->ns;


  // extract daqp data
  double *lb_tmp = mem->lb_tmp; 
  double *ub_tmp = mem->ub_tmp;
  int *idxb = mem->idxb;
  int *idxs = mem->idxs;



  double * sink = work->dupper; // Use this memory "terminate" slack information 
								// fill in the upper triangular of H in dense_qp
  blasfeo_dtrtr_l(nv, qp_in->Hv, 0, 0, qp_in->Hv, 0, 0);
  // extract data from qp_in in row-major
  // (assumes that there are no equality constraints)
  d_dense_qp_get_all_rowmaj(qp_in, work->qp->H, work->qp->f, NULL, NULL, 
	  idxb, lb_tmp, ub_tmp, 
	  work->qp->A, work->qp->blower+nv, work->qp->bupper+nv,
	  sink, sink, sink, sink, idxs, sink, sink);


  // Setup upper/lower bounds
  for (int ii = 0; ii < nv; ii++)
  {
	work->qp->blower[ii] = -DAQP_INF;
	work->qp->bupper[ii] = +DAQP_INF;
	work->qp->sense[ii] |= IMMUTABLE;
  }
  for (int ii = 0; ii < nb; ii++)
  {
	work->qp->blower[idxb[ii]] = lb_tmp[ii];
	work->qp->bupper[idxb[ii]] = ub_tmp[ii];
	work->qp->sense[idxb[ii]] &= ~IMMUTABLE; // "Unignore" these bounds
	mem->idxv_to_idxb[idxb[ii]] = ii;
  }

  // Handle slack. 
  // XXX: DAQP uses a single slack variable for all soft constraints 
  // in contrast to the multiple slack variables used in HPIPM
  for (int ii = 0; ii < ns; ii++){
	if(idxs[ii]<nb)
	  work->qp->sense[idxb[idxs[ii]]] |= SOFT;
	else 
	  work->qp->sense[nb+idxs[ii]-nv] |= SOFT;
  }

}

static void dense_qp_daqp_fill_output(const DAQPWorkspace *work, const dense_qp_out *qp_out,dense_qp_dims *dims, int* idxv_to_idxb){
  int i;
  int nv = dims->nv;
  int nb = dims->nb;
  int ng = dims->ng;
  int ns = dims->ns;
  // primal variables
  blasfeo_pack_dvec(nv, work->x, 1, qp_out->v, 0);
  for(i=nv; i<nv+2*ns; i++){ // XXX: pack single slack into multiple.
	qp_out->v->pa[i] = work->soft_slack;
  }

  // dual variables
  c_float lam;
  for(i = 0; i < 2 * nb+ 2 * ng + 2 * ns; i++) qp_out->lam->pa[i]=0.0;
  for(i = 0; i < work->n_active; i++){
	lam = work->lam_star[i];
	if(work->WS[i] < nv) // bound constraint
	  if(lam >= 0.0)
		qp_out->lam->pa[nb+ng+idxv_to_idxb[work->WS[i]]] = lam;
	  else
		qp_out->lam->pa[idxv_to_idxb[work->WS[i]]] = -lam; 
	else // general constraint
	  if(lam >= 0.0)
		qp_out->lam->pa[2*nb+ng+work->WS[i]-nv] = lam; 
	  else
		qp_out->lam->pa[nb+work->WS[i]-nv] = -lam; 
  }
  // XXX multipliers for upper/lower bounds for slacks are  
  // always set to zero since such bounds are not used in DAQP

}
/************************************************
 * workspcae
 ************************************************/

acados_size_t dense_qp_daqp_workspace_calculate_size(void *config_, dense_qp_dims *dims, void *opts_)
{
    return 0;
}



/************************************************
 * functions
 ************************************************/

int dense_qp_daqp(void* config_, dense_qp_in *qp_in, dense_qp_out *qp_out, void *opts_, void *memory_, void *work_){
    qp_info *info = (qp_info *) qp_out->misc;
    acados_timer tot_timer, qp_timer, interface_timer;

    acados_tic(&tot_timer);
    acados_tic(&interface_timer);

    // cast structures
    dense_qp_daqp_opts *opts = (dense_qp_daqp_opts *) opts_;
    dense_qp_daqp_memory *memory = (dense_qp_daqp_memory *) memory_;
	// Move data into daqp workspace
	dense_qp_daqp_update_memory(qp_in,opts,memory);
    info->interface_time = acados_toc(&interface_timer);

	// Extract workspace and update settings
	DAQPWorkspace* work = memory->daqp_work;
	work->settings = opts->daqp_opts; 

	// === Solve starts === 
    acados_tic(&qp_timer);
	if(opts->warm_start==0) deactivate_constraints(work);
	// setup LDP
	int update_mask,daqp_status;
	update_mask= (opts->warm_start==2) ? 
	  UPDATE_v+UPDATE_d: UPDATE_Rinv+UPDATE_M+UPDATE_v+UPDATE_d;
	update_ldp(update_mask,work);
    // solve LDP 
	if(opts->warm_start==1) activate_constraints(work); //TODO: shift active set?
	daqp_status = daqp_ldp(memory->daqp_work); 
	ldp2qp_solution(work);

	// extract primal and dual solution
	dense_qp_daqp_fill_output(work,qp_out,qp_in->dim,memory->idxv_to_idxb);
    info->solve_QP_time = acados_toc(&qp_timer);

    acados_tic(&interface_timer);

    // compute slacks
	dense_qp_compute_t(qp_in, qp_out);
	info->t_computed = 1;
    
	// log solve info
	info->interface_time += acados_toc(&interface_timer);
    info->total_time = acados_toc(&tot_timer);
    info->num_iter = memory->daqp_work->iterations;
    memory->time_qp_solver_call = info->solve_QP_time;
    memory->iter = memory->daqp_work->iterations;

    int acados_status = daqp_status;
    if (daqp_status == EXIT_OPTIMAL || daqp_status == EXIT_SOFT_OPTIMAL) acados_status = ACADOS_SUCCESS;
    if (daqp_status == EXIT_ITERLIMIT) acados_status = ACADOS_MAXITER;
    return acados_status;
}


void dense_qp_daqp_eval_sens(void *config_, void *qp_in, void *qp_out, void *opts_, void *mem_, void *work_)
{
    printf("\nerror: dense_qp_daqp_eval_sens: not implemented yet\n");
    exit(1);
}


void dense_qp_daqp_config_initialize_default(void *config_)
{
    qp_solver_config *config = config_;

    config->opts_calculate_size = (acados_size_t (*)(void *, void *)) & dense_qp_daqp_opts_calculate_size;
    config->opts_assign = (void *(*) (void *, void *, void *) ) & dense_qp_daqp_opts_assign;
    config->opts_initialize_default =
        (void (*)(void *, void *, void *)) & dense_qp_daqp_opts_initialize_default;
    config->opts_update = (void (*)(void *, void *, void *)) & dense_qp_daqp_opts_update;
    config->opts_set = &dense_qp_daqp_opts_set;
    config->memory_calculate_size =
        (acados_size_t (*)(void *, void *, void *)) & dense_qp_daqp_memory_calculate_size;
    config->memory_assign =
        (void *(*) (void *, void *, void *, void *) ) & dense_qp_daqp_memory_assign;
    config->memory_get = &dense_qp_daqp_memory_get;
    config->workspace_calculate_size =
        (acados_size_t (*)(void *, void *, void *)) & dense_qp_daqp_workspace_calculate_size;
    config->eval_sens = &dense_qp_daqp_eval_sens;
    // config->memory_reset = &dense_qp_daqp_memory_reset;
    config->evaluate = (int (*)(void *, void *, void *, void *, void *, void *)) & dense_qp_daqp;

    return;
}
