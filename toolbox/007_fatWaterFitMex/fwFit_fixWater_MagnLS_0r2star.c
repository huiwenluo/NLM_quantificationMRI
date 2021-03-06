#include "mex.h"
#include "string.h"
#include "math.h"
#include "stdlib.h"
#include <stdio.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlin.h>



/* Some helper functions */
double dmin(double a, double b);
int imax(int a, int b);
int  createSignal_magn_1R2star_f(const gsl_vector * x, void *data, gsl_vector * f);
int  createSignal_magn_1R2star_df(const gsl_vector * x, void *data, gsl_matrix * J);
int  createSignal_magn_1R2star_fdf(const gsl_vector * x, void *data,gsl_vector * f, gsl_matrix * J);
void print_state (size_t iter, gsl_multifit_fdfsolver * s);

#define PI 3.14159265
#define GYRO 42.58
#define MAX_ITER 10


struct data {
  int nte;
  double *te;
  double *curs;
  double *swr;
  double *swi;
  double *sfr;
  double *sfi;
  double w;
};



void mexFunction( int nlhs, mxArray *plhs[],
                  int nrhs, const mxArray *prhs[] )
{
  const char **fnames1, **fnames2, **fnames3;       /* pointers to field names */
  const mwSize *dims;
  mxArray    *tmp, *fout, *species;
  char       *pdata=NULL;

  /* Fat water parameters */
  int nx,ny,nte,fnum,nf;
  const mwSize *imDims;
  double *ims, *te, fieldStrength;
  double waterAmp, *fPPM,*fF, *relAmps;
  double *initW, *initF, *initR2, *outW, *outF, *outR2;
  
  /* Internal parameters */
  int kx,ky,kt,kf;
  double *curs, *sfr, *sfi, *swr, *swi, initEst[1];


  /* check proper input and output */
  if(nrhs!=3)
    mexErrMsgTxt("Three inputs required: imDataParams, algoParams, initParams.");
  else if(nlhs > 1)
    mexErrMsgTxt("Too many output arguments (outParams needed)");
  else if(!mxIsStruct(prhs[0]) || !mxIsStruct(prhs[1]) || !mxIsStruct(prhs[2]) )
    mexErrMsgTxt("Input must be a structure.");

  /* Get imDataParams */
  fnum = mxGetFieldNumber(prhs[0], "images");
  if( fnum>=0 ){
    ims = mxGetPr(mxGetFieldByNumber(prhs[0], 0, fnum));
    imDims = mxGetDimensions(mxGetFieldByNumber(prhs[0], 0, fnum));
    nx = (int)(imDims[0]);
    ny = (int)(imDims[1]);
    nte = (int)(imDims[2]);
  } else {
    mexErrMsgTxt("Data input error: images not correctly specified");
  }

  fnum = mxGetFieldNumber(prhs[0], "TE");
  if( fnum>=0 ){
    te = mxGetPr(mxGetFieldByNumber(prhs[0], 0, fnum));
    imDims = mxGetDimensions(mxGetFieldByNumber(prhs[0], 0, fnum));
    nte = imax((int)(imDims[0]),(int)(imDims[1]));
  } else {
    mexErrMsgTxt("Data input error: TE not correctly specified");
  }

  fnum = mxGetFieldNumber(prhs[0], "FieldStrength");
  if( fnum>=0 ){
    fieldStrength = (double)(mxGetScalar(mxGetFieldByNumber(prhs[0], 0, fnum)));
      } else {
    fieldStrength = 1.5;
  }
  /*
  mexPrintf("%s%d%s%d%s%d%s%f\n", "nx: ", nx, ", ny: ", ny, ", nte :", nte, ", field :", fieldStrength);
  mexPrintf("%s%f%s%f%s%f\n", "im1: ", ims[0], ", te1: ", te[0], ", te2 :", te[1]);
  */
  /* Get algoParams */
  fnum = mxGetFieldNumber(prhs[1], "species");
  if( fnum>=0 ){
    species = mxGetFieldByNumber(prhs[1], 0, fnum);
    waterAmp = (double)(mxGetScalar(mxGetField(species, 0, "relAmps")));
    relAmps = mxGetPr(mxGetField(species, 1, "relAmps"));
    fPPM = mxGetPr(mxGetField(species, 1, "frequency"));
    imDims = mxGetDimensions(mxGetField(species, 1, "relAmps"));
    nf = imax((int)(imDims[0]),(int)(imDims[1]));
  } else {
    mexErrMsgTxt("Algo input error: species not correctly specified");
  }

  /* Get initParams */
  fnum = mxGetFieldNumber(prhs[2], "species");
  if( fnum>=0 ){
    species = mxGetFieldByNumber(prhs[2], 0, fnum);
    initW = mxGetPr(mxGetField(species, 0, "amps"));
    initF = mxGetPr(mxGetField(species, 1, "amps")); 
  } else {
    mexErrMsgTxt("Init input error: init amplitudes not correctly specified");
  }

  fnum = mxGetFieldNumber(prhs[2], "r2starmap");
  if( fnum>=0 ){
    initR2 = mxGetPr(mxGetFieldByNumber(prhs[2], 0, fnum));
  } else {
    mexErrMsgTxt("Init input error: init R2* not correctly specified");
  }


  /* Go process */
  /* Initialize output structure */
  plhs[0] = mxDuplicateArray(prhs[2]);
  fnum = mxGetFieldNumber(plhs[0], "species");
  if( fnum>=0 ){
    species = mxGetFieldByNumber(plhs[0], 0, fnum);
    outW = mxGetPr(mxGetField(species, 0, "amps"));
    outF = mxGetPr(mxGetField(species, 1, "amps")); 
  } else {
    mexErrMsgTxt("Init input error: init amplitudes not correctly specified");
  }

  fnum = mxGetFieldNumber(plhs[0], "r2starmap");
  if( fnum>=0 ){
    outR2 = mxGetPr(mxGetFieldByNumber(plhs[0], 0, fnum));
  } else {
    mexErrMsgTxt("Init input error: init R2* not correctly specified");
  }

  curs = (double *)malloc(nte*sizeof(double));
  sfr = (double *)malloc(nte*sizeof(double));
  sfi = (double *)malloc(nte*sizeof(double));
  swr = (double *)malloc(nte*sizeof(double));
  swi = (double *)malloc(nte*sizeof(double));

  /* Initialize gsl stuff   */
  double *sigma;
  curs = (double *)malloc(nte*sizeof(double));
  
  const gsl_multifit_fdfsolver_type *T;
  gsl_multifit_fdfsolver *s;

  int status;
  size_t i, iter = 0;

  const size_t n = nte;
  const size_t p = 1;

  gsl_matrix *covar = gsl_matrix_alloc (p, p);

  struct data d;

  d.nte = nte;
  d.curs = curs;
  d.te = te;
  d.swr = swr;
  d.swi = swi;
  d.sfr = sfr;
  d.sfi = sfi;
  d.w = 0.0;
  
  gsl_multifit_function_fdf f; 
  gsl_vector_view x = gsl_vector_view_array (initEst, p);
  const gsl_rng_type * type;
  gsl_rng * r;

  gsl_rng_env_setup();

  type = gsl_rng_default;
  r = gsl_rng_alloc (type);

  f.f = &createSignal_magn_1R2star_f;
  f.df = &createSignal_magn_1R2star_df;
  f.fdf = &createSignal_magn_1R2star_fdf;
  f.n = n;
  f.p = p;
  f.params = &d;

  T = gsl_multifit_fdfsolver_lmsder;
  s = gsl_multifit_fdfsolver_alloc (T, n, p);

  /* Initialize water/fat signal models */
  fF = (double *)malloc(nf*sizeof(double));
  for(kf=0;kf<nf;kf++) {
    fF[kf] = fPPM[kf]*GYRO*fieldStrength;
    /*    mexPrintf("%s%f%s%f%s%f\n", "fieldSgrength: " , fieldStrength, "gyro: " , GYRO, "fF: " , fF[kf]);*/
  }
  for(kt=0;kt<nte;kt++) {
    swr[kt] = waterAmp;
    swi[kt] = 0.0;
    sfr[kt] = 0.0;
    sfi[kt] = 0.0;
    for(kf=0;kf<nf;kf++) {
      
      sfr[kt] = sfr[kt] + relAmps[kf]*cos(2*PI*te[kt]*fF[kf]);
      sfi[kt] = sfi[kt] + relAmps[kf]*sin(2*PI*te[kt]*fF[kf]);

    }    

    /*    mexPrintf("%s%d%s%f%s%f%s%f%s%f\n", "nf: " , nf, "swr: ", swr[kt],", swr: ", swi[kt]," , sfr: ", sfr[kt],", sfi: ", sfi[kt]);*/


  }
  
  /* Loop over all pixels */
  for(kx=0;kx<nx;kx++) {
    for(ky=0;ky<ny;ky++) {
      
      /* Get signal at current voxel */
      for(kt=0;kt<nte;kt++) {
	curs[kt] = ims[kx + ky*nx + kt*nx*ny];
      }
      

      
      d.w = initW[kx + ky*nx];
      initEst[0] = initF[kx + ky*nx];
/*       initEst[2] = initR2[kx + ky*nx]; */
      

      /* Do fitting */
      /*      dlevmar_dif(createSignal_magn_1R2star,initEst,curs,3,nte,MAX_ITER,NULL,NULL,work,covar,adata); */
      x = gsl_vector_view_array (initEst, p);
      gsl_multifit_fdfsolver_set (s, &f, &x.vector);
      iter = 0;
      do
	{
	  iter++;
	  status = gsl_multifit_fdfsolver_iterate (s);
	  /*
	  printf ("status = %s\n", gsl_strerror (status));
	  print_state (iter, s);
	  */
	  if (status)
	    break;
	  
	  status = gsl_multifit_test_delta (s->dx, s->x,1e-4, 1e-4);
	}
      while (status == GSL_CONTINUE && iter < MAX_ITER);

      outW[kx + ky*nx] = initW[kx + ky*nx];
      outF[kx + ky*nx] = gsl_vector_get (s->x, 0);
      outR2[kx + ky*nx] = 0.0; /*  gsl_vector_get (s->x, 2);  */
      
    }
  }
  
  free(curs);
  free(sfr);
  free(sfi);
  free(swr);
  free(swi);
  free(fF);
  gsl_multifit_fdfsolver_free (s);
  return;
}

void
print_state (size_t iter, gsl_multifit_fdfsolver * s)
{
  printf ("iter: %3u x = % 15.8f  "
          "|f(x)| = %g\n",
          iter,
          gsl_vector_get (s->x, 0), 
          gsl_blas_dnrm2 (s->f));
}


int  createSignal_magn_1R2star_f(const gsl_vector * x, void *data, gsl_vector * f) {
  size_t kt;
  double shat;

  int nte = ((struct data *)data)->nte;
  double *curs = ((struct data *)data)->curs;
  double *te = ((struct data *)data)->te;
  double *swr = ((struct data *)data)->swr;
  double *swi = ((struct data *)data)->swi;
  double *sfr = ((struct data *)data)->sfr;
  double *sfi = ((struct data *)data)->sfi;
  double W = ((struct data *)data)->w;

/*   double W = gsl_vector_get (x, 0); */
  double F = gsl_vector_get (x, 0);
  double r2 = 0.0; /*gsl_vector_get (x, 2);*/
  
  /*  mexPrintf("%s%f%s%f%s%f\n", "W: ", W, ", r2*: ", r2 );*/


  for(kt=0;kt<nte;kt++) {

    shat = exp(-te[kt]*r2)*sqrt((W*swr[kt] + F*sfr[kt])*(W*swr[kt] + F*sfr[kt]) + (W*swi[kt] + F*sfi[kt])*(W*swi[kt] + F*sfi[kt]));

    
    gsl_vector_set (f, kt,  shat - curs[kt]);
    

    /*    mexPrintf("%s%f%s%f%s%f\n", "s: ", curs[kt], ", shat: ", shat , ", te: ", te[kt]);*/
  }

  return GSL_SUCCESS;
}


/*
  k1 = real(sum(RA.*exp(j*2*pi*FS.*T),2));
  k2 = imag(sum(RA.*exp(j*2*pi*FS.*T),2));

  J = zeros(3,N);

  
  J = [ds1(:) , ds2(:), ds3(:)+ds4(:)];
*/

int  createSignal_magn_1R2star_df(const gsl_vector * x, void *data, gsl_matrix * J) {
  size_t kt;
  double curJ1,curJ2,curJ3,shat;
  
  int nte = ((struct data *)data)->nte;
  double *curs = ((struct data *)data)->curs;
  double *te = ((struct data *)data)->te;
  double *swr = ((struct data *)data)->swr;
  double *swi = ((struct data *)data)->swi;
  double *sfr = ((struct data *)data)->sfr;
  double *sfi = ((struct data *)data)->sfi;
  double W = ((struct data *)data)->w;

/*   double W = gsl_vector_get (x, 0); */
  double F = gsl_vector_get (x, 0);
  double r2 = 0.0; /*gsl_vector_get (x, 2);*/

  for(kt=0;kt<nte;kt++) {

    
    /*  shat = abs(rhoW.*exp(-r2w*t(:)) + rhoF*sum(RA.*exp(j*(2*pi*FS.*T)).*exp(-r2f*T),2));
	ds1 = (rhoW.*exp(-2*r2w*t) + rhoF*k1.*exp(-(r2w+r2f)*t))./(shat);
	ds2 = (rhoF.*(k1.^2+k2.^2).*exp(-2*r2f*t) + rhoW*k1.*exp(-(r2w+r2f)*t))./(shat);
	ds3 = (-rhoW^2*t.*exp(-2*r2w*t) -rhoW*rhoF*k1.*t.*exp(-(r2w+r2f)*t) )./shat;
	ds4 = (-rhoF^2*t.*(k1.^2+k2.^2).*exp(-2*r2f*t) - rhoW*rhoF*k1.*t.*exp(-(r2w+r2f)*t) )./shat; */
    shat = sqrt((W*swr[kt] + F*sfr[kt])*(W*swr[kt] + F*sfr[kt]) + (W*swi[kt] + F*sfi[kt])*(W*swi[kt] + F*sfi[kt]));
/*     curJ1 = exp(-te[kt]*r2)*(W + F*sfr[kt])/shat; */
/*     gsl_matrix_set (J, kt, 0, curJ1);  */

    curJ2 = exp(-te[kt]*r2)*(F*(sfr[kt]*sfr[kt] + sfi[kt]*sfi[kt]) + W*sfr[kt])/shat;
    gsl_matrix_set (J, kt, 0, curJ2); 
      
/*     curJ3 = exp(-te[kt]*r2)*(-W*W*te[kt] - W*F*sfr[kt]*te[kt] -F*F*te[kt]*(sfr[kt]*sfr[kt] + sfi[kt]*sfi[kt]) - W*F*sfr[kt]*te[kt])/shat; */
/*     gsl_matrix_set (J, kt, 2, curJ3);  */

    
    /*    mexPrintf("%s%f%s%f%s%f\n", "j1: ", curJ1, ", j2: ", curJ2 , ", j3: ", curJ3);*/

  }

  /*  mexPrintf("\n\n");*/

  return GSL_SUCCESS;
}

int  createSignal_magn_1R2star_fdf(const gsl_vector * x, void *data,gsl_vector * f, gsl_matrix * J) {
  createSignal_magn_1R2star_f (x, data, f);
  createSignal_magn_1R2star_df (x, data, J);
  return GSL_SUCCESS;
}


double dmin(double a, double b) {
  if( a < b ) {
    return a;
  } else {
    return b;
  }
}

int imax(int a, int b) {
  if( a < b ) {
    return b;
  } else {
    return a;
  }
}
