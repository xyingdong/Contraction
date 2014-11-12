//============================================================================
// Name        : LapHs.cpp
// Author      : BK
// Version     :
// Copyright   : Copies are prohibited so far
// Description : stochastic LapH code
//============================================================================

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>

#include "GlobalData.h"
#include "BasicOperator.h"
#include "Perambulator.h"
#include "typedefs.h"

#include "omp.h"

int main (int ac, char* av[]) {
  // ***************************************************************************
  // ***************************************************************************
  // initialization ************************************************************
  // ***************************************************************************
  // ***************************************************************************

  // initialization of OMP paralization
  // TODO: number of thread must be set via inputfile
  Eigen::initParallel();
  omp_set_num_threads(2);
  Eigen::setNbThreads(1); // parallel eigen makes everything slower

  // reading in global parameters from input file
  GlobalData* global_data = GlobalData::Instance();
  global_data->read_parameters(ac, av);

  // everything for operator handling
  BasicOperator* basic = new BasicOperator();

  // global variables from input file needed in main function
  const int Lt = global_data->get_Lt();
  const int end_config = global_data->get_end_config();
  const int delta_config = global_data->get_delta_config();
  const int start_config = global_data->get_start_config();
  const int number_of_eigen_vec = global_data->get_number_of_eigen_vec();

  //const int number_of_max_mom = global_data->get_number_of_max_mom();
  const int max_mom_squared = global_data->get_number_of_max_mom();
  //number_of_max_mom * number_of_max_mom;
  const int number_of_momenta = global_data->get_number_of_momenta();
  const std::vector<int> mom_squared = global_data->get_momentum_squared();

  const std::vector<quark> quarks = global_data->get_quarks();
  const int number_of_rnd_vec = quarks[0].number_of_rnd_vec;

  const int dirac_min = global_data->get_dirac_min();
  const int dirac_max = global_data->get_dirac_max();
  std::vector<int> dirac_ind {5};
  const int number_of_dirac = dirac_ind.size();

  int displ_min = global_data->get_displ_min();
  int displ_max = global_data->get_displ_max();
  const int number_of_displ = displ_max - displ_min + 1;

  const int p_min = 0; //number_of_momenta/2;
  const int p_max = number_of_momenta;

  // TODO decide on path
  std::string outpath = global_data->get_output_path() + "/" + 
      global_data->get_name_lattice();

  // other variables
  clock_t time;

  const std::complex<double> I(0.0, 1.0);

  char outfile[400];
  FILE *fp = NULL;

  // ***************************************************************************
  // ***************************************************************************
  // memory allocation *********************************************************
  // ***************************************************************************
  // ***************************************************************************

  // abbreviations for clearer memory allocation. Wont be used in loops and 
  // when building the contractions
  const size_t nmom = number_of_momenta;
  const size_t nrnd = number_of_rnd_vec;
  const size_t ndir = number_of_dirac;
  const size_t ndis = number_of_displ;

  const size_t dilE = quarks[0].number_of_dilution_E;

  // memory for the correlation function
  //TODO: dont need the memory for p_u^2 > p_d^2
  array_cd_d5 C4_mes(boost::extents[nmom][nmom][ndir][ndir][Lt]);
  array_cd_d10 Corr(boost::extents[nmom][nmom][ndir][ndir][ndis][ndis][Lt][Lt]
                                    [nrnd][nrnd]);

  int norm = 0;
  for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
    for(int rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){      
      if(rnd2 != rnd1){
        for(int rnd3 = 0; rnd3 < number_of_rnd_vec; ++rnd3){
          if((rnd3 != rnd2) && (rnd3 != rnd1)){
            for(int rnd4 = 0; rnd4 < number_of_rnd_vec; ++rnd4){
              if((rnd4 != rnd1) && (rnd4 != rnd2) && (rnd4 != rnd3)){
                norm++;
                //std::cout << "\n\nnorm: " << norm << rnd1 << rnd3 << rnd2 << rnd4 << std::endl;
              }
            }
          }
        }
      }
    }
  }
  std::cout << "\n\tNumber of contraction combinations: " << norm << std::endl;
  const double norm1 = Lt * norm;

  // memory for intermediate matrices when building C4_3 (save multiplications)
  array_Xcd_d7_eigen X(boost::extents[nmom][nmom][ndir][ndir][nrnd][nrnd][nrnd]);
  array_Xcd_d7_eigen Y(boost::extents[nmom][nmom][ndir][ndir][nrnd][nrnd][nrnd]);
  std::fill(X.data(), X.data() + X.num_elements(), 
            Eigen::MatrixXcd(4 * dilE, 4 * dilE));
  std::fill(Y.data(), Y.data() + Y.num_elements(), 
            Eigen::MatrixXcd(4 * dilE, 4 * dilE));
  // ***************************************************************************
  // ***************************************************************************
  // Loop over all configurations **********************************************
  // ***************************************************************************
  // ***************************************************************************
  for(int config_i = start_config; config_i <= end_config; config_i +=
      delta_config){

    std::cout << "\nprocessing configuration: " << config_i << "\n\n";
    basic->set_basic(config_i);
    // setting the correlation function to zero
    std::cout << "\n\tcomputing the traces of pi_+/-:\r";
    time = clock();
    // setting the correlation functions to zero
    std::fill(Corr.data(), Corr.data() + Corr.num_elements(), 
                                                              cmplx(0.0, 0.0));
    std::fill(C4_mes.data(), C4_mes.data() + C4_mes.num_elements(), 
                                                               cmplx(0.0, 0.0));
    // calculate all two-operator traces of the form tr(u \Gamma \bar{d}) and 
    // build all combinations of momenta, dirac_structures and displacements as
    // specified in infile
    for(int t_source = 0; t_source < Lt; ++t_source){
    std::cout << "\tcomputing the traces of pi_+/-: " 
        << std::setprecision(2) << (float) t_source/Lt*100 << "%\r" 
        << std::flush;
    int t_source_1 = (t_source + 1) % Lt;
    // this is a way to reuse the already computed operators from t_source_1
    if(t_source != 0){
      basic->swap_operators();
      basic->init_operator_u(1, t_source_1, 'b', 0);
      basic->init_operator_d(1, t_source_1, 'b', 0);
    }
    else {
      basic->init_operator_u(0, t_source,   'b', 0);
      basic->init_operator_u(1, t_source_1, 'b', 0);
      basic->init_operator_d(0, t_source,   'b', 0);
      basic->init_operator_d(1, t_source_1, 'b', 0);
    }
    for(int t_sink = 0; t_sink < Lt; ++t_sink){
      int t_sink_1 = (t_sink + 1) % Lt;

//      for(int displ_u = 0; displ_u < number_of_displ; displ_u++){
//      for(int displ_d = 0; displ_d < number_of_displ; displ_d++){ 
//        // initialize contraction[rnd_i] = perambulator * basicoperator = D_u^-1
//        // choose 'i' for interlace or 'b' for block time dilution scheme
//        // TODO: get that from input file
//        // choose 'c' for charged or 'u' for uncharged particles
//        for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){
//        for(int p_u = p_min; p_u < p_max; ++p_u) {
//          // "multiply contraction[rnd_i] with gamma structure"
//          // contraction[rnd_i] are the columns of D_u^-1 which get
//          // reordered by gamma multiplication. No actual multiplication
//          // is carried out
//          for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
//          for(int p_d = p_min; p_d < p_max; ++p_d) {
//            // same as get_operator but with gamma_5 trick. D_u^-1 is
//            // daggered and multipied with g_5 from left and right
//            // the momentum is changed to reflect the switched sign 
//            // in the momentum exponential for pi_+-
//            #pragma omp parallel for collapse(1) schedule(dynamic)
//            for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
//            for(int rnd2 = rnd1+1; rnd2 < number_of_rnd_vec; ++rnd2){
//              // build all 2pt traces leading to C2_mes
//              // Corr = tr(D_d^-1(t_sink) Gamma 
//              //     D_u^-1(t_source) Gamma)
//              Corr[p_u][p_d][dirac_u][dirac_d][displ_u][displ_d]
//                  [t_source][t_sink][rnd1][rnd2] = 
//                  (basic->get_operator_charged(0, t_sink, dirac_u, p_u, rnd1, rnd2) *
//                   basic->get_operator_g5(0, t_sink, dirac_d, p_d, rnd2)).trace();
//
//            }} // Loops over random vectors end here! 
//          }}// Loops over dirac_d and p_d end here
//        }}// Loops over dirac_u and p_u end here
//      }}// Loops over displacements end here
//
//      // Using the dagger operation to get all possible random vector combinations
//      // TODO: Think about imaginary correlations functions - There might be an 
//      //       additional minus sign involved
//      for(int displ_u = 0; displ_u < number_of_displ; displ_u++){
//      for(int displ_d = 0; displ_d < number_of_displ; displ_d++){
//        for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){
//        for(int p_u = p_min; p_u < p_max; ++p_u) {
//          for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
//          for(int p_d = p_min; p_d < p_max; ++p_d) {
//            // TODO: A collpase of both random vectors might be better but
//            //       must be done by hand because rnd2 starts from rnd1+1
//            #pragma omp parallel for collapse(1) schedule(dynamic)
//            for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
//            for(int rnd2 = rnd1+1; rnd2 < number_of_rnd_vec; ++rnd2){
//              Corr[p_u][p_d][dirac_d][dirac_u][displ_u]
//                  [displ_d][t_source][t_sink][rnd2][rnd1] = 
//                       std::conj(Corr[p_u][p_d][dirac_u][dirac_d][displ_u]
//                                     [displ_d][t_source][t_sink][rnd1][rnd2]); 
//            }} // Loops over random vectors end here! 
//          }}// Loops over dirac_d and p_d end here
//        }}// Loops over dirac_u and p_u end here
//      }}// Loops over displacements end here


      // ***********************************************************************
      // FOUR PT CONTRACTION 3 *************************************************
      // ***********************************************************************
      // build 4pt-function C4_mes for pi^+pi^+ Equivalent two just summing
      // up the four-trace with same time difference between source and sink 
      // (all to all) for every dirac structure, momentum
      // displacement not supported at the moment
      // to build the trace with four matrices, build combinations 
      // these have dimension // (4 * quarks[0].number_of_dilution_E) x (4 * 
      //     quarks[0].number_of_dilution_E)
      // thus the multiplication in this order is fastest

      // TODO: Make X and Y dependent on p and dirac -> SPEEDUP
      // X = D_d^-1(t_sink | t_source + 1) 
      //     Gamma D_u^-1(t_source + 1 | t_sink + 1) Gamma
      #pragma omp parallel for collapse(2) schedule(dynamic)
      for(size_t p_so = 0; p_so < nmom; p_so++){
      for(size_t p_si = 0; p_si < nmom; p_si++){
        for(size_t d_so = 0; d_so < ndir; d_so++){
        for(size_t d_si = 0; d_si < ndir; d_si++){
          for(size_t rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
          for(size_t rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){
          if(rnd2 != rnd1){
          for(size_t rnd3 = 0; rnd3 < number_of_rnd_vec; ++rnd3){
          if((rnd3 != rnd1) && (rnd3 != rnd2)){

            X[p_so][p_si][d_so][d_si][rnd1][rnd2][rnd3] = 
                    basic->get_operator_g5(1, t_sink, d_si, p_si, rnd1) * 
                    basic->get_operator_charged(1, t_sink_1, d_so, p_so, rnd2, rnd3);
            Y[p_so][p_si][d_so][d_si][rnd1][rnd2][rnd3] = 
                    basic->get_operator_g5(0, t_sink_1, d_si, p_si, rnd1) * 
                    basic->get_operator_charged(0, t_sink, d_so, p_so, rnd2, rnd3);

          }}}}}// loops random vectors
        }}// loops dirac indices
      }}// loops momenta

      for(size_t dirac_1 = 0; dirac_1 < number_of_dirac; ++dirac_1){     
        for(size_t p = 0; p <= max_mom_squared; p++){
        for(size_t p_u = 0; p_u < nmom; ++p_u) {
        if(mom_squared[p_u] == p){
            for(size_t dirac_2 = 0; dirac_2 < number_of_dirac; ++dirac_2){
            for(size_t p_d = 0; p_d < nmom; ++p_d) {
            if(mom_squared[p_d] == p){
              // complete diagramm. combine X and Y to four-trace
              // C4_mes = tr(D_u^-1(t_source     | t_sink      ) Gamma 
              //             D_d^-1(t_sink       | t_source + 1) Gamma 
              //             D_u^-1(t_source + 1 | t_sink + 1  ) Gamma 
              //             D_d^-1(t_sink + 1   | t_source    ) Gamma)
              #pragma omp parallel shared(C4_mes)
              {
                cmplx priv_C4(0.0,0.0);
                #pragma omp for collapse(2) schedule(dynamic)
                for(size_t rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
                for(size_t rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){      
                if(rnd2 != rnd1){
                for(size_t rnd3 = 0; rnd3 < number_of_rnd_vec; ++rnd3){
                if((rnd3 != rnd2) && (rnd3 != rnd1)){
                for(size_t rnd4 = 0; rnd4 < number_of_rnd_vec; ++rnd4){
                if((rnd4 != rnd1) && (rnd4 != rnd2) && (rnd4 != rnd3)){

                    priv_C4 += (X[p_d][p_u][dirac_1][dirac_2][rnd3][rnd2][rnd4] * 
                                Y[nmom - p_d - 1][nmom - p_u - 1][dirac_1][dirac_2][rnd4][rnd1][rnd3]).trace();

                }}}}}}}
                #pragma omp critical
                {
                  C4_mes[p][p][dirac_1][dirac_2]
                      [abs((t_sink - t_source) - Lt) % Lt] += priv_C4;
                }
              }
            }}// loop and if condition p_d
          }// loop dirac_2
        }}}// loop and if conditions p_u
      }// loop dirac_1
    }}// Loops over time end here

    // *************************************************************************
    // FOUR PT CONTRACTION 3 ***************************************************
    // *************************************************************************
    // Normalization of 4pt-function
    for(auto i = C4_mes.data(); i < (C4_mes.data()+C4_mes.num_elements()); i++)
      *i /= norm1;
    // output to binary file
    for(size_t dirac_1 = 0; dirac_1 < number_of_dirac; ++dirac_1){     
    for(size_t dirac_2 = 0; dirac_2 < number_of_dirac; ++dirac_2){
      for(size_t p = 0; p <= max_mom_squared; p++){
        sprintf(outfile, 
            "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d/"
            "C4_3_conf%04d.dat", 
            outpath.c_str(), dirac_ind.at(dirac_1), dirac_ind.at(dirac_2), 
            p, p, 0, 0, config_i);
        if((fp = fopen(outfile, "wb")) == NULL)
          std::cout << "fail to open outputfile" << std::endl;
        fwrite((double*) &(C4_mes[p][p][dirac_1][dirac_2][0]), 
               sizeof(double), 2 * Lt, fp);
        fclose(fp);
      }// loop p
    }}// loops dirac_1 dirac_2

    ////////////////////////////////////////////////////////////////////////////
    //                          TWO POINT FUNCTION                            //
    ////////////////////////////////////////////////////////////////////////////
    // build 2pt-function C2_mes for pi^+ from Corr. Equivalent to just summing
    // up traces with same time difference between source and sink (all to all)
    // for every dirac structure, momentum, displacement
    std::cout << "\tcomputing the pi_+/-\n" << std::endl;
    // setting the correlation function to zero
    array_cd_d6 C2_mes(boost::extents[nmom][ndir][ndir][ndis][ndis][Lt]);
    std::fill(C2_mes.origin(), C2_mes.origin() + C2_mes.num_elements() , 
                                                              cmplx(0.0, 0.0));
    for(int t_source = 0; t_source < Lt; ++t_source){
    for(int t_sink = 0; t_sink < Lt; ++t_sink){
      for(int p2 = 0; p2 <= max_mom_squared; p2++){
      for(int p = p_min; p < p_max; ++p){
      if(mom_squared[p] == p2){
        for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){
        for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
          for(int displ_u = 0; displ_u < number_of_displ; displ_u++){
          for(int displ_d = 0; displ_d < number_of_displ; displ_d++){            
            for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
            for(int rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){
            if(rnd1 != rnd2){
              C2_mes[p2][dirac_u][dirac_d][displ_u][displ_d]
                    [abs((t_sink - t_source - Lt) % Lt)] += 
                 Corr[p][number_of_momenta - p - 1][dirac_u][dirac_d]
                     [displ_u][displ_d][t_source][t_sink][rnd1][rnd2];
            }}}
          }}
        }}
      }}}
    }}
    // normalization of correlation function
    double norm3 = Lt * number_of_rnd_vec * (number_of_rnd_vec - 1);
    for(auto i = C2_mes.data(); i < (C2_mes.data()+C2_mes.num_elements()); i++)
      *i /= norm3;

    // output to binary file - only diagaonal and summed momenta
    for(int dirac = 0; dirac < number_of_dirac; ++dirac){
      for(int p = 0; p <= max_mom_squared; p++){
        for(int displ = 0; displ < number_of_displ; ++displ){
          sprintf(outfile, 
              "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d/"
              "C2_pi+-_conf%04d.dat", 
              outpath.c_str(), dirac_ind.at(dirac), dirac_ind.at(dirac), p, p, 
              displ_min, displ_max, config_i);
          if((fp = fopen(outfile, "wb")) == NULL)
            std::cout << "fail to open outputfile: " << outfile << std::endl;

          fwrite((double*) &(C2_mes[p][dirac][dirac][displ][displ][0]), 
                                                  sizeof(double), 2 * Lt, fp);
          fclose(fp);
        }
      }
    }
    time = clock() - time;
    std::cout << "\t\tSUCCESS - " << ((float) time)/CLOCKS_PER_SEC 
              << " seconds" << std::endl;


    ////////////////////////////////////////////////////////////////////////////
    //                         FOUR POINT FUNCTION                            //
    ////////////////////////////////////////////////////////////////////////////
    // *************************************************************************
    // FOUR PT CONTRACTION 1 ***************************************************
    // *************************************************************************
    std::cout << "\n\tcomputing the connected contribution of C4_1:\n";
    time = clock();
    // displacement not supported for 4pt functions atm
    displ_min = 0;
    displ_max = 0;
    // setting the correlation function to zero
    std::fill(C4_mes.data(), C4_mes.data() + C4_mes.num_elements(), 
                                                              cmplx(0.0, 0.0));
    for(int t_source = 0; t_source < Lt; ++t_source){
    for(int t_sink = 0; t_sink < Lt; ++t_sink){
      int t_source_1 = (t_source + 1) % Lt;
      int t_sink_1 = (t_sink + 1) % Lt;
      for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){     
      for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
        for(int p_u = p_min; p_u < p_max; ++p_u) {
        for(int p_d = p_min; p_d < p_max; ++p_d) {
        if(mom_squared[p_u] <= mom_squared[p_d]){
          for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
          for(int rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){      
          if(rnd2 != rnd1){
            for(int rnd3 = 0; rnd3 < number_of_rnd_vec; ++rnd3){
            if((rnd3 != rnd2) && (rnd3 != rnd1)){
              for(int rnd4 = 0; rnd4 < number_of_rnd_vec; ++rnd4){
              if((rnd4 != rnd1) && (rnd4 != rnd2) && (rnd4 != rnd3)){
                C4_mes[p_u][p_d][dirac_u][dirac_d]
                      [abs((t_sink - t_source - Lt) % Lt)] +=
                  (Corr[p_u][number_of_momenta - p_d - 1][dirac_u][dirac_d]
                       [0][0][t_source_1][t_sink_1][rnd1][rnd3]) *
                  (Corr[number_of_momenta - p_u - 1][p_d][dirac_u][dirac_d]
                       [0][0][t_source][t_sink][rnd2][rnd4]);
              }}// loop rnd4 and if
            }}// loop rnd3 and if
          }}}// loops rnd1 and rnd 2 and if
        }}}// loops momenta and if
      }}// loops dirac
    }}// loops t_sink and t_source
    // Normalization of 4pt-function
    for(auto i = C4_mes.data(); i < (C4_mes.data()+C4_mes.num_elements()); i++)
      *i /= norm1;

    // output to binary file
    // see output to binary file for C2. 
    // write into folders with suffix "_unsuppressed". These only include
    // correlators of the diagonal matrix elements of the GEVP for which
    // the three-momentum remains unchanged for both quarks. Because the
    // quarks have to be back-to-back, for the offdiagonal elements this
    // cannot occur. The suppression can be interpreted as Zweig-suppressed
    // gluon exchange
    for(int dirac = 0; dirac < number_of_dirac; ++dirac){
      for(int p = 0; p <= max_mom_squared; p++){

        sprintf(outfile, 
            "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d_unsuppressed/"
            "C4_1_conf%04d.dat", 
            outpath.c_str(), dirac_ind.at(dirac), dirac_ind.at(dirac), p, p, 
            displ_min, displ_max, config_i);
        if((fp = fopen(outfile, "wb")) == NULL)
          std::cout << "fail to open outputfile" << std::endl;

        for(int p_u = p_min; p_u < p_max; ++p_u){
          if(mom_squared[p_u] == p){

            fwrite((double*) &(C4_mes[p_u][p_u][dirac][dirac][0]), 
                sizeof(double), 2 * Lt, fp);
          }
        }

        fclose(fp);

      }
    }

    // to build a GEVP, the correlators are written into a seperate folder
    // for every dirac structure, momentum, (entry of the GEVP matrix).
    // displacement is not supported at the moment
    for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){
      for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
        for(int p1 = 0; p1 <= max_mom_squared; p1++){
          for(int p2 = p1; p2 <= max_mom_squared; p2++){

            sprintf(outfile, 
               "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d/"
               "C4_1_conf%04d.dat", 
               outpath.c_str(), dirac_ind.at(dirac_u), dirac_ind.at(dirac_d), 
               p1, p2, displ_min, displ_max, config_i);
           if((fp = fopen(outfile, "wb")) == NULL)
             std::cout << "fail to open outputfile" << std::endl;

           for(int p_u = p_min; p_u < p_max; ++p_u){
              if(mom_squared[p_u] == p1){
                for(int p_d = p_min; p_d < p_max; ++p_d){
                  if(mom_squared[p_d] == p2){

                    fwrite((double*) &(C4_mes[p_u][p_d][dirac_u][dirac_d][0]), 
                        sizeof(double), 2 * Lt, fp);
                  }
                }
              }
            }

            fclose(fp);

          }
        }
      }
    }

    time = clock() - time;
    printf("\t\tSUCCESS - %.1f seconds\n", ((float) time)/CLOCKS_PER_SEC);


    // *************************************************************************
    // FOUR PT CONTRACTION 2 ***************************************************
    // *************************************************************************
    std::cout << "\n\tcomputing the connected contribution of C4_2:\n";
    time = clock();
    // setting the correlation function to zero
    std::fill(C4_mes.data(), C4_mes.data() + C4_mes.num_elements(), 
                                                               cmplx(0.0, 0.0));
    for(int t_source = 0; t_source < Lt; ++t_source){
    for(int t_sink = 0; t_sink < Lt - 1; ++t_sink){
      int t_source_1 = (t_source + 1) % Lt;
      int t_sink_1 = (t_sink + 1) % Lt;
      for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){     
      for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
        for(int p_u = p_min; p_u < p_max; ++p_u) {
        for(int p_d = p_min; p_d < p_max; ++p_d) {
        if(mom_squared[p_u] <= mom_squared[p_d]){
          for(int rnd1 = 0; rnd1 < number_of_rnd_vec; ++rnd1){
          for(int rnd2 = 0; rnd2 < number_of_rnd_vec; ++rnd2){      
          if(rnd2 != rnd1){
            for(int rnd3 = 0; rnd3 < number_of_rnd_vec; ++rnd3){
            if((rnd3 != rnd2) && (rnd3 != rnd1)){
              for(int rnd4 = 0; rnd4 < number_of_rnd_vec; ++rnd4){
              if((rnd4 != rnd1) && (rnd4 != rnd2) && (rnd4 != rnd3)){
                C4_mes[p_u][p_d][dirac_u][dirac_d]
                      [abs((t_sink - t_source - Lt) % Lt)] +=
                  (Corr[p_u][number_of_momenta - p_d - 1][dirac_u][dirac_d]
                       [0][0][t_source_1][t_sink][rnd1][rnd3]) *
                  (Corr[number_of_momenta - p_u - 1][p_d][dirac_u][dirac_d]
                       [0][0][t_source][t_sink_1][rnd2][rnd4]);
              }}// loop rnd4
            }}// loop rnd3
          }}}// loops rnd2 and rnd1
        }}}// loops momenta
      }}// loops dirac
    }}// loops t_source and t_sink
    // Normalization of 4pt-function. 
    for(auto i = C4_mes.data(); i < (C4_mes.data()+C4_mes.num_elements()); i++)
      *i /= norm1;

    // output to binary file
    // see output to binary file for C2. 
    // write into folders with suffix "_unsuppressed". These only include
    // correlators of the diagonal matrix elements of the GEVP for which
    // the three-momentum remains unchanged for both quarks. Because the
    // quarks have to be back-to-back, for the offdiagonal elements this
    // cannot occur. The suppression can be interpreted as Zweig-suppressed
    // gluon exchange
    for(int dirac = 0; dirac < number_of_dirac; ++dirac){
      for(int p = 0; p <= max_mom_squared; p++){
        sprintf(outfile, 
            "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d_unsuppressed/"
            "C4_2_conf%04d.dat", 
            outpath.c_str(), dirac_ind.at(dirac), dirac_ind.at(dirac), p, p, 
            displ_min, displ_max, config_i);
        if((fp = fopen(outfile, "wb")) == NULL)
          std::cout << "fail to open outputfile" << std::endl;

        for(int p_u = p_min; p_u < p_max; ++p_u){
          if(mom_squared[p_u] == p){

            fwrite((double*) &(C4_mes[p_u][p_u][dirac][dirac][0]), 
                sizeof(double), 2 * Lt, fp);
          }
        }

        fclose(fp);

      }
    }

    // to build a GEVP, the correlators are written into a seperate folder
    // for every dirac structure, momentum, (entry of the GEVP matrix).
    // displacement is not supported at the moment

    for(int dirac_u = 0; dirac_u < number_of_dirac; ++dirac_u){
      for(int dirac_d = 0; dirac_d < number_of_dirac; ++dirac_d){
        for(int p1 = 0; p1 <= max_mom_squared; p1++){
          for(int p2 = p1; p2 <= max_mom_squared; p2++){

            sprintf(outfile, 
               "%s/dirac_%02d_%02d_p_%01d_%01d_displ_%01d_%01d/"
               "C4_2_conf%04d.dat", 
               outpath.c_str(), dirac_ind.at(dirac_u), dirac_ind.at(dirac_d), 
               p1, p2, displ_min, displ_max, config_i);
           if((fp = fopen(outfile, "wb")) == NULL)
             std::cout << "fail to open outputfile" << std::endl;

           for(int p_u = p_min; p_u < p_max; ++p_u){
              if(mom_squared[p_u] == p1){
                for(int p_d = p_min; p_d < p_max; ++p_d){
                  if(mom_squared[p_d] == p2){

                    fwrite((double*) &(C4_mes[p_u][p_d][dirac_u][dirac_d][0]), 
                        sizeof(double), 2 * Lt, fp);
                  }
                }
              }
            }

            fclose(fp);

          }
        }
      }
    }

    time = clock() - time;
    printf("\t\tSUCCESS - %.1f seconds\n", ((float) time)/CLOCKS_PER_SEC);

  } // loop over configs ends here

  delete basic;
}

