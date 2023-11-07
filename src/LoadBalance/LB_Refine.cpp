#include "GAMER.h"

#ifdef LOAD_BALANCE



void LB_Refine_GetNewRealPatchList( const int FaLv, int &NNew_Home, int *&NewPID_Home, int &NNew_Away,
                                    ulong *&NewCr1D_Away, int *&NewCr1D_Away_IdxTable, real *&NewCData_Away,
                                    int &NDel_Home, int *&DelPID_Home, int &NDel_Away, ulong *&DelCr1D_Away,
                                    int &RefineF2S_Send_NPatchTotal, int *&RefineF2S_Send_PIDList,
                                    long (*&CFB_SibLBIdx_Home)[6], long (*&CFB_SibLBIdx_Away)[6],
                                    bool &convertLevelsToWaveScheme );
void LB_Refine_AllocateNewPatch( const int FaLv, int NNew_Home, int *NewPID_Home, int NNew_Away,
                                 const ulong *NewCr1D_Away, const int *NewCr1D_Away_IdxTable, real *NewCData_Away,
                                 int NDel_Home, int *DelPID_Home, int NDel_Away, ulong *DelCr1D_Away,
                                 int &RefineS2F_Send_NPatchTotal, int *&RefineS2F_Send_PIDList,
                                 const int (*CFB_SibRank_Home)[6], const int (*CFB_SibRank_Away)[6],
                                 const real *CFB_BField, const int *CFB_NSibEachRank );
#ifdef PARTICLE
void Par_LB_Refine_SendParticle2Father( const int FaLv, const int RefineS2F_Send_NPatchTotal, int *RefineS2F_Send_PIDList );
#endif
#ifdef MHD
void MHD_LB_Refine_GetCoarseFineInterfaceBField(
   const int FaLv, const int NNew_Home, const int NNew_Away,
   const long (*CFB_SibLBIdx_Home)[6], const long (*CFB_SibLBIdx_Away)[6],
   int (*&CFB_SibRank_Home)[6], int (*&CFB_SibRank_Away)[6],
   real *&CFB_BField, int *CFB_NSibEachRank );
#endif




//-------------------------------------------------------------------------------------------------------
// Function    :  LB_Refine
// Description :  Construct patches at FaLv+1 according to the flagging results at level FaLv
//
// Note        :  1. This function will also construct buffer patches at both FaLv and FaLv+1
//                2. Data of all sibling-buffer patches must be prepared in advance for creating new
//                   patches at FaLv+1 by spatial interpolation
//
// Parameter   :  FaLv : Target refinement level to be refined
//-------------------------------------------------------------------------------------------------------
void LB_Refine( const int FaLv )
{

   const int SonLv = FaLv + 1;

// check
   if ( FaLv == NLEVEL-1 )
   {
      Aux_Message( stderr, "WARNING : function <%s> should NOT be applied to the finest level !!\n",
                   __FUNCTION__ );
      return;
   }

// check the synchronization
   if ( NPatchTotal[SonLv] != 0 )   Mis_CompareRealValue( Time[FaLv], Time[SonLv], __FUNCTION__, true );


// 0. initialize variables for exchanging particles
// ==========================================================================================
   int  RefineS2F_Send_NPatchTotal = 0;
   int *RefineS2F_Send_PIDList     = NULL;
   int  RefineF2S_Send_NPatchTotal = 0;
   int *RefineF2S_Send_PIDList     = NULL;

#  ifdef PARTICLE
   RefineS2F_Send_PIDList = new int  [ amr->NPatchComma[FaLv][3] - amr->NPatchComma[FaLv][1] ];
   RefineF2S_Send_PIDList = new int  [ amr->NPatchComma[FaLv][1] ];
#  endif


// 1. construct LB_CutPoint for the newly-created level
// ==========================================================================================
//###NOTE : here we have assumed that the newly-created son patches will have LB_Idx ~ 8*(father LB_Idx)
//          --> hold for Hilbert curve method, but may not hold for other methods
   if ( NPatchTotal[SonLv] == 0 )
      for (int r=0; r<MPI_NRank+1; r++)   amr->LB->CutPoint[SonLv][r] = amr->LB->CutPoint[FaLv][r]*8;


// 2. get the unsorted lists of father patches at FaLv to allocate/deallocate son patches at FaLv+1
// ==========================================================================================
   int    NNew_Home, NDel_Home, NNew_Away, NDel_Away;  // Home/Away : for target patches at home/not at home
   int   *NewPID_Home=NULL, *DelPID_Home=NULL;
   ulong *NewCr1D_Away=NULL, *DelCr1D_Away=NULL;
   int   *NewCr1D_Away_IdxTable=NULL;
   real  *NewCData_Away=NULL;

// CFB = Coarse-Fine interface B field (for MHD only)
   int  (*CFB_SibRank_Home)[6]=NULL, (*CFB_SibRank_Away)[6]=NULL;
   long (*CFB_SibLBIdx_Home)[6]=NULL, (*CFB_SibLBIdx_Away)[6]=NULL;
   int    CFB_NSibEachRank[MPI_NRank];
   real  *CFB_BField=NULL;

   bool SwitchFinerLevelsToWaveScheme = false;

   LB_Refine_GetNewRealPatchList( FaLv, NNew_Home, NewPID_Home, NNew_Away, NewCr1D_Away, NewCr1D_Away_IdxTable, NewCData_Away,
                                  NDel_Home, DelPID_Home, NDel_Away, DelCr1D_Away,
                                  RefineF2S_Send_NPatchTotal, RefineF2S_Send_PIDList,
                                  CFB_SibLBIdx_Home, CFB_SibLBIdx_Away,
                                  SwitchFinerLevelsToWaveScheme );

# if ( ELBDM_SCHEME == ELBDM_HYBRID )
// sync information whether refined levels are switched to wave scheme
   bool Send = SwitchFinerLevelsToWaveScheme;
   bool Recv;

   MPI_Allreduce(&Send, &Recv, 1, MPI_C_BOOL, MPI_LOR, MPI_COMM_WORLD);

   SwitchFinerLevelsToWaveScheme = Recv;
# endif // # if ( ELBDM_SCHEME == ELBDM_HYBRID )

// 3. get the magnetic field on the coarse-fine interfaces for the divergence-free interpolation
// ==========================================================================================
#  ifdef MHD
   MHD_LB_Refine_GetCoarseFineInterfaceBField( FaLv, NNew_Home, NNew_Away, CFB_SibLBIdx_Home, CFB_SibLBIdx_Away,
                                               CFB_SibRank_Home, CFB_SibRank_Away, CFB_BField, CFB_NSibEachRank );
#  endif


// 4. allocate/deallocate son patches at FaLv+1
// ==========================================================================================
   LB_Refine_AllocateNewPatch( FaLv, NNew_Home, NewPID_Home, NNew_Away, NewCr1D_Away, NewCr1D_Away_IdxTable, NewCData_Away,
                               NDel_Home, DelPID_Home, NDel_Away, DelCr1D_Away,
                               RefineS2F_Send_NPatchTotal, RefineS2F_Send_PIDList,
                               CFB_SibRank_Home, CFB_SibRank_Away, CFB_BField, CFB_NSibEachRank );


// 5. construct the MPI send and recv data list
// ==========================================================================================
// 5.1 list for exchanging hydro, potential, and magnetic field data in buffer patches (also construct the "SibDiff" lists)
   LB_RecordExchangeDataPatchID(  FaLv, true );
   LB_RecordExchangeDataPatchID( SonLv, true );

// 5.2 list for exchanging restricted hydro and magnetic field data
//     --> note that even when OPT__FIXUP_RESTRICT is off we still need to do data restriction in several places
//         (e.g., restart, and OPT__CORR_AFTER_ALL_SYNC)
//     --> for simplicity and sustainability, we always invoke LB_RecordExchangeRestrictDataPatchID()
   LB_RecordExchangeRestrictDataPatchID(  FaLv );
   LB_RecordExchangeRestrictDataPatchID( SonLv );

// 5.3 list for exchanging hydro fluxes (and also allocate flux arrays)
   if ( amr->WithFlux )
   {
      LB_AllocateFluxArray(  FaLv );
      LB_AllocateFluxArray( SonLv );
   }

// 5.4 list for exchanging MHD electric field (and also allocate electric field arrays)
#  ifdef MHD
   if ( amr->WithElectric )
   {
      MHD_LB_AllocateElectricArray(  FaLv );
      MHD_LB_AllocateElectricArray( SonLv );
   }
#  endif

// 5.5 list for exchanging hydro and magnetic field data after the fix-up operation
//     --> for simplicity and sustainability, we always invoke LB_RecordExchangeFixUpDataPatchID()
//     --> see the comments 4.2 above
   LB_RecordExchangeFixUpDataPatchID(  FaLv );
   LB_RecordExchangeFixUpDataPatchID( SonLv );

// 5.6 list for overlapping MPI time with CPU/GPU computation
   if ( OPT__OVERLAP_MPI )
   {
      LB_RecordOverlapMPIPatchID(  FaLv );
      LB_RecordOverlapMPIPatchID( SonLv );
   }

// 5.7 list for exchanging particles
#  ifdef PARTICLE
   Par_LB_RecordExchangeParticlePatchID( SonLv );

   if ( SonLv < MAX_LEVEL )
   Par_LB_RecordExchangeParticlePatchID( SonLv+1 );

// only for reconstructing the amr->Par->B2R_Real/Buffer_NPatchTotal[FaLv][0] lists
   if ( FaLv >= 0 )
   Par_LB_RecordExchangeParticlePatchID( FaLv );
#  endif


// 6. send particles to leaf real patches
// ==========================================================================================
#  ifdef PARTICLE
// 6.1 send particles from buffer patches at FaLv to their corresponding real patches
   Par_LB_Refine_SendParticle2Father( FaLv, RefineS2F_Send_NPatchTotal, RefineS2F_Send_PIDList );

// 6.2 send particles from real patches at FaLv to their real son patches living abroad
   const bool TimingSendPar_No = false;
   Par_PassParticle2Son_MultiPatch( FaLv, PAR_PASS2SON_GENERAL, TimingSendPar_No,
                                    RefineF2S_Send_NPatchTotal, RefineF2S_Send_PIDList );
#  endif



// (c1.3.6) convert density/phase to density/real part/imaginary part in hybrid scheme if we switch the level from Phase to wave
#  if ( ELBDM_SCHEME == ELBDM_HYBRID )
   if ( SwitchFinerLevelsToWaveScheme ) {
      for (int ChildLv = SonLv; ChildLv <= TOP_LEVEL; ++ChildLv) {
//       set use_wave_flag
         amr->use_wave_flag[ChildLv] = true;

//       iterate over real and buffer patches
         for (int PID=0; PID < amr->NPatchComma[ChildLv][27]; PID++)
         {
            for (int k=0; k<PS1; k++)  {
            for (int j=0; j<PS1; j++)  {
            for (int i=0; i<PS1; i++)  {
//             convert both sandglasses
               for ( int FluSg = 0; FluSg < 2; ++FluSg )
               {
//                check fluid != NULL for buffer patches
                  if ( amr->patch[FluSg][ChildLv][PID]->fluid != NULL && amr->FluSgTime[ChildLv][FluSg] >= 0.0 )
                  {
//                   check whether dB wavelength is resolved after conversion to wave patch
#                    ifdef GAMER_DEBUG
                     int kk  =  k;
                     int kkp = (kk + 1) < PS1  ? kk + 1 : kk    ;
                     int kkm = (kk - 1) < 0    ? kk     : kk - 1;
                     int ii  =  i;
                     int iip = (ii + 1) < PS1  ? ii + 1 : ii    ;
                     int iim = (ii - 1) < 0    ? ii     : ii - 1;
                     int jj  =  j;
                     int jjp = (jj + 1) < PS1  ? jj + 1 : jj    ;
                     int jjm = (jj - 1) < 0    ? jj     : jj - 1;

                     //check whether dB wavelength is resolved within the newly converted patch
                     real dPhase = MAX(MAX(MAX(MAX(MAX(
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][iip] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ]),
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][iim])),
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jjp][ii ] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ])),
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jjm][ii ])),
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kkp][jj ][ii ] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ])),
                     FABS(amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kk ][jj ][ii ] - amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][kkm][jj ][ii ]));

//                   currently, the selection of the first wave level is fixed as a runtime parameter
//                   ideally, the code should be able to adaptively increase the first wave level by 1 when this happens
                     if ( dPhase > M_PI ) {
                        Aux_Message ( stderr, "WARNING: Phase jump = %13.7e > PI when converting patch from fluid to wave scheme on lv %d!", dPhase, ChildLv);
                     }
#                    endif // # ifdef GAMER_DEBUG

                     const real Amp   = SQRT(amr->patch[FluSg][ChildLv][PID]->fluid[DENS][k][j][i]);
                     const real Phase = amr->patch[FluSg][ChildLv][PID]->fluid[PHAS][k][j][i];
                     amr->patch[FluSg][ChildLv][PID]->fluid[REAL][k][j][i] = Amp * COS(Phase);
                     amr->patch[FluSg][ChildLv][PID]->fluid[IMAG][k][j][i] = Amp * SIN(Phase);
                  }
               } // FluSg
            }}} // k,j,i
         } // for (int PID=0; PID < amr->NPatchComma[ChildLv][27]; PID++)
      } // for (int ChildLv = SonLv; ChildLv <= TOP_LEVEL; ++ChildLv)
   } // if ( SwitchFinerLevelsToWaveScheme )
#   endif // #if ( MODEL == ELBDM && ELBDM_SCHEME == ELBDM_HYBRID)

// 7. miscellaneous
// ==========================================================================================
// 7.1 reset LB_CutPoint to the default values (-1) if SonLv has been totally removed
   if ( NPatchTotal[SonLv] == 0 )
      for (int r=0; r<MPI_NRank+1; r++)   amr->LB->CutPoint[SonLv][r] = -1;

// 7.2 free memory
   if ( NewPID_Home == NULL  &&  NNew_Home != 0 )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "NewPID_Home"   );

   if ( DelPID_Home == NULL  &&  NDel_Home != 0 )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "DelPID_Home"   );

   if ( NewCr1D_Away == NULL )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "NewCr1D_Away" );

   if ( NewCr1D_Away_IdxTable == NULL )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "NewCr1D_Away_IdxTable" );

   if ( NewCData_Away == NULL )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "NewCData_Away" );

   if ( DelCr1D_Away == NULL )
      Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "DelCr1D_Away" );

#  ifdef MHD
   if ( NNew_Home != 0 )
   {
      if ( CFB_SibRank_Home == NULL )
         Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "CFB_SibRank_Home"   );

      if ( CFB_SibLBIdx_Home == NULL )
         Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "CFB_SibLBIdx_Home"   );
   }

   if ( NNew_Away != 0 )
   {
      if ( CFB_SibRank_Away == NULL )
         Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "CFB_SibRank_Away"   );

      if ( CFB_SibLBIdx_Away == NULL )
         Aux_Error( ERROR_INFO, "%s has not been allocated !!\n", "CFB_SibLBIdx_Away"   );
   }
#  endif // #ifdef MHD

   free( NewPID_Home );
   free( DelPID_Home );
   delete [] NewCr1D_Away;
   delete [] NewCr1D_Away_IdxTable;
   delete [] NewCData_Away;
   delete [] DelCr1D_Away;
#  ifdef MHD
   free( CFB_SibLBIdx_Home );
   delete [] CFB_SibLBIdx_Away;
   delete [] CFB_SibRank_Home;
   delete [] CFB_SibRank_Away;
   delete [] CFB_BField;
#  endif

#  ifdef PARTICLE
   delete [] RefineS2F_Send_PIDList;
   delete [] RefineF2S_Send_PIDList;
#  endif

} // FUNCTION : LB_Refine



#endif // #ifdef LOAD_BALANCE
