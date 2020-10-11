// EnergyPlus, Copyright (c) 1996-2020, The Board of Trustees of the University of Illinois,
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy), Oak Ridge
// National Laboratory, managed by UT-Battelle, Alliance for Sustainable Energy, LLC, and other
// contributors. All rights reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// EnergyPlus Headers
#include <EnergyPlus/DataHeatBalSurface.hh>
namespace EnergyPlus {

namespace DataHeatBalSurface {

    // MODULE INFORMATION:
    //       AUTHOR         Rick Strand
    //       DATE WRITTEN   December 2000

    // PURPOSE OF THIS MODULE:
    // The purpose of this module is to contain data needed for the surface
    // heat balances which are now "external" subroutines.

    // Using/Aliasing
    // Data
    // MODULE PARAMETER DEFINITIONS
    Real64 const MinSurfaceTempLimit(-100.0);            // Lowest inside surface temperature allowed in Celsius
    Real64 const MinSurfaceTempLimitBeforeFatal(-250.0); // 2.5 times MinSurfaceTempLimit
    Real64 const DefaultSurfaceTempLimit(200.0);         // Highest inside surface temperature allowed in Celsius
    std::vector<bool> Zone_has_mixed_HT_models;          // True if any surfaces in zone use CondFD, HAMT, or Kiva

    // Integer Variables for the Heat Balance Simulation
    Array1D_int SUMH; // From Old Bldctf.inc

    // Surface heat balance limits and convergence parameters
    Real64 MaxSurfaceTempLimit(200.0);            // Highest inside surface temperature allowed in Celsius
    Real64 MaxSurfaceTempLimitBeforeFatal(500.0); // 2.5 times MaxSurfaceTempLimit
    Real64 const IterDampConst(5.0);              // Damping constant for inside surface temperature iterations
    int const ItersReevalConvCoeff(30);           // Number of iterations between inside convection coefficient reevaluations
    int MinIterations(1);                         // Minimum number of iterations for surface heat balance
    int const MaxIterations(500);                 // Maximum number of iterations allowed for inside surface temps
    Real64 const PoolIsOperatingLimit(0.0001);    // Limit to determine if swimming pool is operating or not
    int const MinEMPDIterations(4);               // Minimum number of iterations required for EMPD solution
    int const IterationsForCondFDRelaxChange(5);  // number of iterations for inside temps that triggers a change

    // Variables Dimensioned to Max Number of Heat Transfer Surfaces (maxhts)
    Array1D<Real64> CTFConstInPart;               // Constant Inside Portion of the CTF calculation
    Array1D<Real64> CTFConstOutPart;              // Constant Outside Portion of the CTF calculation
    // This group of arrays (soon to be vectors) added to facilitate vectorizable loops in CalcHeatBalanceInsideSurf2CTFOnly
    Array1D<Real64> CTFCross0;                    // Construct.CTFCross(0)
    Array1D<Real64> CTFInside0;                   // Construct.CTFInside(0)
    Array1D<Real64> CTFSourceIn0;                 // Construct.CTFSourceIn(0)
    Array1D<Real64> TH11Surf;                     // TH(1,1,SurfNum)
    Array1D<Real64> QsrcHistSurf1;                // QsrcHist(SurfNum, 1)
    Array1D_int IsAdiabatic;                      // 0 not adiabatic, 1 is adiabatic
    Array1D_int IsNotAdiabatic;                   // 1 not adiabatic, 0 is adiabatic
    Array1D_int IsSource;                         // 0 no internal source/sink, 1 has internal source/sing
    Array1D_int IsNotSource;                      // 1 no internal source/sink, 0 has internal source/sing
    Array1D_int IsPoolSurf;                       // 0 not pool, 1 is pool
    Array1D_int IsNotPoolSurf;                    // 1 not pool, 0 is pool
    Array1D<Real64> TempTermSurf;                 // TempTerm for heatbalance equation
    Array1D<Real64> TempDivSurf;                  // Divisor for heatbalance equation
    // end group added to support CalcHeatBalanceInsideSurf2CTFOnly
    Array1D<Real64> TempSurfIn;                   // Temperature of the Inside Surface for each heat transfer surface
    Array1D<Real64> TempInsOld;                   // TempSurfIn from previous iteration for convergence check
    Array1D<Real64> TempSurfInTmp;                // Inside Surface Temperature Of Each Heat Transfer Surface
    Array1D<Real64> HcExtSurf;                    // Outside Convection Coefficient
    Array1D<Real64> HAirExtSurf;                  // Outside Convection Coefficient to Air
    Array1D<Real64> HSkyExtSurf;                  // Outside Convection Coefficient to Sky
    Array1D<Real64> HGrdExtSurf;                  // Outside Convection Coefficient to Ground
    Array1D<Real64> TempSource;                   // Temperature at the source location for each heat transfer surface
    Array1D<Real64> TempUserLoc;                  // Temperature at the user specified location for each heat transfer surface
    Array1D<Real64> TempSurfInRep;                // Temperature of the Inside Surface for each heat transfer surface
    Array1D<Real64> TempSurfInMovInsRep;          // Temperature of interior movable insulation on the side facing the zone
    // (report)
    Array1D<Real64> QConvInReport; // Surface convection heat gain at inside face [J]
    Array1D<Real64> QdotConvInRep; // Surface convection heat transfer rate at inside face surface [W]
    // (report)
    Array1D<Real64> QdotConvInRepPerArea; // Surface conv heat transfer rate per m2 at inside face surf
    //  (report){w/m2]

    // these next three all are for net IR thermal radiation exchange with other surfaces in the model.
    Array1D<Real64> QRadNetSurfInReport;        // Surface thermal radiation heat gain at Inside face [J]
    Array1D<Real64> QdotRadNetSurfInRep;        // Surface thermal radiation heat transfer inside face surface [W]
    Array1D<Real64> QdotRadNetSurfInRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Inside face surf
    // these next three all are for solar radiation gains on inside face
    Array1D<Real64> QRadSolarInReport;        // Surface thermal radiation heat gain at Inside face [J]
    Array1D<Real64> QdotRadSolarInRep;        // Surface thermal radiation heat transfer inside face surface [W]
    Array1D<Real64> QdotRadSolarInRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Inside face surf
    // these next three all are for Lights visible radiation gains on inside face
    Array1D<Real64> QRadLightsInReport;        // Surface thermal radiation heat gain at Inside face [J]
    Array1D<Real64> QdotRadLightsInRep;        // Surface thermal radiation heat transfer inside face surface [W]
    Array1D<Real64> QdotRadLightsInRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Inside face surf
    // these next three all are for Internal Gains sources of radiation gains on inside face
    Array1D<Real64> QRadIntGainsInReport;        // Surface thermal radiation heat gain at Inside face [J]
    Array1D<Real64> QdotRadIntGainsInRep;        // Surface thermal radiation heat transfer inside face surface [W]
    Array1D<Real64> QdotRadIntGainsInRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Inside face surf
    // these next three all are for Radiative HVAC sources of radiation gains on inside face
    Array1D<Real64> QRadHVACInReport;        // Surface thermal radiation heat gain at Inside face [J]
    Array1D<Real64> QdotRadHVACInRep;        // Surface thermal radiation heat transfer inside face surface [W]
    Array1D<Real64> QdotRadHVACInRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Inside face surf

    Array1D<Real64> QConvOutReport;        // Surface convection heat gain at Outside face [J]
    Array1D<Real64> QdotConvOutRep;        // Surface convection heat transfer rate at Outside face surface [W]
    Array1D<Real64> QdotConvOutRepPerArea; // Surface conv heat transfer rate per m2 at Outside face surf
    //  (report){w/m2]

    Array1D<Real64> QRadOutReport;        // Surface thermal radiation heat gain at Outside face [J]
    Array1D<Real64> QdotRadOutRep;        // Surface thermal radiation heat transfer outside face surface [W]
    Array1D<Real64> QdotRadOutRepPerArea; // [W/m2]Surface thermal radiation heat transfer rate per m2 at
    //      Outside face surf
    Array1D<Real64> QAirExtReport;  // Surface Outside Face Thermal Radiation to Air Heat Transfer Rate [W]
    Array1D<Real64> QHeatEmiReport; // Surface Outside Face Heat Emission to Air Rate [W]

    Array1D<Real64> OpaqSurfInsFaceCondGainRep; // Equals Opaq Surf Ins Face Cond
    // when Opaq Surf Ins Face Cond >= 0
    Array1D<Real64> OpaqSurfInsFaceCondLossRep; // Equals -Opaq Surf Ins Face Cond
    // when Opaq Surf Ins Face Cond  < 0
    Array1D<Real64> OpaqSurfInsFaceConduction; // Opaque surface inside face heat conduction flow (W)
    // from inside of opaque surfaces, for reporting (W)
    Array1D<Real64> OpaqSurfInsFaceConductionFlux; // Opaque surface inside face heat conduction flux (W/m2)
    // from inside of opaque surfaces, for reporting (W/m2)
    Array1D<Real64> OpaqSurfInsFaceConductionEnergy; // Opaque surface inside face heat conduction flow (J)
    // from inside of opaque surfaces, for reporting (J)

    Array1D<Real64> OpaqSurfExtFaceCondGainRep; // Equals Opaq Surf Ext Face Cond
    // when Opaq Surf Ext Face Cond >= 0
    Array1D<Real64> OpaqSurfExtFaceCondLossRep; // Equals -Opaq Surf Ext Face Cond
    // when Opaq Surf Ext Face Cond  < 0
    Array1D<Real64> OpaqSurfOutsideFaceConduction; // Opaque surface outside face heat conduction flow (W)
    // from inside of opaque surfaces, for reporting (W)
    Array1D<Real64> OpaqSurfOutsideFaceConductionFlux; // Opaque surface outside face heat conduct flux (W/m2)
    // from outside of opaque surfaces, for reporting (W/m2)
    Array1D<Real64> OpaqSurfOutsideFaceConductionEnergy; // Opaque surface outside face heat conduction flow (J)
    // from inside of opaque surfaces, for reporting (J)

    Array1D<Real64> OpaqSurfAvgFaceCondGainRep; // Equals Opaq Surf average Face Cond
    // when Opaq Surf average Face Cond >= 0
    Array1D<Real64> OpaqSurfAvgFaceCondLossRep; // Equals -Opaq Surf average Face Cond
    // when Opaq Surf average Face Cond  < 0
    Array1D<Real64> OpaqSurfAvgFaceConduction; // Opaque surface average heat conduction flow (W)
    // net conduction from outside environ toward inside zone
    //  from inside of opaque surfaces, for reporting (W)
    Array1D<Real64> OpaqSurfAvgFaceConductionFlux; // Opaque surface average face heat conduction flux (W/m2)
    // net conduction from outside environ to inside zone
    //  from inside of opaque surfaces, for reporting (W/m2)
    Array1D<Real64> OpaqSurfAvgFaceConductionEnergy; // Opaque surface average heat conduction flow (J)
    // net conduction from outside environ toward inside zone
    //  from inside of opaque surfaces, for reporting (J)

    Array1D<Real64> OpaqSurfStorageGainRep; // Equals Opaque surface stored heat conduction flow
    // when Opaque surface stored heat conduction flow  >= 0
    Array1D<Real64> OpaqSurfStorageCondLossRep; // Equals -Opaque surface stored heat conduction flow
    // when Opaque surface stored heat conduction flow   < 0
    Array1D<Real64> OpaqSurfStorageConduction; // Opaque surface stored heat conduction flow (W)
    // storage of heat inside surface, positive is increasing in surf
    Array1D<Real64> OpaqSurfStorageConductionFlux; // Opaque surface stored heat conduction flux (W/m2)
    // storage of heat inside surface, positive is increasing in surf
    Array1D<Real64> OpaqSurfStorageConductionEnergy; // Opaque surface stored heat conduction flow (J)
    // storage of heat inside surface, positive is increasing in surf

    Array1D<Real64> OpaqSurfInsFaceBeamSolAbsorbed; // Opaque surface inside face absorbed beam solar,
    // for reporting (W)
    Array1D<Real64> TempSurfOut; // Temperature of the Outside Surface for each heat transfer surface
    // used for reporting purposes only.  Ref: TH(x,1,1)
    Array1D<Real64> QRadSWOutMvIns; // Short wave radiation absorbed on outside of movable insulation
    // unusedREAL(r64), ALLOCATABLE, DIMENSION(:) :: QBV                 !Beam solar absorbed by interior shades in a zone, plus
    // diffuse from beam not absorbed in zone, plus
    // beam absorbed at inside surfaces
    Array1D<Real64> QC; // Short-Wave Radiation Converted Direct To Convection
    Array1D<Real64> QD; // Diffuse solar radiation in a zone from sky and ground diffuse entering
    // through exterior windows and reflecting from interior surfaces,
    // beam from exterior windows reflecting from interior surfaces,
    // and beam entering through interior windows (considered diffuse)
    Array1D<Real64> QDforDaylight; // Diffuse solar radiation in a zone from sky and ground diffuse entering
    // through exterior windows, beam from exterior windows reflecting
    // from interior surfaces, and beam entering through interior windows
    //(considered diffuse)
    // Originally QD, now used only for QSDifSol calc for daylighting
    Array1D<Real64> QDV; // Diffuse solar radiation in a zone from sky and ground diffuse entering
    // through exterior windows
    Array1D<Real64> VMULT;             // 1/(Sum Of A Zone's Inside Surfaces Area*Absorptance)
    Array1D<Real64> VCONV;             // Fraction Of Short-Wave Radiation From Lights Converted To Convection
    Array1D<Real64> NetLWRadToSurf;    // Net interior long wavelength radiation to a surface from other surfaces
    Array1D<Real64> ZoneMRT;           // Zone Mean Radiant Temperature
    Array1D<Real64> QRadSWLightsInAbs; // Short wave from Lights radiation absorbed on inside of opaque surface
    // Variables that are used in both the Surface Heat Balance and the Moisture Balance
    Array1D<Real64> QRadSWOutAbs;      // Short wave radiation absorbed on outside of opaque surface
    Array1D<Real64> QRadSWInAbs;       // Short wave radiation absorbed on inside of opaque surface
    Array1D<Real64> QRadLWOutSrdSurfs; // Long wave radiation absorbed on outside of exterior surface

    Array1D<Real64> QAdditionalHeatSourceOutside; // Additional heat source term on boundary conditions at outside surface
    Array1D<Real64> QAdditionalHeatSourceInside;  // Additional heat source term on boundary conditions at inside surface

    Array1D<Real64> InitialDifSolInAbs;   // Initial diffuse solar absorbed on inside of opaque surface [W/m2]
    Array1D<Real64> InitialDifSolInTrans; // Initial diffuse solar transmitted out through window surface [W/m2]

    // REAL(r64) variables from BLDCTF.inc and only used in the Heat Balance
    Array3D<Real64> TH; // Temperature History (SurfNum,Hist Term,In/Out) where:
    // Hist Term (1 = Current Time, 2-MaxCTFTerms = previous times),
    // In/Out (1 = Outside, 2 = Inside)
    Array3D<Real64> QH; // Flux History (TH and QH are interpolated from THM and QHM for
    // the next user requested time step)
    Array3D<Real64> THM;        // Master Temperature History (on the time step for the construct)
    Array3D<Real64> QHM;        // Master Flux History (on the time step for the construct)
    Array2D<Real64> TsrcHist;   // Temperature history at the source location (SurfNum,Term)
    Array2D<Real64> TuserHist;  // Temperature history at the user specified location (SurfNum,Term)
    Array2D<Real64> QsrcHist;   // Heat source/sink history for the surface (SurfNum,Term)
    Array2D<Real64> TsrcHistM;  // Master temperature history at the source location (SurfNum,Term)
    Array2D<Real64> TuserHistM; // Master temperature history at the user specified location (SurfNum,Term)
    Array2D<Real64> QsrcHistM;  // Master heat source/sink history for the surface (SurfNum,Term)

    Array2D<Real64> FractDifShortZtoZ; // Fraction of diffuse short radiation in Zone 2 transmitted to Zone 1
    Array1D_bool RecDifShortFromZ;     // True if Zone gets short radiation from another
    bool InterZoneWindow(false);       // True if there is an interzone window

    Real64 SumSurfaceHeatEmission(0.0); // Heat emission from all surfaces

    // Functions

    // Clears the global data in DataHeatBalSurface.
    // Needed for unit tests, should not be normally called.
    void clear_state()
    {
        SUMH.deallocate();
        MaxSurfaceTempLimit = 200.0;
        MaxSurfaceTempLimitBeforeFatal = 500.0;
        MinIterations = 1;
        Zone_has_mixed_HT_models.clear();
        CTFConstInPart.deallocate();
        CTFConstOutPart.deallocate();
        CTFCross0.deallocate();
        CTFInside0.deallocate();
        CTFSourceIn0.deallocate();
        TH11Surf.deallocate();
        QsrcHistSurf1.deallocate();
        IsAdiabatic.deallocate();
        IsNotAdiabatic.deallocate();
        IsSource.deallocate();
        IsNotSource.deallocate();
        IsPoolSurf.deallocate();
        IsNotPoolSurf.deallocate();
        TempTermSurf.deallocate();
        TempDivSurf.deallocate();
        TempSurfIn.deallocate();
        TempInsOld.deallocate();
        TempSurfInTmp.deallocate();
        HcExtSurf.deallocate();
        HAirExtSurf.deallocate();
        HSkyExtSurf.deallocate();
        HGrdExtSurf.deallocate();
        TempSource.deallocate();
        TempUserLoc.deallocate();
        TempSurfInRep.deallocate();
        TempSurfInMovInsRep.deallocate();
        QConvInReport.deallocate();
        QdotConvInRep.deallocate();
        QdotConvInRepPerArea.deallocate();
        QRadNetSurfInReport.deallocate();
        QdotRadNetSurfInRep.deallocate();
        QdotRadNetSurfInRepPerArea.deallocate();
        QRadSolarInReport.deallocate();
        QdotRadSolarInRep.deallocate();
        QdotRadSolarInRepPerArea.deallocate();
        QRadLightsInReport.deallocate();
        QdotRadLightsInRep.deallocate();
        QdotRadLightsInRepPerArea.deallocate();
        QRadIntGainsInReport.deallocate();
        QdotRadIntGainsInRep.deallocate();
        QdotRadIntGainsInRepPerArea.deallocate();
        QRadHVACInReport.deallocate();
        QdotRadHVACInRep.deallocate();
        QdotRadHVACInRepPerArea.deallocate();
        QConvOutReport.deallocate();
        QdotConvOutRep.deallocate();
        QdotConvOutRepPerArea.deallocate();
        QRadOutReport.deallocate();
        QdotRadOutRep.deallocate();
        QdotRadOutRepPerArea.deallocate();
        OpaqSurfInsFaceCondGainRep.deallocate();
        OpaqSurfInsFaceCondLossRep.deallocate();
        OpaqSurfInsFaceConduction.deallocate();
        OpaqSurfInsFaceConductionFlux.deallocate();
        OpaqSurfInsFaceConductionEnergy.deallocate();
        OpaqSurfExtFaceCondGainRep.deallocate();
        OpaqSurfExtFaceCondLossRep.deallocate();
        OpaqSurfOutsideFaceConduction.deallocate();
        OpaqSurfOutsideFaceConductionFlux.deallocate();
        OpaqSurfOutsideFaceConductionEnergy.deallocate();
        OpaqSurfAvgFaceCondGainRep.deallocate();
        OpaqSurfAvgFaceCondLossRep.deallocate();
        OpaqSurfAvgFaceConduction.deallocate();
        OpaqSurfAvgFaceConductionFlux.deallocate();
        OpaqSurfAvgFaceConductionEnergy.deallocate();
        OpaqSurfStorageGainRep.deallocate();
        OpaqSurfStorageCondLossRep.deallocate();
        OpaqSurfStorageConduction.deallocate();
        OpaqSurfStorageConductionFlux.deallocate();
        OpaqSurfStorageConductionEnergy.deallocate();
        OpaqSurfInsFaceBeamSolAbsorbed.deallocate();
        TempSurfOut.deallocate();
        QRadSWOutMvIns.deallocate();
        QC.deallocate();
        QD.deallocate();
        QDforDaylight.deallocate();
        QDV.deallocate();
        VMULT.deallocate();
        VCONV.deallocate();
        NetLWRadToSurf.deallocate();
        ZoneMRT.deallocate();
        QRadSWLightsInAbs.deallocate();
        QRadSWOutAbs.deallocate();
        QRadSWInAbs.deallocate();
        QRadLWOutSrdSurfs.deallocate();
        QAdditionalHeatSourceOutside.deallocate();
        QAdditionalHeatSourceInside.deallocate();
        InitialDifSolInAbs.deallocate();
        InitialDifSolInTrans.deallocate();
        TH.deallocate();
        QH.deallocate();
        THM.deallocate();
        QHM.deallocate();
        TsrcHist.deallocate();
        QsrcHist.deallocate();
        TsrcHistM.deallocate();
        QsrcHistM.deallocate();
        FractDifShortZtoZ.deallocate();
        RecDifShortFromZ.deallocate();
        InterZoneWindow = false;
        SumSurfaceHeatEmission = 0;
    }

} // namespace DataHeatBalSurface

} // namespace EnergyPlus
