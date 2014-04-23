// C++ Headers
#include <cmath>

// ObjexxFCL Headers
#include <ObjexxFCL/FArray.functions.hh>
#include <ObjexxFCL/FArray1D.hh>
#include <ObjexxFCL/Fmath.hh>
#include <ObjexxFCL/gio.hh>
#include <ObjexxFCL/string.functions.hh>

// EnergyPlus Headers
#include <PlantCondLoopOperation.hh>
#include <DataEnvironment.hh>
#include <DataGlobals.hh>
#include <DataHVACGlobals.hh>
#include <DataIPShortCuts.hh>
#include <DataLoopNode.hh>
#include <DataPlant.hh>
#include <DataPrecisionGlobals.hh>
#include <DataRuntimeLanguage.hh>
#include <DataSizing.hh>
#include <EMSManager.hh>
#include <FluidProperties.hh>
#include <General.hh>
#include <GeneralRoutines.hh>
#include <InputProcessor.hh>
#include <NodeInputManager.hh>
#include <ReportSizingManager.hh>
#include <ScheduleManager.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

namespace PlantCondLoopOperation {

	// MODIFIED       LKL Sep 03: adding integer/pointers for various parts of operation schemes
	// MODIFIED       DEF JUL 10: complete re-write to support new Plant manager

	// PURPOSE OF THIS MODULE: This module assigns loads to the equipment on
	// the plant and condenser loops that will operate
	// for a given timestep.

	// METHODOLOGY EMPLOYED:  The main driver, "ManagePlantLoadDistribution",
	// gets 'Plant Operation scheme' and 'Plant Equipment List' input.  Pointers are
	// set up in the PlantLoop data structure to allow components to directly access the
	// operation schemes and plant lists that the component shows up on.
	// ManagePlantLoadDistribution is called one time for each component on the loop.
	// It finds the operation scheme and equipment list associated with the component
	// and calculates the component load.  If the component is part of a 'load range'
	// based scheme, it also assigns a component load to each of the components on the
	// equipment list.

	// REFERENCES:

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace DataPlant;
	using DataHVACGlobals::NumPlantLoops;
	using DataHVACGlobals::NumCondLoops;
	using DataHVACGlobals::SmallLoad;
	using FluidProperties::GetSpecificHeatGlycol;

	// Data
	//MODULE PARAMETER DEFINITIONS
	int const HeatingOp( 1 ); // Constant for Heating Operation
	int const CoolingOp( 2 ); // Constant for Cooling Operation
	int const DualOp( 3 ); // Constant for Cooling or Heating Operation

	bool const TurnItemOn( true ); // Convenient for calling TurnPlantItemOnOff instead of hardwired true/false
	bool const TurnItemOff( false ); // Convenient for calling TurnPlantItemOnOff instead of hardwired true/false

	//MODULE VARIABLE DECLARATIONS:

	//SUBROUTINE SPECIFICATIONS FOR MODULE  !SUBROUTINE SPECIFICATIONS FOR MODULE
	//Driver Routines
	//Get Input Routines
	//Initialization Routines
	//Load Distribution/Calculation Routines

	//ON/OFF Utility Routines

	//PLANT EMS Utility Routines

	// MODULE SUBROUTINES:

	// Beginning of Module Driver Subroutines
	//*************************************************************************

	// Functions

	void
	ManagePlantLoadDistribution(
		int const LoopNum, // PlantLoop data structure loop counter
		int const LoopSideNum, // PlantLoop data structure LoopSide counter
		int const BranchNum, // PlantLoop data structure branch counter
		int const CompNum, // PlantLoop data structure component counter
		Real64 & LoopDemand,
		Real64 & RemLoopDemand,
		bool const FirstHVACIteration,
		bool & LoopShutDownFlag, // EMS flag to tell loop solver to shut down pumps
		Optional_bool LoadDistributionWasPerformed
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR:          Dan Fisher
		//       DATE WRITTEN:    April 1999
		//       REVISED:         March 2001
		//                        July 2001, Rick Strand (revision of pump and loop control code)
		//                        July 2010, Dan Fisher, complete rewrite to component based control

		// PURPOSE OF THIS SUBROUTINE:
		// ManageLoopOperation is the driver routine
		// for plant equipment selection.  It calls the general "Get-
		// Input" routines, initializes the loop pointers, then calls the
		// appropriate type of control algorithm (setpoint, load range based,
		// or uncontrolled) for the component

		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// Using/Aliasing
		using DataEnvironment::OutWetBulbTemp; // Current outdoor relative humidity [%]
		using DataEnvironment::OutDryBulbTemp;
		using DataEnvironment::OutDewPointTemp;
		using DataEnvironment::OutRelHum;
		using General::RoundSigDigits;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:

		int ListNum; // DO loop index in PlantLoop()%LoopSide()%Branch()%Comp()%OpScheme()%EquipList(ListNum)
		int CurListNum; // Current list...= ListNum,  used for error checking only
		//Indices in PlantLoop()%LoopSide()%Branch()%Comp() data structure
		int CurCompLevelOpNum; // This is set by the init routine at each FirstHVACIteration.
		//It tells which scheme for this component is currently scheduled
		//and is used to avoid a 'schedule search' on each call
		//It is used as the OpScheme index in PL()%LoopSide()%Branch()%Comp()%OpScheme(CurCompLevelOpNum)
		//Value of pointers held in PlantLoop()%LoopSide()%Branch()%Comp() data structure
		//Used as indices in PlantLoop()%OpScheme() data structure
		int CurSchemePtr; // set by PL()%LoopSide()%Branch()%Comp()%OpScheme(CurCompLevelOpNum)%OpSchemePtr
		//used to locate data in PL()%OpScheme(CurSchemePtr)
		int ListPtr; // !set by PL()%LoopSide()%Branch()%Comp()%OpScheme(CurCompLevelOpNum)%EquipList(CurListNum)ListPtr
		//used to locate data in PL()%OpScheme(CurSchemePtr)%EquipList(ListPtr)
		//Local values from the PlantLoop()%OpScheme() data structure
		std::string CurSchemeTypeName; // current operation scheme type
		std::string CurSchemeName; // current operation scheme name
		int CurSchemeType; // identifier set in PlantData
		Real64 RangeVariable; // holds the 'loop demand', wetbulb temp, etc.
		Real64 TestRangeVariable; // abs of RangeVariable for logic tests etc.
		Real64 RangeHiLimit; // upper limit of the range variable
		Real64 RangeLoLimit; // lower limit of the range variable
		//Local values from the PlantLoop()%LoopSide()%Branch()%Comp() data structure
		int NumEquipLists; // number of equipment lists
		//Error control flags
		bool foundlist; // equipment list found
		bool UpperLimitTooLow; // error processing
		Real64 HighestRange; // error processing
		static int TooLowIndex( 0 ); // error processing
		static int NotTooLowIndex( 0 ); // error processing
		//INTEGER , SAVE                    :: ErrCount = 0     !number of errors
		//CHARACTER(len=20)                 :: CharErrOut       !Error message
		int NumCompsOnList;
		int CompIndex;
		int EquipBranchNum;
		int EquipCompNum;

		//Shut down equipment and return if so instructed by LoopShutDownFlag
		if ( LoopShutDownFlag ) {
			TurnOffLoopEquipment( LoopNum );
			return;
		}

		//Return if there are no loop operation schemes available
		if ( ! any( PlantLoop( LoopNum ).OpScheme.Available() ) ) return;

		//Implement EMS control commands
		ActivateEMSControls( LoopNum, LoopSideNum, BranchNum, CompNum, LoopShutDownFlag );

		//Schedules are checked and CurOpScheme updated on FirstHVACIteration in InitLoadDistribution
		//Here we just load CurOpScheme to a local variable
		CurCompLevelOpNum = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurCompLevelOpNum;
		//If no current operation scheme for component, RETURN
		if ( CurCompLevelOpNum == 0 ) return;
		//set local variables from data structure
		NumEquipLists = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( CurCompLevelOpNum ).NumEquipLists;
		CurSchemePtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( CurCompLevelOpNum ).OpSchemePtr;
		CurSchemeType = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).OpSchemeType;
		CurSchemeTypeName = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).TypeOf;
		CurSchemeName = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).Name;

		//Load the 'range variable' according to the type of control scheme specified
		{ auto const SELECT_CASE_var( CurSchemeType );
		if ( ( SELECT_CASE_var == UncontrolledOpSchemeType ) || ( SELECT_CASE_var == CompSetPtBasedSchemeType ) ) {
			//No RangeVariable specified for these types
		} else if ( SELECT_CASE_var == EMSOpSchemeType ) {
			InitLoadDistribution( FirstHVACIteration );
			//No RangeVariable specified for these types
		} else if ( SELECT_CASE_var == HeatingRBOpSchemeType ) {
			// For zero demand, we need to clean things out before we leave
			if ( LoopDemand < SmallLoad ) {
				InitLoadDistribution( FirstHVACIteration );
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
				return;
			}
			RangeVariable = LoopDemand;
		} else if ( SELECT_CASE_var == CoolingRBOpSchemeType ) {
			// For zero demand, we need to clean things out before we leave
			if ( LoopDemand > ( -1.0 * SmallLoad ) ) {
				InitLoadDistribution( FirstHVACIteration );
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
				return;
			}
			RangeVariable = LoopDemand;
		} else if ( SELECT_CASE_var == DryBulbRBOpSchemeType ) {
			RangeVariable = OutDryBulbTemp;
		} else if ( SELECT_CASE_var == WetBulbRBOpSchemeType ) {
			RangeVariable = OutWetBulbTemp;
		} else if ( SELECT_CASE_var == RelHumRBOpSchemeType ) {
			RangeVariable = OutRelHum;
		} else if ( SELECT_CASE_var == DewPointRBOpSchemeType ) {
			RangeVariable = OutDewPointTemp;
		} else if ( ( SELECT_CASE_var == DryBulbTDBOpSchemeType ) || ( SELECT_CASE_var == WetBulbTDBOpSchemeType ) || ( SELECT_CASE_var == DewPointTDBOpSchemeType ) ) {
			RangeVariable = FindRangeVariable( LoopNum, CurSchemePtr, CurSchemeType );
		} else {
			// No controls specified.  This is a fatal error
			ShowFatalError( "Invalid Operation Scheme Type Requested=" + CurSchemeTypeName + ", in ManagePlantLoadDistribution" );
		}}

		//Find the proper list within the specified scheme
		foundlist = false;
		if ( CurSchemeType == UncontrolledOpSchemeType ) {
			//!***what else do we do with 'uncontrolled' equipment?
			//There's an equipment list...but I think the idea is to just
			//Set one component to run in an 'uncontrolled' way (whatever that means!)

		} else if ( CurSchemeType == CompSetPtBasedSchemeType ) {
			//check for EMS Control
			TurnOnPlantLoopPipes( LoopNum, LoopSideNum );
			FindCompSPLoad( LoopNum, LoopSideNum, BranchNum, CompNum, CurCompLevelOpNum );
		} else if ( CurSchemeType == EMSOpSchemeType ) {
			TurnOnPlantLoopPipes( LoopNum, LoopSideNum );
			DistributeUserDefinedPlantLoad( LoopNum, LoopSideNum, BranchNum, CompNum, CurCompLevelOpNum, CurSchemePtr, LoopDemand, RemLoopDemand );
		} else { //it's a range based control type with multiple equipment lists
			CurListNum = 0;
			for ( ListNum = 1; ListNum <= NumEquipLists; ++ListNum ) {
				//setpointers to 'PlantLoop()%OpScheme()...'structure
				ListPtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( CurCompLevelOpNum ).EquipList( ListNum ).ListPtr;
				RangeHiLimit = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).RangeUpperLimit;
				RangeLoLimit = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).RangeLowerLimit;
				//these limits are stored with absolute values, but the LoopDemand can be negative for cooling
				TestRangeVariable = std::abs( RangeVariable );

				//trying to do something where the last stage still runs the equipment but at the hi limit.

				if ( TestRangeVariable < RangeLoLimit || TestRangeVariable > RangeHiLimit ) {
					if ( ( TestRangeVariable > RangeHiLimit ) && ListPtr == ( PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipListNumForLastStage ) ) {
						// let this go thru, later AdjustChangeInLoadForLastStageUpperRangeLimit will cap dispatch to RangeHiLimit
						CurListNum = ListNum;
						break;
					} else {
						continue;
					}
				} else {
					CurListNum = ListNum;
					break;
				}
			}

			if ( CurListNum > 0 ) {
				// there could be equipment on another list that needs to be nulled out, it may have a load from earlier iteration
				for ( ListNum = 1; ListNum <= NumEquipLists; ++ListNum ) {
					if ( ListNum == CurListNum ) continue; // leave current one alone
					NumCompsOnList = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListNum ).NumComps;
					for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
						EquipBranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListNum ).Comp( CompIndex ).BranchNumPtr;
						EquipCompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListNum ).Comp( CompIndex ).CompNumPtr;
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( EquipBranchNum ).Comp( EquipCompNum ).MyLoad = 0.0;
					}
				}
				if ( PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).NumComps > 0 ) {
					TurnOnPlantLoopPipes( LoopNum, LoopSideNum );
					DistributePlantLoad( LoopNum, LoopSideNum, CurSchemePtr, ListPtr, LoopDemand, RemLoopDemand );
					if ( present( LoadDistributionWasPerformed ) ) LoadDistributionWasPerformed = true;
				}
			}

		} //End of range based schemes

	}

	// Beginning of GetInput subroutines for the Module
	//******************************************************************************

	void
	GetPlantOperationInput( bool & GetInputOK )
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   October 1998
		//       MODIFIED       July 2010, Dan Fisher, restructure input data
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE: This subroutine reads the primary plant loop
		// operation schemes from the input file

		// METHODOLOGY EMPLOYED: calls the Input Processor to retrieve data from input file.
		// The format of the Energy+.idd (the EnergyPlus input data dictionary) for the
		// following keywords is reflected exactly in this subroutine:
		//    PlantEquipmentOperationSchemes
		//    CondenserEquipmentOperationSchemes

		// REFERENCES:
		// na

		// Using/Aliasing
		using ScheduleManager::GetScheduleIndex;
		using InputProcessor::GetNumObjectsFound;
		using InputProcessor::GetObjectItem;
		using InputProcessor::GetObjectItemNum;
		using InputProcessor::FindItemInList;
		using InputProcessor::VerifyName;
		using namespace DataIPShortCuts; // Data for field names, blank numerics

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		static std::string const RoutineName( "GetPlantOperationInput: " ); // include trailing blank space

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int LoopNum; // Loop counter (Plant or Cond)
		int OpNum; // Scheme counter
		int Num; // Item counter
		int NumPlantOpSchemes; // Total Number of PlantEquipmentOperationSchemes
		int NumCondOpSchemes; // Total Number of CondenserEquipmentOperationSchemes
		int NumAlphas; // Number of alpha items in the input object
		int NumNums; // Number of numeric items in the input object
		int IOStat;
		std::string PlantOpSchemeName; // Name of the plant or condenser operating scheme
		std::string CurrentModuleObject; // for ease in renaming
		std::string PlantLoopObject; // for ease in renaming
		FArray1D_string OpSchemeNames; // Used to verify unique op scheme names
		bool IsNotOK;
		bool IsBlank;
		bool ErrorsFound; // Passed in from OpSchemeInput

		ErrorsFound = false;

		if ( ! allocated( PlantLoop ) ) {
			GetInputOK = false;
			return;
		} else {
			GetInputOK = true;
		}

		// get number of operation schemes
		CurrentModuleObject = "PlantEquipmentOperationSchemes";
		NumPlantOpSchemes = GetNumObjectsFound( CurrentModuleObject );

		if ( NumPlantOpSchemes > 0 ) {
			// OpSchemeListNames is used to determine if there are any duplicate operation scheme names
			OpSchemeNames.allocate( NumPlantOpSchemes );
			OpSchemeNames = "";
			Num = 0;
			for ( OpNum = 1; OpNum <= NumPlantOpSchemes; ++OpNum ) {
				GetObjectItem( CurrentModuleObject, OpNum, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat );
				IsNotOK = false;
				IsBlank = false;
				VerifyName( cAlphaArgs( 1 ), OpSchemeNames, Num, IsNotOK, IsBlank, CurrentModuleObject + " Name" );
				if ( IsNotOK ) {
					ErrorsFound = true;
					continue;
				}
				++Num;
				OpSchemeNames( Num ) = cAlphaArgs( 1 );
			}

			OpSchemeNames.deallocate();

		}

		CurrentModuleObject = "CondenserEquipmentOperationSchemes";
		NumCondOpSchemes = GetNumObjectsFound( CurrentModuleObject );

		if ( NumCondOpSchemes > 0 ) {
			// OpSchemeListNames is used to determine if there are any duplicate operation scheme names
			OpSchemeNames.allocate( NumCondOpSchemes );
			OpSchemeNames = "";
			Num = 0;
			for ( OpNum = 1; OpNum <= NumCondOpSchemes; ++OpNum ) {
				GetObjectItem( CurrentModuleObject, OpNum, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat );
				IsNotOK = false;
				IsBlank = false;
				VerifyName( cAlphaArgs( 1 ), OpSchemeNames, Num, IsNotOK, IsBlank, CurrentModuleObject + " Name" );
				if ( IsNotOK ) {
					ErrorsFound = true;
					continue;
				}
				++Num;
				OpSchemeNames( Num ) = cAlphaArgs( 1 );
			}

			OpSchemeNames.deallocate();

		}

		//Load the Plant data structure
		for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
			PlantOpSchemeName = PlantLoop( LoopNum ).OperationScheme;
			if ( LoopNum <= NumPlantLoops ) {
				CurrentModuleObject = "PlantEquipmentOperationSchemes";
				PlantLoopObject = "PlantLoop";
			} else {
				CurrentModuleObject = "CondenserEquipmentOperationSchemes";
				PlantLoopObject = "CondenserLoop";
			}
			OpNum = GetObjectItemNum( CurrentModuleObject, PlantOpSchemeName );
			if ( OpNum > 0 ) {
				GetObjectItem( CurrentModuleObject, OpNum, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
				PlantLoop( LoopNum ).NumOpSchemes = ( NumAlphas - 1 ) / 3;
				if ( PlantLoop( LoopNum ).NumOpSchemes > 0 ) {
					PlantLoop( LoopNum ).OpScheme.allocate( PlantLoop( LoopNum ).NumOpSchemes );
					for ( Num = 1; Num <= PlantLoop( LoopNum ).NumOpSchemes; ++Num ) {
						PlantLoop( LoopNum ).OpScheme( Num ).TypeOf = cAlphaArgs( Num * 3 - 1 );

						{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).OpScheme( Num ).TypeOf );

						if ( SELECT_CASE_var == "LOAD RANGE BASED OPERATION" ) { // Deprecated
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = LoadRBOpSchemeType; // Deprecated
							ShowSevereError( CurrentModuleObject + " = \"" + cAlphaArgs( 1 ) + "\" deprecated field value =\"" + PlantLoop( LoopNum ).OpScheme( Num ).TypeOf + "\"." );
							ShowContinueError( "... should be replaced with PlantEquipmentOperation:CoolingLoad or " "PlantEquipmentOperation:HeatingLoad" );
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:COOLINGLOAD" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = CoolingRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:HEATINGLOAD" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = HeatingRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:COMPONENTSETPOINT" ) { //* Temp Based Control
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = CompSetPtBasedSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:USERDEFINED" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = EMSOpSchemeType;
							AnyEMSPlantOpSchemesInModel = true;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDRYBULB" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = DryBulbRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORWETBULB" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = WetBulbRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDEWPOINT" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = DewPointRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORRELATIVEHUMIDITY" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = RelHumRBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDRYBULBDIFFERENCE" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = DryBulbTDBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORWETBULBDIFFERENCE" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = WetBulbTDBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDEWPOINTDIFFERENCE" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = DewPointTDBOpSchemeType;
						} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:UNCONTROLLED" ) {
							PlantLoop( LoopNum ).OpScheme( Num ).OpSchemeType = UncontrolledOpSchemeType;
						} else { // invalid op scheme type for plant loop
							ShowSevereError( RoutineName + "Invalid " + cAlphaFieldNames( Num * 3 - 1 ) + '=' + cAlphaArgs( Num * 3 - 1 ) + ", entered in " + CurrentModuleObject + '=' + cAlphaArgs( 1 ) );
							ErrorsFound = true;
						}}

						PlantLoop( LoopNum ).OpScheme( Num ).Name = cAlphaArgs( Num * 3 );
						PlantLoop( LoopNum ).OpScheme( Num ).Sched = cAlphaArgs( Num * 3 + 1 );
						PlantLoop( LoopNum ).OpScheme( Num ).SchedPtr = GetScheduleIndex( PlantLoop( LoopNum ).OpScheme( Num ).Sched );
						if ( PlantLoop( LoopNum ).OpScheme( Num ).SchedPtr == 0 ) {
							ShowSevereError( RoutineName + "Invalid " + cAlphaFieldNames( Num * 3 + 1 ) + " = \"" + cAlphaArgs( Num * 3 + 1 ) + "\", entered in " + CurrentModuleObject + "= \"" + cAlphaArgs( 1 ) + "\"." );
							ErrorsFound = true;
						}
					}
				} else {
					ShowSevereError( CurrentModuleObject + " = \"" + cAlphaArgs( 1 ) + "\", requires at least " + cAlphaFieldNames( 2 ) + ", " + cAlphaFieldNames( 3 ) + " and " + cAlphaFieldNames( 4 ) + " to be specified." );
					ErrorsFound = true;
				}
			} else {
				ShowSevereError( RoutineName + PlantLoopObject + '=' + PlantLoop( LoopNum ).Name + " is expecting" );
				ShowContinueError( CurrentModuleObject + '=' + PlantOpSchemeName + ", but not found." );
				ErrorsFound = true;
			}
		}

		if ( ErrorsFound ) {
			ShowFatalError( RoutineName + "Errors found in getting input for PlantEquipmentOperationSchemes or " "CondenserEquipmentOperationSchemes" );
		}

	}

	void
	GetOperationSchemeInput()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   October 1998
		//       MODIFIED       August 2001, LKL -- Validations
		//       RE-ENGINEERED  July 2010, Dan Fisher, restructure input data

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine reads the primary plant loop
		// operation schemes from the input file

		// METHODOLOGY EMPLOYED:
		// calls the Input Processor to retrieve data from input file.
		// The format of the Energy+.idd (the EnergyPlus input data dictionary) for the
		// following keywords is reflected exactly in this subroutine:
		//    PlantEquipmentOperation:*

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::GetNumObjectsFound;
		using InputProcessor::GetObjectItem;
		using InputProcessor::VerifyName;
		using InputProcessor::SameString;
		using InputProcessor::FindItemInList;
		using NodeInputManager::GetOnlySingleNode;
		using namespace DataLoopNode;
		using namespace DataSizing;
		using namespace DataIPShortCuts;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na
		// SUBROUTINE PARAMETER DEFINITIONS:
		static std::string const RoutineName( "GetOperationSchemeInput: " ); // include trailing blank space

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int SchemeNum;
		int Num;
		int NumAlphas;
		int NumNums;
		int IOStat;
		bool IsNotOK; // Flag to verify name
		bool IsBlank; // Flag for blank name
		bool ErrorsFound; // May be set here and passed on
		int CLRBO; // Number ofCooling Load Range Based Operation Inputs
		int HLRBO; // Number ofHeating Load Range Based Operation Inputs
		int DBRBO; // Number ofDry Bulb Temperature Range Based Operation Inputs
		int WBRBO; // Number ofWet Bulb Temperature Range Based Operation Inputs
		int DPRBO; // Number ofDewPoint Temperature Range Based Operation Inputs
		int RHRBO; // Number ofRelative Humidity Range Based Operation Inputs
		int CSPBO; // Number of Component SetPoint Based Operation Inputs
		int DBTDBO; // Number ofDry Bulb Temperature Range Based Operation Inputs
		int WBTDBO; // Number ofWet Bulb Temperature Range Based Operation Inputs
		int DPTDBO; // Number ofDewPoint Temperature Range Based Operation Inputs
		int NumSchemes; // Number of Condenser equipment lists
		int NumUncontrolledSchemes; // Number of Condenser equipment lists
		int NumUserDefOpSchemes; // number of user defined EMS op schemes
		int CELists; // Number of Condenser equipment lists
		int PELists; // Number of Plant equipment lists
		int Count; // Loop counter
		int NumSchemeLists;
		int LoopNum;
		std::string CurrentModuleObject; // for ease in renaming.
		FArray1D_string TempVerifyNames;

		ErrorsFound = false; //DSU CS

		//**********VERIFY THE 'PLANTEQUIPMENTOPERATION:...' KEYWORDS**********
		CLRBO = GetNumObjectsFound( "PlantEquipmentOperation:CoolingLoad" );
		HLRBO = GetNumObjectsFound( "PlantEquipmentOperation:HeatingLoad" );
		DBRBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorDryBulb" );
		WBRBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorWetBulb" );
		DPRBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorDewpoint" );
		RHRBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorRelativeHumidity" );
		CSPBO = GetNumObjectsFound( "PlantEquipmentOperation:ComponentSetpoint" ); //* Temp Based Control
		NumUserDefOpSchemes = GetNumObjectsFound( "PlantEquipmentOperation:UserDefined" );
		DBTDBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorDryBulbDifference" );
		WBTDBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorWetBulbDifference" );
		DPTDBO = GetNumObjectsFound( "PlantEquipmentOperation:OutdoorDewpointDifference" );
		NumSchemes = CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO + WBTDBO + DPTDBO + NumUserDefOpSchemes;
		NumUncontrolledSchemes = GetNumObjectsFound( "PlantEquipmentOperation:Uncontrolled" );
		if ( ( NumSchemes + NumUncontrolledSchemes ) <= 0 ) {
			ShowFatalError( "No PlantEquipmentOperation:* objects specified. Stop simulation." );
		}

		// test for blank or duplicates -- this section just determines if there are any duplicate operation scheme names
		TempVerifyNames.allocate( NumSchemes );
		TempVerifyNames = "";

		//Check for existence of duplicates in keyword names
		Count = 0;
		for ( Num = 1; Num <= NumSchemes; ++Num ) {
			if ( CLRBO > 0 && Num <= CLRBO ) {
				CurrentModuleObject = "PlantEquipmentOperation:CoolingLoad";
				Count = Num;
			} else if ( HLRBO > 0 && Num <= ( CLRBO + HLRBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:HeatingLoad";
				Count = Num - CLRBO;
			} else if ( DBRBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorDryBulb";
				Count = Num - CLRBO - HLRBO;
			} else if ( WBRBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorWetBulb";
				Count = Num - CLRBO - HLRBO - DBRBO;
			} else if ( DPRBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorDewpoint";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO;
			} else if ( RHRBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorRelativeHumidity";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO;
			} else if ( CSPBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:ComponentSetpoint";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO;
			} else if ( DBTDBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorDryBulbDifference";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO - CSPBO;
			} else if ( WBTDBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO + WBTDBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorWetBulbDifference";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO - CSPBO - DBTDBO;
			} else if ( DPTDBO > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO + WBTDBO + DPTDBO ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:OutdoorDewpointDifference";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO - CSPBO - DBTDBO - WBTDBO;
			} else if ( NumUncontrolledSchemes > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO + WBTDBO + DPTDBO + NumUncontrolledSchemes ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:Uncontrolled";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO - CSPBO - DBTDBO - WBTDBO - DPTDBO;
			} else if ( NumUserDefOpSchemes > 0 && Num <= ( CLRBO + HLRBO + DBRBO + WBRBO + DPRBO + RHRBO + CSPBO + DBTDBO + WBTDBO + DPTDBO + NumUncontrolledSchemes + NumUserDefOpSchemes ) ) {
				CurrentModuleObject = "PlantEquipmentOperation:UserDefined";
				Count = Num - CLRBO - HLRBO - DBRBO - WBRBO - DPRBO - RHRBO - CSPBO - DBTDBO - WBTDBO - DPTDBO - NumUncontrolledSchemes;
			} else {
				ShowFatalError( "Error in control scheme identification" );
			}

			GetObjectItem( CurrentModuleObject, Count, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat );
			IsNotOK = false;
			IsBlank = false;
			VerifyName( cAlphaArgs( 1 ), TempVerifyNames, Num - 1, IsNotOK, IsBlank, CurrentModuleObject + " Name" );
			if ( IsNotOK ) {
				ErrorsFound = true;
				continue;
			}
			TempVerifyNames( Num ) = cAlphaArgs( 1 );

		}
		TempVerifyNames.deallocate();

		//**********VERIFY THE 'PlantEquipmentList' AND 'CondenserEquipmentList' KEYWORDS*********
		PELists = GetNumObjectsFound( "PlantEquipmentList" );
		CELists = GetNumObjectsFound( "CondenserEquipmentList" );
		NumSchemeLists = PELists + CELists;
		TempVerifyNames.allocate( NumSchemeLists );
		TempVerifyNames = "";
		Count = 0;
		for ( Num = 1; Num <= NumSchemeLists; ++Num ) {
			if ( Num <= PELists ) {
				CurrentModuleObject = "PlantEquipmentList";
				Count = Num;
			} else {
				CurrentModuleObject = "CondenserEquipmentList";
				Count = Num - PELists;
			}
			GetObjectItem( CurrentModuleObject, Count, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat );
			IsNotOK = false;
			IsBlank = false;
			VerifyName( cAlphaArgs( 1 ), TempVerifyNames, Num - 1, IsNotOK, IsBlank, CurrentModuleObject + " Name" );
			if ( IsNotOK ) {
				ErrorsFound = true;
				continue;
			}
			TempVerifyNames( Num ) = cAlphaArgs( 1 );
		}
		TempVerifyNames.deallocate();

		//**********GET INPUT AND LOAD PLANT DATA STRUCTURE*********

		//extend number of equipment lists to include one for each CSPBO
		NumSchemeLists += CSPBO + NumUserDefOpSchemes;
		for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
			for ( SchemeNum = 1; SchemeNum <= PlantLoop( LoopNum ).NumOpSchemes; ++SchemeNum ) {

				{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).OpScheme( SchemeNum ).TypeOf );

				if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:COOLINGLOAD" ) {
					CurrentModuleObject = "PlantEquipmentOperation:CoolingLoad";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, CLRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:HEATINGLOAD" ) {
					CurrentModuleObject = "PlantEquipmentOperation:HeatingLoad";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, HLRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:COMPONENTSETPOINT" ) { //* Temp Based Control
					CurrentModuleObject = "PlantEquipmentOperation:ComponentSetPoint";
					FindCompSPInput( CurrentModuleObject, CSPBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:USERDEFINED" ) {
					CurrentModuleObject = "PlantEquipmentOperation:UserDefined";
					GetUserDefinedOpSchemeInput( CurrentModuleObject, NumUserDefOpSchemes, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDRYBULB" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorDryBulb";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, DBRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORWETBULB" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorWetBulb";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, WBRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDEWPOINT" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorDewPoint";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, DPRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORRELATIVEHUMIDITY" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorrelativeHumidity";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, RHRBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDRYBULBDIFFERENCE" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorDryBulbDifference";
					FindDeltaTempRangeInput( CurrentModuleObject, DBTDBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORWETBULBDIFFERENCE" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorWetBulbDifference";
					FindDeltaTempRangeInput( CurrentModuleObject, WBTDBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:OUTDOORDEWPOINTDIFFERENCE" ) {
					CurrentModuleObject = "PlantEquipmentOperation:OutdoorDewPointDifference";
					FindDeltaTempRangeInput( CurrentModuleObject, DPTDBO, LoopNum, SchemeNum, ErrorsFound );

				} else if ( SELECT_CASE_var == "PLANTEQUIPMENTOPERATION:UNCONTROLLED" ) {
					CurrentModuleObject = "PlantEquipmentOperation:Uncontrolled";
					FindRangeBasedOrUncontrolledInput( CurrentModuleObject, NumUncontrolledSchemes, LoopNum, SchemeNum, ErrorsFound );

				} else { // invalid op scheme type for plant loop
					// DSU?  Seems like the alpha args below is incorrect....
					ShowSevereError( "Invalid operation scheme type = \"" + cAlphaArgs( Num * 3 - 1 ) + "\", entered in " + CurrentModuleObject + '=' + cAlphaArgs( 1 ) );
					ErrorsFound = true;
				}}
			}
		}

		// Validate that component names/types in each list correspond to a valid component in input file
		if ( ErrorsFound ) {
			ShowFatalError( RoutineName + "Errors found getting inputs. Previous error(s) cause program termination." );
		}
	}

	void
	FindRangeBasedOrUncontrolledInput(
		std::string & CurrentModuleObject, // for ease in renaming
		int const NumSchemes, // May be set here and passed on
		int const LoopNum, // May be set here and passed on
		int const SchemeNum, // May be set here and passed on
		bool & ErrorsFound // May be set here and passed on
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   July 2010
		//       MODIFIED       Chandan Sharma, August 2010
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// Load range based or uncontrolled input into PLANTLOOP data structure

		// METHODOLOGY EMPLOYED:
		// calls the Input Processor to retrieve data from input file.
		// The format of the Energy+.idd (the EnergyPlus input data dictionary) for the
		// following keywords is reflected exactly in this subroutine:
		//       PlantEquipmentOperation:CoolingLoad
		//       PlantEquipmentOperation:HeatingLoad
		//       PlantEquipmentOperation:OutdoorDryBulb
		//       PlantEquipmentOperation:OutdoorWetBulb
		//       PlantEquipmentOperation:OutdoorDewPoint
		//       PlantEquipmentOperation:OutdoorRelativeHumidity
		//       PlantEquipmentOperation:Uncontrolled

		// REFERENCES:
		// na

		// Using/Aliasing
		using InputProcessor::GetObjectItem;
		using InputProcessor::SameString;
		using InputProcessor::GetObjectDefMaxArgs;
		using General::RoundSigDigits;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		int const Plant( 1 ); // Used to identify whether the current loop is Plant
		int const Condenser( 2 ); // Used to identify whether the current loop is Condenser

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NumAlphas;
		int NumNums;
		int IOStat;
		FArray1D_string AlphArray; // Alpha input items for object
		FArray1D_string cAlphaFields; // Alpha field names
		FArray1D_string cNumericFields; // Numeric field names
		FArray1D< Real64 > NumArray; // Numeric input items for object
		FArray1D_bool lAlphaBlanks; // Logical array, alpha field input BLANK = .TRUE.
		FArray1D_bool lNumericBlanks; // Logical array, numeric field input BLANK = .TRUE.
		static int TotalArgs( 0 ); // Total number of alpha and numeric arguments (max) for a
		//   certain object in the input file
		int Num;
		int NumEquipLists;
		int ListNum;
		std::string LoopOpSchemeObj; // Used to identify the object name for loop equipment operation scheme
		bool SchemeNameFound; // Set to FALSE if a match of OpScheme object and OpScheme name is not found
		int InnerListNum; // inner loop list number
		Real64 OuterListNumLowerLimit;
		Real64 OuterListNumUpperLimit;
		Real64 InnerListNumLowerLimit;
		Real64 InnerListNumUpperLimit;

		SchemeNameFound = true;

		// Determine max number of alpha and numeric arguments for all objects being read, in order to allocate local arrays
		GetObjectDefMaxArgs( CurrentModuleObject, TotalArgs, NumAlphas, NumNums );

		AlphArray.allocate( NumAlphas );
		AlphArray = "";
		cAlphaFields.allocate( NumAlphas );
		cAlphaFields = "";
		cNumericFields.allocate( NumNums );
		cNumericFields = "";
		NumArray.allocate( NumNums );
		NumArray = 0.0;
		lAlphaBlanks.allocate( NumAlphas );
		lAlphaBlanks = true;
		lNumericBlanks.allocate( NumNums );
		lNumericBlanks = true;

		if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
			LoopOpSchemeObj = "PlantEquipmentOperationSchemes";
		} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) {
			LoopOpSchemeObj = "CondenserEquipmentOperationSchemes";
		}

		if ( NumSchemes > 0 ) {
			for ( Num = 1; Num <= NumSchemes; ++Num ) {
				GetObjectItem( CurrentModuleObject, Num, AlphArray, NumAlphas, NumArray, NumNums, IOStat );
				if ( SameString( PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, AlphArray( 1 ) ) ) break;
				if ( Num == NumSchemes ) {
					ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
					ErrorsFound = true;
					SchemeNameFound = false;
				}
			}
			if ( SchemeNameFound ) {
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists = ( NumAlphas - 1 );
				if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists <= 0 ) {
					ShowSevereError( CurrentModuleObject + " = \"" + AlphArray( 1 ) + "\", specified without equipment list." );
					ErrorsFound = true;
				} else {
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList.allocate( PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists );
					NumEquipLists = PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists;
					if ( NumNums <= 0 ) { // Uncontrolled OpScheme type
						ListNum = NumEquipLists; // NumEquipLists is always 1 for Uncontrolled OpScheme type
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Name = AlphArray( 2 );
						LoadEquipList( LoopNum, SchemeNum, ListNum, ErrorsFound );
					} else { // Range based OpScheme type
						for ( ListNum = 1; ListNum <= NumEquipLists; ++ListNum ) {
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit = NumArray( ListNum * 2 - 1 );
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit = NumArray( ListNum * 2 );
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Name = AlphArray( ListNum + 1 );
							if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit < 0.0 ) {
								ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", found a negative value for an upper limit in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
								ErrorsFound = true;
							}

							{ auto const SELECT_CASE_var( CurrentModuleObject ); // different op schemes have different lower limit check values

							if ( ( SELECT_CASE_var == "PlantEquipmentOperation:CoolingLoad" ) || ( SELECT_CASE_var == "PlantEquipmentOperation:HeatingLoad" ) || ( SELECT_CASE_var == "PlantEquipmentOperation:OutdoorrelativeHumidity" ) ) {
								// these should not be less than zero
								if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit < 0.0 ) {
									ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", found a negative value for a lower limit in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
									ErrorsFound = true;
								}
							} else {
								// others should not be less than -70
								if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit < -70. ) {
									ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", found too low of a value for a lower limit in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
									ErrorsFound = true;
								}
							}}

							if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit > PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit ) {
								ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", found a lower limit that is higher than an upper limit in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
								ErrorsFound = true;
							}

							LoadEquipList( LoopNum, SchemeNum, ListNum, ErrorsFound );
						}
						// now run through lists again and check that range limits do not overlap each other
						for ( ListNum = 1; ListNum <= NumEquipLists; ++ListNum ) {
							OuterListNumLowerLimit = PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit;
							OuterListNumUpperLimit = PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit;
							for ( InnerListNum = 1; InnerListNum <= NumEquipLists; ++InnerListNum ) {
								if ( InnerListNum == ListNum ) continue; // don't check against self.
								InnerListNumLowerLimit = PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( InnerListNum ).RangeLowerLimit;
								InnerListNumUpperLimit = PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( InnerListNum ).RangeUpperLimit;
								// Check if inner list has a lower limit that is between an outer's lower and upper limit
								if ( InnerListNumLowerLimit > OuterListNumLowerLimit && InnerListNumLowerLimit < OuterListNumUpperLimit ) {
									ShowWarningError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", detected overlapping ranges in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
									ShowContinueError( "Range # " + RoundSigDigits( InnerListNum ) + " Lower limit = " + RoundSigDigits( InnerListNumLowerLimit, 1 ) + " lies within the Range # " + RoundSigDigits( ListNum ) + " (" + RoundSigDigits( OuterListNumLowerLimit, 1 ) + " to " + RoundSigDigits( OuterListNumUpperLimit, 1 ) + ")." );
									ShowContinueError( "Check that input for load range limit values do not overlap, " "and the simulation continues..." );

								}
								// Check if inner list has an upper limit that is between an outer's lower and upper limit
								if ( InnerListNumUpperLimit > OuterListNumLowerLimit && InnerListNumUpperLimit < OuterListNumUpperLimit ) {
									ShowWarningError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", detected overlapping ranges in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
									ShowContinueError( "Range # " + RoundSigDigits( InnerListNum ) + " Upper limit = " + RoundSigDigits( InnerListNumUpperLimit, 1 ) + " lies within Range # " + RoundSigDigits( ListNum ) + " (" + RoundSigDigits( OuterListNumLowerLimit, 1 ) + " to " + RoundSigDigits( OuterListNumUpperLimit, 1 ) + ")." );
									ShowContinueError( "Check that input for load range limit values do not overlap, " "and the simulation continues..." );

								}
							}
						}

					}
				}
			}
		} else {
			ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
			ErrorsFound = true;
		}

		AlphArray.deallocate();
		cAlphaFields.deallocate();
		cNumericFields.deallocate();
		NumArray.deallocate();
		lAlphaBlanks.deallocate();
		lNumericBlanks.deallocate();

	}

	void
	FindDeltaTempRangeInput(
		std::string & CurrentModuleObject, // for ease in renaming
		int const NumSchemes, // May be set here and passed on
		int const LoopNum, // May be set here and passed on
		int const SchemeNum, // May be set here and passed on
		bool & ErrorsFound // May be set here and passed on
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Chandan Sharma
		//       DATE WRITTEN   August 2010
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// Load range based input into PLANTLOOP data structure

		// METHODOLOGY EMPLOYED:
		// calls the Input Processor to retrieve data from input file.
		// The format of the Energy+.idd (the EnergyPlus input data dictionary) for the
		// following keywords is reflected exactly in this subroutine:
		//       PlantEquipmentOperation:OutdoorDryBulbDifference
		//       PlantEquipmentOperation:OutdoorWetBulbDifference
		//       PlantEquipmentOperation:OutdoorDewPointDifference

		// REFERENCES:
		// Based on subroutine FindRangeBasedOrUncontrolledInput from Dan Fisher, July 2010

		// Using/Aliasing
		using InputProcessor::GetObjectItem;
		using InputProcessor::SameString;
		using InputProcessor::GetObjectDefMaxArgs;
		using NodeInputManager::GetOnlySingleNode;
		using namespace DataLoopNode;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		int const Plant( 1 ); // Used to identify whether the current loop is Plant
		int const Condenser( 2 ); // Used to identify whether the current loop is Condenser

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NumAlphas;
		int NumNums;
		int IOStat;
		FArray1D_string AlphArray; // Alpha input items for object
		FArray1D_string cAlphaFields; // Alpha field names
		FArray1D_string cNumericFields; // Numeric field names
		FArray1D< Real64 > NumArray; // Numeric input items for object
		FArray1D_bool lAlphaBlanks; // Logical array, alpha field input BLANK = .TRUE.
		FArray1D_bool lNumericBlanks; // Logical array, numeric field input BLANK = .TRUE.
		static int TotalArgs( 0 ); // Total number of alpha and numeric arguments (max) for a
		//   certain object in the input file
		int Num;
		int NumEquipLists;
		int ListNum;
		std::string LoopOpSchemeObj; // Used to identify the object name for loop equipment operation scheme
		bool SchemeNameFound; // Set to FALSE if a match of OpScheme object and OpScheme name is not found

		SchemeNameFound = true;

		// Determine max number of alpha and numeric arguments for all objects being read, in order to allocate local arrays
		GetObjectDefMaxArgs( CurrentModuleObject, TotalArgs, NumAlphas, NumNums );

		AlphArray.allocate( NumAlphas );
		AlphArray = "";
		cAlphaFields.allocate( NumAlphas );
		cAlphaFields = "";
		cNumericFields.allocate( NumNums );
		cNumericFields = "";
		NumArray.allocate( NumNums );
		NumArray = 0.0;
		lAlphaBlanks.allocate( NumAlphas );
		lAlphaBlanks = true;
		lNumericBlanks.allocate( NumNums );
		lNumericBlanks = true;

		if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
			LoopOpSchemeObj = "PlantEquipmentOperationSchemes";
		} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) {
			LoopOpSchemeObj = "CondenserEquipmentOperationSchemes";
		}

		if ( NumSchemes > 0 ) {
			for ( Num = 1; Num <= NumSchemes; ++Num ) {
				GetObjectItem( CurrentModuleObject, Num, AlphArray, NumAlphas, NumArray, NumNums, IOStat );
				if ( SameString( PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, AlphArray( 1 ) ) ) break;
				if ( Num == NumSchemes ) {
					ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
					ErrorsFound = true;
					SchemeNameFound = false;
				}
			}
			if ( SchemeNameFound ) {
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists = ( NumAlphas - 2 );
				if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists <= 0 ) {
					ShowSevereError( CurrentModuleObject + " = \"" + AlphArray( 1 ) + "\", specified without equipment list." );
					ErrorsFound = true;
				} else {
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList.allocate( PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists );
					NumEquipLists = PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists;
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).ReferenceNodeName = AlphArray( 2 );
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).ReferenceNodeNumber = GetOnlySingleNode( AlphArray( 2 ), ErrorsFound, CurrentModuleObject, AlphArray( 1 ), NodeType_Water, NodeConnectionType_Sensor, 1, ObjectIsNotParent );
					//For DO Loop below -- Check for lower limit > upper limit.(invalid)
					for ( ListNum = 1; ListNum <= NumEquipLists; ++ListNum ) {
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit = NumArray( ListNum * 2 - 1 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit = NumArray( ListNum * 2 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Name = AlphArray( ListNum + 2 );
						if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeLowerLimit > PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).RangeUpperLimit ) {
							ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", found a lower limit that is higher than an upper limit in " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
							ErrorsFound = true;
						}
						LoadEquipList( LoopNum, SchemeNum, ListNum, ErrorsFound );
					}
				}
			}
		} else {
			ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
			ErrorsFound = true;
		}

		AlphArray.deallocate();
		cAlphaFields.deallocate();
		cNumericFields.deallocate();
		NumArray.deallocate();
		lAlphaBlanks.deallocate();
		lNumericBlanks.deallocate();

	}

	void
	LoadEquipList(
		int const LoopNum, // May be set here and passed on
		int const SchemeNum, // May be set here and passed on
		int const ListNum, // May be set here and passed on
		bool & ErrorsFound // May be set here and passed on
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   July 2010
		//       MODIFIED       B. Griffith Sept 2011, major rewrite
		//                      allow mixing list types across plant types, store info first time
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// Load delta range based input into PLANTLOOP data structure

		// METHODOLOGY EMPLOYED:
		// calls the Input Processor to retrieve data from input file.

		// REFERENCES:
		// na
		// Using/Aliasing
		using InputProcessor::GetNumObjectsFound;
		using InputProcessor::GetObjectItem;
		using InputProcessor::SameString;
		using namespace DataIPShortCuts;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		static bool MyOneTimeFlag( true );
		bool FoundIntendedList;
		int Num;
		int MachineNum;
		int PELists;
		int CELists;
		//  INTEGER :: NumLists
		int NumAlphas;
		int NumNums;
		int IOStat;
		bool IsNotOK;
		std::string CurrentModuleObject;
		static int TotNumLists( 0 );
		static FArray1D_string EquipListsNameList;
		static FArray1D_int EquipListsTypeList;
		static FArray1D_int EquipListsIndexList;
		int iIndex;
		bool firstblank;

		if ( MyOneTimeFlag ) {
			// assemble mapping between list names and indices one time
			PELists = GetNumObjectsFound( "PlantEquipmentList" );
			CELists = GetNumObjectsFound( "CondenserEquipmentList" );
			TotNumLists = PELists + CELists;
			if ( TotNumLists > 0 ) {
				EquipListsNameList.allocate( TotNumLists );
				EquipListsTypeList.allocate( TotNumLists );
				EquipListsIndexList.allocate( TotNumLists );

				//First load PlantEquipmentList info
				if ( PELists > 0 ) {
					CurrentModuleObject = "PlantEquipmentList";
					for ( Num = 1; Num <= PELists; ++Num ) {
						iIndex = Num;
						GetObjectItem( CurrentModuleObject, Num, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
						EquipListsNameList( iIndex ) = cAlphaArgs( 1 );
						EquipListsTypeList( iIndex ) = LoopType_Plant;
						EquipListsIndexList( iIndex ) = Num;
						MachineNum = 2;
						while ( MachineNum <= NumAlphas ) {
							firstblank = false;
							if ( lAlphaFieldBlanks( MachineNum ) || lAlphaFieldBlanks( MachineNum + 1 ) ) {
								if ( lAlphaFieldBlanks( MachineNum ) ) {
									ShowSevereError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", invalid component specification." );
									ShowContinueError( cAlphaFieldNames( MachineNum ) + " is blank." );
									firstblank = true;
									ErrorsFound = true;
								}
								if ( lAlphaFieldBlanks( MachineNum + 1 ) ) {
									if ( ! firstblank ) {
										ShowSevereError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", invalid component specification." );
									}
									ShowContinueError( cAlphaFieldNames( MachineNum + 1 ) + " is blank." );
									ErrorsFound = true;
								}
							} else {
								ValidateComponent( cAlphaArgs( MachineNum ), cAlphaArgs( MachineNum + 1 ), IsNotOK, CurrentModuleObject );
								if ( IsNotOK ) {
									ShowContinueError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", Input Error." );
									ErrorsFound = true;
								}
							}
							MachineNum += 2;
						}
					}
				}
				if ( CELists > 0 ) {
					CurrentModuleObject = "CondenserEquipmentList";
					for ( Num = 1; Num <= CELists; ++Num ) {
						iIndex = Num + PELists;
						GetObjectItem( CurrentModuleObject, Num, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
						EquipListsNameList( iIndex ) = cAlphaArgs( 1 );
						EquipListsTypeList( iIndex ) = LoopType_Condenser;
						EquipListsIndexList( iIndex ) = Num;
						MachineNum = 2;
						while ( MachineNum <= NumAlphas ) {
							firstblank = false;
							if ( lAlphaFieldBlanks( MachineNum ) || lAlphaFieldBlanks( MachineNum + 1 ) ) {
								if ( lAlphaFieldBlanks( MachineNum ) ) {
									ShowSevereError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", invalid component specification." );
									ShowContinueError( cAlphaFieldNames( MachineNum ) + " is blank." );
									firstblank = true;
									ErrorsFound = true;
								}
								if ( lAlphaFieldBlanks( MachineNum + 1 ) ) {
									if ( ! firstblank ) {
										ShowSevereError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", invalid component specification." );
									}
									ShowContinueError( cAlphaFieldNames( MachineNum + 1 ) + " is blank." );
									ErrorsFound = true;
								}
							} else {
								ValidateComponent( cAlphaArgs( MachineNum ), cAlphaArgs( MachineNum + 1 ), IsNotOK, CurrentModuleObject );
								if ( IsNotOK ) {
									ShowContinueError( CurrentModuleObject + "=\"" + cAlphaArgs( 1 ) + "\", Input Error." );
									ErrorsFound = true;
								}
							}
							MachineNum += 2;
						}
					}
				}
			}
			if ( ErrorsFound ) {
				ShowFatalError( "LoadEquipList/GetEquipmentLists: Failed due to preceding errors." );
			}
			MyOneTimeFlag = false;
		}

		FoundIntendedList = false;
		// find name in set of possible list
		for ( Num = 1; Num <= TotNumLists; ++Num ) {
			if ( SameString( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Name, EquipListsNameList( Num ) ) ) {
				FoundIntendedList = true;
				// get object item for real this time
				{ auto const SELECT_CASE_var( EquipListsTypeList( Num ) );
				if ( SELECT_CASE_var == LoopType_Plant ) {
					CurrentModuleObject = "PlantEquipmentList";
				} else if ( SELECT_CASE_var == LoopType_Condenser ) {
					CurrentModuleObject = "CondenserEquipmentList";
				}}
				GetObjectItem( CurrentModuleObject, EquipListsIndexList( Num ), cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).NumComps = ( NumAlphas - 1 ) / 2;
				if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).NumComps > 0 ) {
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Comp.allocate( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).NumComps );
					for ( MachineNum = 1; MachineNum <= PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).NumComps; ++MachineNum ) {
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Comp( MachineNum ).TypeOf = cAlphaArgs( MachineNum * 2 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Comp( MachineNum ).Name = cAlphaArgs( MachineNum * 2 + 1 );
					} //MachineList
				}
			}
		}

		if ( ! FoundIntendedList ) {
			ShowSevereError( "LoadEquipList: Failed to find PlantEquipmentList or CondenserEquipmentList object named = " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( ListNum ).Name );
			ErrorsFound = true;

		}
	}

	void
	FindCompSPInput(
		std::string & CurrentModuleObject, // for ease in renaming
		int const NumSchemes, // May be set here and passed on
		int const LoopNum, // May be set here and passed on
		int const SchemeNum, // May be set here and passed on
		bool & ErrorsFound // May be set here and passed on
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   July 2010
		//       MODIFIED       B. Griffith, check setpoint nodes have setpoint managers on EMS on them.
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// Load component setpoint based input into PLANTLOOP data structure

		// METHODOLOGY EMPLOYED:
		// calls the Input Processor to retrieve data from input file.
		// The format of the Energy+.idd (the EnergyPlus input data dictionary) for the
		// following keywords is reflected exactly in this subroutine:
		//    PlantEquipmentOperation:ComponentSetPoint

		// REFERENCES:
		// na
		// Using/Aliasing
		using InputProcessor::GetObjectItem;
		using InputProcessor::SameString;
		using namespace DataLoopNode;
		using NodeInputManager::GetOnlySingleNode;
		using namespace DataSizing;
		using namespace DataIPShortCuts;
		using ReportSizingManager::ReportSizingOutput;
		using DataGlobals::AnyEnergyManagementSystemInModel;
		using EMSManager::CheckIfNodeSetPointManagedByEMS;
		using EMSManager::iTemperatureSetPoint;
		using EMSManager::iTemperatureMinSetPoint;
		using EMSManager::iTemperatureMaxSetPoint;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		std::string EquipNum;

		// SUBROUTINE PARAMETER DEFINITIONS:
		int const Plant( 1 ); // Used to identify whether the current loop is Plant
		int const Condenser( 2 ); // Used to identify whether the current loop is Condenser

		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NumAlphas;
		int NumNums;
		int CompNum;
		int CompInNode;
		int IOStat;
		Real64 CompFlowRate;
		int Num;
		std::string LoopOpSchemeObj; // Used to identify the object name for loop equipment operation scheme
		bool SchemeNameFound; // Set to FALSE if a match of OpScheme object and OpScheme name is not found
		bool NodeEMSSetPointMissing;

		SchemeNameFound = true;

		if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
			LoopOpSchemeObj = "PlantEquipmentOperationSchemes";
		} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) {
			LoopOpSchemeObj = "CondenserEquipmentOperationSchemes";
		}

		if ( NumSchemes > 0 ) {
			for ( Num = 1; Num <= NumSchemes; ++Num ) {
				GetObjectItem( CurrentModuleObject, Num, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat );
				if ( SameString( PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, cAlphaArgs( 1 ) ) ) break;
				if ( Num == NumSchemes ) {
					ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
					ErrorsFound = true;
					SchemeNameFound = false;
				}
			}
			if ( SchemeNameFound ) {
				// why only one equip list assumed here? because component setpoint managers have their own lists contained.
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists = 1;
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList.allocate( 1 );
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps = ( NumAlphas - 1 ) / 5;
				if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps > 0 ) {
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp.allocate( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps );
					for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps; ++CompNum ) {
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).TypeOf = cAlphaArgs( CompNum * 5 - 3 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).Name = cAlphaArgs( CompNum * 5 - 2 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).DemandNodeName = cAlphaArgs( CompNum * 5 - 1 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).DemandNodeNum = GetOnlySingleNode( cAlphaArgs( CompNum * 5 - 1 ), ErrorsFound, CurrentModuleObject, cAlphaArgs( 1 ), NodeType_Water, NodeConnectionType_Sensor, 1, ObjectIsNotParent );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName = cAlphaArgs( CompNum * 5 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum = GetOnlySingleNode( cAlphaArgs( CompNum * 5 ), ErrorsFound, CurrentModuleObject, cAlphaArgs( 1 ), NodeType_Water, NodeConnectionType_Sensor, 1, ObjectIsNotParent );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointFlowRate = rNumericArgs( CompNum );

						if ( rNumericArgs( CompNum ) == AutoSize ) {
							for ( Num = 1; Num <= SaveNumPlantComps; ++Num ) {
								CompInNode = CompDesWaterFlow( Num ).SupNode;
								CompFlowRate = CompDesWaterFlow( Num ).DesVolFlowRate;
								if ( CompInNode == PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).DemandNodeNum ) {
									PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointFlowRate = CompFlowRate;
								} else {
									//call error...Demand node must be component inlet node for autosizing
								}
							}
							gio::write( EquipNum, "*" ) << Num;
							ReportSizingOutput( CurrentModuleObject, PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, "Design Water Flow Rate [m3/s] Equipment # " + stripped( EquipNum ), CompFlowRate );
						}

						{ auto const SELECT_CASE_var( cAlphaArgs( CompNum * 5 + 1 ) );
						if ( SELECT_CASE_var == "COOLING" ) {
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum = CoolingOp;
						} else if ( SELECT_CASE_var == "HEATING" ) {
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum = HeatingOp;
						} else if ( SELECT_CASE_var == "DUAL" ) {
							PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum = DualOp;
						}}

						if ( ( cAlphaArgs( 5 + 1 ) != "COOLING" ) && ( cAlphaArgs( 5 + 1 ) != "HEATING" ) && ( cAlphaArgs( 5 + 1 ) != "DUAL" ) ) {
							ShowSevereError( "Equipment Operation Mode should be either HEATING or COOLING or DUAL mode, for " + CurrentModuleObject + '=' + cAlphaArgs( 1 ) );
						}
						//check that setpoint node has valid setpoint managers or EMS
						{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).LoopDemandCalcScheme );
						if ( SELECT_CASE_var == SingleSetPoint ) {
							if ( Node( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum ).TempSetPoint == SensedNodeFlagValue ) {
								if ( ! AnyEnergyManagementSystemInModel ) {
									ShowSevereError( "Missing temperature setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
									ShowContinueError( "A temperature setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
									if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
										ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=SingleSetpoint" );
									} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
									}
									ShowContinueError( " Use a setpoint manager to place a single temperature setpoint on the node" );
									ErrorsFound = true;
								} else {
									// need call to EMS to check node
									NodeEMSSetPointMissing = false;
									CheckIfNodeSetPointManagedByEMS( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum, iTemperatureSetPoint, NodeEMSSetPointMissing );
									if ( NodeEMSSetPointMissing ) {
										ShowSevereError( "Missing temperature setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
										ShowContinueError( "A temperature setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
										if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
											ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=SingleSetpoint" );
										} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
										}
										ShowContinueError( " Use a setpoint manager or EMS actuator to place a single temperature setpoint on node" );
										ErrorsFound = true;
									}
								}
							}
						} else if ( SELECT_CASE_var == DualSetPointDeadBand ) {
							if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum == CoolingOp ) {
								if ( Node( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum ).TempSetPointHi == SensedNodeFlagValue ) {
									if ( ! AnyEnergyManagementSystemInModel ) {
										ShowSevereError( "Missing temperature high setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
										ShowContinueError( "A temperature high setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
										if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
											ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
										} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
										}
										ShowContinueError( " Use a setpoint manager to place a dual temperature setpoint on the node" );
										ErrorsFound = true;
									} else {
										// need call to EMS to check node
										NodeEMSSetPointMissing = false;
										CheckIfNodeSetPointManagedByEMS( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum, iTemperatureMaxSetPoint, NodeEMSSetPointMissing );
										if ( NodeEMSSetPointMissing ) {
											ShowSevereError( "Missing high temperature setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
											ShowContinueError( "A high temperature setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
											if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
												ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
											} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
											}
											ShowContinueError( " Use a setpoint manager or EMS actuator to place a dual or high temperature" " setpoint on node" );
											ErrorsFound = true;
										}
									}
								}
							} else if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum == HeatingOp ) {
								if ( Node( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum ).TempSetPointLo == SensedNodeFlagValue ) {
									if ( ! AnyEnergyManagementSystemInModel ) {
										ShowSevereError( "Missing temperature low setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
										ShowContinueError( "A temperature low setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
										if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
											ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
										} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
										}
										ShowContinueError( " Use a setpoint manager to place a dual temperature setpoint on the node" );
										ErrorsFound = true;
									} else {
										// need call to EMS to check node
										NodeEMSSetPointMissing = false;
										CheckIfNodeSetPointManagedByEMS( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum, iTemperatureMinSetPoint, NodeEMSSetPointMissing );
										CheckIfNodeSetPointManagedByEMS( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum, iTemperatureMaxSetPoint, NodeEMSSetPointMissing );
										if ( NodeEMSSetPointMissing ) {
											ShowSevereError( "Missing low temperature setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
											ShowContinueError( "A low temperature setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
											if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
												ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
											} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
											}
											ShowContinueError( " Use a setpoint manager or EMS actuator to place a dual or low temperature" " setpoint on node" );
											ErrorsFound = true;
										}
									}
								}
							} else if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).CtrlTypeNum == DualOp ) {
								if ( ( Node( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum ).TempSetPointHi == SensedNodeFlagValue ) || ( Node( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum ).TempSetPointLo == SensedNodeFlagValue ) ) {
									if ( ! AnyEnergyManagementSystemInModel ) {
										ShowSevereError( "Missing temperature dual setpoints for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
										ShowContinueError( "A dual temperaturesetpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
										if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
											ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
										} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
										}
										ShowContinueError( " Use a setpoint manager to place a dual temperature setpoint on the node" );
										ErrorsFound = true;
									} else {
										// need call to EMS to check node
										NodeEMSSetPointMissing = false;
										CheckIfNodeSetPointManagedByEMS( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeNum, iTemperatureMinSetPoint, NodeEMSSetPointMissing );
										if ( NodeEMSSetPointMissing ) {
											ShowSevereError( "Missing dual temperature setpoint for " + CurrentModuleObject + " named " + cAlphaArgs( 1 ) );
											ShowContinueError( "A dual temperature setpoint is needed at the node named " + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).SetPointNodeName );
											if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
												ShowContinueError( "PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Plant Loop Demand Calculation Scheme=DualSetpointDeadband" );
											} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) { // not applicable to Condenser loops
											}
											ShowContinueError( " Use a setpoint manager or EMS actuator to place a dual temperature" " setpoint on node" );
											ErrorsFound = true;
										}
									}
								}
							}
						}}
					}
				} else {
					ShowSevereError( CurrentModuleObject + " = \"" + cAlphaArgs( 1 ) + "\", specified without any machines." );
					ErrorsFound = true;
				}
			}
		} else {
			ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
			ErrorsFound = true;
		}
	}

	void
	GetUserDefinedOpSchemeInput(
		std::string & CurrentModuleObject, // for ease in renaming
		int const NumSchemes, // May be set here and passed on
		int const LoopNum, // May be set here and passed on
		int const SchemeNum, // May be set here and passed on
		bool & ErrorsFound // May be set here and passed on
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         <author>
		//       DATE WRITTEN   <date_written>
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// <description>

		// METHODOLOGY EMPLOYED:
		// <description>

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataIPShortCuts;
		using namespace DataPlant;
		using InputProcessor::GetObjectItem;
		using InputProcessor::SameString;
		using InputProcessor::FindItemInList;

		using DataRuntimeLanguage::EMSProgramCallManager;
		using DataRuntimeLanguage::NumProgramCallManagers;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		int const Plant( 1 ); // Used to identify whether the current loop is Plant
		int const Condenser( 2 ); // Used to identify whether the current loop is Condenser

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NumAlphas;
		int NumNums;
		int Num;
		int CompNum;
		int IOStat;
		bool SchemeNameFound; // Set to FALSE if a match of OpScheme object and OpScheme name is not found
		std::string LoopOpSchemeObj; // Used to identify the object name for loop equipment operation scheme
		int StackMngrNum; // local temporary for Erl program calling manager index
		bool lDummy;

		SchemeNameFound = true;

		if ( PlantLoop( LoopNum ).TypeOfLoop == Plant ) {
			LoopOpSchemeObj = "PlantEquipmentOperationSchemes";
		} else if ( PlantLoop( LoopNum ).TypeOfLoop == Condenser ) {
			LoopOpSchemeObj = "CondenserEquipmentOperationSchemes";
		}

		if ( NumSchemes > 0 ) {

			for ( Num = 1; Num <= NumSchemes; ++Num ) {
				GetObjectItem( CurrentModuleObject, Num, cAlphaArgs, NumAlphas, rNumericArgs, NumNums, IOStat, lNumericFieldBlanks, lAlphaFieldBlanks, cAlphaFieldNames, cNumericFieldNames );
				if ( SameString( PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, cAlphaArgs( 1 ) ) ) break; //found the correct one
				if ( Num == NumSchemes ) { // did not find it
					ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
					ErrorsFound = true;
					SchemeNameFound = false;
				}
			}
			if ( SchemeNameFound ) {
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).NumEquipLists = 1;
				PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList.allocate( 1 );

				PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps = ( NumAlphas - 3 ) / 2;
				if ( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps > 0 ) {
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp.allocate( PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps );
					for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).NumComps; ++CompNum ) {
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).TypeOf = cAlphaArgs( CompNum * 2 + 2 );
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).Name = cAlphaArgs( CompNum * 2 + 3 );

						//Setup EMS actuators for machines' MyLoad.
						SetupEMSActuator( "Plant Equipment Operation", PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + ':' + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).Name, "Distributed Load Rate", "[W]", lDummy, PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).EMSActuatorDispatchedLoadValue );
						SetupEMSInternalVariable( "Component Remaining Current Demand Rate", PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + ':' + PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).Name, "[W]", PlantLoop( LoopNum ).OpScheme( SchemeNum ).EquipList( 1 ).Comp( CompNum ).EMSIntVarRemainingLoadValue );
					}
				}
				StackMngrNum = FindItemInList( cAlphaArgs( 2 ), EMSProgramCallManager.Name(), NumProgramCallManagers );
				if ( StackMngrNum > 0 ) { // found it
					PlantLoop( LoopNum ).OpScheme( SchemeNum ).ErlSimProgramMngr = StackMngrNum;
				} else {
					ShowSevereError( "Invalid " + cAlphaFieldNames( 2 ) + '=' + cAlphaArgs( 2 ) );
					ShowContinueError( "Entered in " + CurrentModuleObject + '=' + cAlphaArgs( 1 ) );
					ShowContinueError( "EMS Program Manager Name not found." );
					ErrorsFound = true;
				}
				if ( ! lAlphaFieldBlanks( 3 ) ) {
					StackMngrNum = FindItemInList( cAlphaArgs( 3 ), EMSProgramCallManager.Name(), NumProgramCallManagers );
					if ( StackMngrNum > 0 ) { // found it
						PlantLoop( LoopNum ).OpScheme( SchemeNum ).ErlInitProgramMngr = StackMngrNum;
					} else {
						ShowSevereError( "Invalid " + cAlphaFieldNames( 3 ) + '=' + cAlphaArgs( 3 ) );
						ShowContinueError( "Entered in " + CurrentModuleObject + '=' + cAlphaArgs( 1 ) );
						ShowContinueError( "EMS Program Manager Name not found." );
						ErrorsFound = true;
					}
				}

				// setup internal variable for Supply Side Current Demand Rate [W]
				SetupEMSInternalVariable( "Supply Side Current Demand Rate", PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name, "[W]", PlantLoop( LoopNum ).OpScheme( SchemeNum ).EMSIntVarLoopDemandRate );
			}

		} else {
			ShowSevereError( LoopOpSchemeObj + " = \"" + PlantLoop( LoopNum ).OperationScheme + "\", could not find " + CurrentModuleObject + " = \"" + PlantLoop( LoopNum ).OpScheme( SchemeNum ).Name + "\"." );
			ErrorsFound = true;
		}

	}

	// End of GetInput subroutines for the Module
	//******************************************************************************

	// Beginning Initialization Section of the Plant Loop Module
	//******************************************************************************

	void
	InitLoadDistribution( bool const FirstHVACIteration )
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR:          Dan Fisher
		//       DATE WRITTEN:    July 2010
		//       REVISED:

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine scans equipment lists and matches a particular
		// plant component with a component on the list.  Pointers to the
		// operation scheme and equipment list are saved on the plant data
		// structure to facilitate a new load management routine that calls
		// ManageLoadDistribution for every component.

		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// Using/Aliasing
		using DataGlobals::BeginEnvrnFlag;
		using DataGlobals::emsCallFromUserDefinedComponentModel;
		using EMSManager::ManageEMS;
		using InputProcessor::FindItem;
		using InputProcessor::SameString;
		using ScheduleManager::GetCurrentScheduleValue;
		using ScheduleManager::GetScheduleIndex;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int LoopPtr;
		int LoopSidePtr;
		int BranchPtr;
		int CompPtr;
		int LoopNum;
		int DummyLoopNum;
		int LoopSideNum;
		int BranchNum;
		int CompNum;
		int Index;
		int EquipNum;
		int OpNum;
		int OpSchemePtr;
		int SchemeNum;
		int thisSchemeNum;
		int SchemeType;
		int ListNum;
		static bool MyOneTimeFlag( true );
		bool FoundScheme;
		bool FoundSchemeMatch;
		//  LOGICAL, SAVE                     :: FirstHVACInitsDone = .FALSE.
		//  LOGICAL, SAVE                     :: MyEnvrnFlag = .TRUE.
		int ThisTypeOfNum;
		int CompOpNum;
		int OldNumOpSchemes;
		int OldNumEquipLists;
		int NewNumEquipLists;
		int NewNumOpSchemes;
		int NumSearchResults;
		bool GetInputOK; // successful Get Input
		static bool GetPlantOpInput( true ); // successful Get Input
		bool errFlag1;
		bool errFlag2;
		Real64 HighestRange;

		// Object Data
		FArray1D< OpSchemePtrData > TempCompOpScheme;

		errFlag2 = false;
		//Get Input
		if ( GetPlantOpInput ) {
			GetPlantOperationInput( GetInputOK );
			if ( GetInputOK ) {
				GetOperationSchemeInput();
				GetPlantOpInput = false;
			} else {
				return;
			}
		}

		//ONE TIME INITS
		if ( MyOneTimeFlag ) {
			//Set up 'component' to 'op scheme' pointers in Plant data structure
			//We're looking for matches between a component on a PlantLoop()%OpScheme()%List()
			//and the same component in the PlantLoop()%LoopSide()%Branch()%Comp() data structure

			// first loop over main operation scheme data and finish filling out indexes to plant topology for the components in the lists
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( OpNum = 1; OpNum <= PlantLoop( LoopNum ).NumOpSchemes; ++OpNum ) {
					for ( ListNum = 1; ListNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).NumEquipLists; ++ListNum ) {
						for ( EquipNum = 1; EquipNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).NumComps; ++EquipNum ) {

							ThisTypeOfNum = FindItem( PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).TypeOf, SimPlantEquipTypes, NumSimPlantEquipTypes );
							errFlag1 = false;
							ScanPlantLoopsForObject( PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).Name, ThisTypeOfNum, DummyLoopNum, LoopSideNum, BranchNum, CompNum, _, _, NumSearchResults, _, LoopNum, errFlag1 );

							if ( errFlag1 ) {
								ShowSevereError( "InitLoadDistribution: Equipment specified for operation scheme not found on correct loop" );
								ShowContinueError( "Operation Scheme name = " + PlantLoop( LoopNum ).OpScheme( OpNum ).Name );
								ShowContinueError( "Loop name = " + PlantLoop( LoopNum ).Name );
								ShowContinueError( "Component name = " + PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).Name );
								ShowFatalError( "InitLoadDistribution: Simulation terminated because of error in operation scheme." );

							}

							PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).LoopNumPtr = DummyLoopNum;
							PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).LoopSideNumPtr = LoopSideNum;
							PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).BranchNumPtr = BranchNum;
							PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).CompNumPtr = CompNum;

							if ( ValidLoopEquipTypes( ThisTypeOfNum ) == LoopType_Plant && PlantLoop( LoopNum ).TypeOfLoop == LoopType_Condenser ) {
								ShowSevereError( "InitLoadDistribution: CondenserLoop=\"" + PlantLoop( LoopNum ).Name + "\", Operation Scheme=\"" + PlantLoop( LoopNum ).OperationScheme + "\"," );
								ShowContinueError( "Scheme type=" + PlantLoop( LoopNum ).OpScheme( OpNum ).TypeOf + ", Name=\"" + PlantLoop( LoopNum ).OpScheme( OpNum ).Name + "\" includes equipment that is not valid on a Condenser Loop" );
								ShowContinueError( "Component " + ccSimPlantEquipTypes( ThisTypeOfNum ) + " not allowed as supply equipment on this type of loop." );
								ShowContinueError( "Component name = " + PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).Name );

								errFlag2 = true;
							}
							if ( ValidLoopEquipTypes( ThisTypeOfNum ) == LoopType_Condenser && PlantLoop( LoopNum ).TypeOfLoop == LoopType_Plant ) {
								ShowSevereError( "InitLoadDistribution: PlantLoop=\"" + PlantLoop( LoopNum ).Name + "\", Operation Scheme=\"" + PlantLoop( LoopNum ).OperationScheme + "\"," );
								ShowContinueError( "Scheme type=" + PlantLoop( LoopNum ).OpScheme( OpNum ).TypeOf + ", Name=\"" + PlantLoop( LoopNum ).OpScheme( OpNum ).Name + "\" includes equipment that is not valid on a Plant Loop" );
								ShowContinueError( "Component " + ccSimPlantEquipTypes( ThisTypeOfNum ) + " not allowed as supply equipment on this type of loop." );
								ShowContinueError( "Component name = " + PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).Name );
								errFlag2 = true;
							}

						} //Equipment on List
					} //List
				} //operation scheme
			} //loop

			//second loop, fill op schemes info at each component.
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( OpNum = 1; OpNum <= PlantLoop( LoopNum ).NumOpSchemes; ++OpNum ) {
					for ( ListNum = 1; ListNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).NumEquipLists; ++ListNum ) {
						for ( EquipNum = 1; EquipNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).NumComps; ++EquipNum ) {
							// dereference indices (stored in previous loop)
							DummyLoopNum = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).LoopNumPtr;
							LoopSideNum = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).LoopSideNumPtr;
							BranchNum = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).BranchNumPtr;
							CompNum = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( EquipNum ).CompNumPtr;

							if ( PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes == 0 ) {
								// first op scheme for this component, allocate OpScheme and its EquipList to size 1
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme.allocate( 1 );
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( 1 ).EquipList.allocate( 1 );
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes = 1;
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( 1 ).NumEquipLists = 1;
								// store pointers
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( 1 ).OpSchemePtr = OpNum;
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( 1 ).EquipList( 1 ).ListPtr = ListNum;
								PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( 1 ).EquipList( 1 ).CompPtr = EquipNum;

							} else if ( PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes > 0 ) {
								// already an op scheme
								OldNumOpSchemes = PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes;
								// create and store complete copy of old opScheme structure
								TempCompOpScheme.allocate( OldNumOpSchemes );
								for ( thisSchemeNum = 1; thisSchemeNum <= OldNumOpSchemes; ++thisSchemeNum ) {
									OldNumEquipLists = PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).NumEquipLists;
									TempCompOpScheme( thisSchemeNum ).EquipList.allocate( OldNumEquipLists );
								}
								TempCompOpScheme = PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme;

								//could be new list on existing scheme or new scheme with new list.  Check and see
								FoundSchemeMatch = false;
								for ( thisSchemeNum = 1; thisSchemeNum <= OldNumOpSchemes; ++thisSchemeNum ) {
									//compare the OpScheme index, 'opnum', in the PlantLoop()%OpScheme()data structure
									//with the OpSchemePtr in the PlantLoop()%LoopSide()%Branch()%Comp() data structure.
									if ( OpNum != PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).OpSchemePtr ) continue;
									FoundSchemeMatch = true;
									break;
								}
								if ( FoundSchemeMatch ) {
									//op scheme already exists, but need to add a list to the existing OpScheme
									NewNumEquipLists = PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).NumEquipLists + 1;

									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).EquipList.deallocate();
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).EquipList.allocate( NewNumEquipLists );
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).EquipList( {1,NewNumEquipLists - 1} ) = TempCompOpScheme( thisSchemeNum ).EquipList; //structure array assignment
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).NumEquipLists = NewNumEquipLists;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).EquipList( NewNumEquipLists ).ListPtr = ListNum;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( thisSchemeNum ).EquipList( NewNumEquipLists ).CompPtr = EquipNum;

								} else { //(.NOT.FoundSchemeMatch)THEN
									// add new op scheme and a new list
									NewNumOpSchemes = OldNumOpSchemes + 1;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme.deallocate();
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme.allocate( NewNumOpSchemes );
									for ( SchemeNum = 1; SchemeNum <= OldNumOpSchemes; ++SchemeNum ) {
										NewNumEquipLists = TempCompOpScheme( SchemeNum ).NumEquipLists;
										PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( SchemeNum ).EquipList.allocate( NewNumEquipLists );
									}
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( {1,OldNumOpSchemes} ) = TempCompOpScheme; // structure array assignment

									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( NewNumOpSchemes ).EquipList.allocate( 1 );
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes = NewNumOpSchemes;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( NewNumOpSchemes ).NumEquipLists = 1;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( NewNumOpSchemes ).OpSchemePtr = OpNum;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( NewNumOpSchemes ).EquipList( 1 ).ListPtr = ListNum;
									PlantLoop( DummyLoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( NewNumOpSchemes ).EquipList( 1 ).CompPtr = EquipNum;

								}

								if ( allocated( TempCompOpScheme ) ) TempCompOpScheme.deallocate();

							}

						} //Equipment on List
					} //List
				} //operation scheme
			} //loop

			//check the pointers to see if a single component is attached to more than one type of control scheme
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( LoopSideNum = DemandSide; LoopSideNum <= SupplySide; ++LoopSideNum ) {
					for ( BranchNum = 1; BranchNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++BranchNum ) {
						for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).TotalComponents; ++CompNum ) {
							if ( allocated( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme ) ) {
								for ( Index = 1; Index <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NumOpSchemes; ++Index ) {
									OpSchemePtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( Index ).OpSchemePtr;
									if ( OpSchemePtr == 0 ) {
										ShowSevereError( "InitLoadDistribution: no operation scheme index found for component on PlantLoop=" + PlantLoop( LoopNum ).Name );
										ShowContinueError( "Component name = " + PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Name );
										errFlag2 = true;
									}
									if ( Index == 1 ) {
										SchemeType = PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).OpSchemeType;
									} else {
										if ( SchemeType != PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).OpSchemeType ) {
											//CALL FATAL ERROR 'component may not be specified on two types of operation schemes
											//DSU?  BG do not understand.  Cannot different op schemes be in effect at different times?
											//  I thought this would be allowed??
										}
									}
								}
							}
						}
					}
				}
			}

			// fill out information on which equipment list is the "last" meaning it has the highest upper limit for load range
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( OpNum = 1; OpNum <= PlantLoop( LoopNum ).NumOpSchemes; ++OpNum ) {
					// skip non-load based op schemes
					if ( ( PlantLoop( LoopNum ).OpScheme( OpNum ).OpSchemeType != HeatingRBOpSchemeType ) && ( PlantLoop( LoopNum ).OpScheme( OpNum ).OpSchemeType != CoolingRBOpSchemeType ) ) continue;
					HighestRange = 0.0;
					for ( ListNum = 1; ListNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).NumEquipLists; ++ListNum ) {
						HighestRange = max( HighestRange, PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).RangeUpperLimit );
					} //List
					for ( ListNum = 1; ListNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).NumEquipLists; ++ListNum ) {
						if ( HighestRange == PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).RangeUpperLimit ) {
							PlantLoop( LoopNum ).OpScheme( OpNum ).EquipListNumForLastStage = ListNum;
						}
					}
				} //operation scheme
			} //loop

			MyOneTimeFlag = false;
		}

		if ( AnyEMSPlantOpSchemesInModel ) {
			// Execute any Initialization EMS program calling managers for User-Defined operation.
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( OpNum = 1; OpNum <= PlantLoop( LoopNum ).NumOpSchemes; ++OpNum ) {
					if ( PlantLoop( LoopNum ).OpScheme( OpNum ).OpSchemeType == EMSOpSchemeType ) {
						if ( BeginEnvrnFlag && PlantLoop( LoopNum ).OpScheme( OpNum ).MyEnvrnFlag ) {
							if ( PlantLoop( LoopNum ).OpScheme( OpNum ).ErlInitProgramMngr > 0 ) {
								ManageEMS( emsCallFromUserDefinedComponentModel, PlantLoop( LoopNum ).OpScheme( OpNum ).ErlInitProgramMngr );
							}
							PlantLoop( LoopNum ).OpScheme( OpNum ).MyEnvrnFlag = false;
						}
						if ( ! BeginEnvrnFlag ) PlantLoop( LoopNum ).OpScheme( OpNum ).MyEnvrnFlag = true;
					}
				} //operation scheme
			} //loop
		}

		//FIRST HVAC INITS
		if ( FirstHVACIteration ) {
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				for ( LoopSideNum = DemandSide; LoopSideNum <= SupplySide; ++LoopSideNum ) {
					for ( BranchNum = 1; BranchNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++BranchNum ) {
						for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).TotalComponents; ++CompNum ) {
							// initalize components 'ON-AVAILABLE-NO LOAD-NO EMS CTRL'
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = true;
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideOn = false;
							//  Zero out the old curOpSchemePtr so that we don't get 'carry-over' when we update schedules
							if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurOpSchemeType != DemandOpSchemeType && PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurOpSchemeType != PumpOpSchemeType && PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurOpSchemeType != WSEconOpSchemeType && PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurOpSchemeType != NoControlOpSchemeType ) {
								PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurOpSchemeType = NoControlOpSchemeType;
							}
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).CurCompLevelOpNum = 0;
						}
					}
				}
			}
			//Update the OpScheme schedules
			for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
				FoundScheme = false;
				for ( OpNum = 1; OpNum <= PlantLoop( LoopNum ).NumOpSchemes; ++OpNum ) {
					if ( GetCurrentScheduleValue( PlantLoop( LoopNum ).OpScheme( OpNum ).SchedPtr ) > 0.0 ) {
						PlantLoop( LoopNum ).OpScheme( OpNum ).Available = true;
						FoundScheme = true;
						for ( ListNum = 1; ListNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).NumEquipLists; ++ListNum ) {
							//The component loop loads the pointers from the OpScheme data structure
							//If the component happens to be active in more than schedule, the *LAST*
							//schedule found will be activated
							for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).NumComps; ++CompNum ) {
								LoopPtr = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( CompNum ).LoopNumPtr;
								LoopSidePtr = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( CompNum ).LoopSideNumPtr;
								BranchPtr = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( CompNum ).BranchNumPtr;
								CompPtr = PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( CompNum ).CompNumPtr;

								if ( PlantLoop( LoopPtr ).LoopSide( LoopSidePtr ).Branch( BranchPtr ).Comp( CompPtr ).CurOpSchemeType != PumpOpSchemeType ) {
									PlantLoop( LoopPtr ).LoopSide( LoopSidePtr ).Branch( BranchPtr ).Comp( CompPtr ).CurOpSchemeType = PlantLoop( LoopNum ).OpScheme( OpNum ).OpSchemeType;
								} else {
									ShowSevereError( "Invalid [pump] component found on equipment list.  Pumps are not allowed on equipment lists." );
									ShowContinueError( "Problem component name = " + PlantLoop( LoopNum ).OpScheme( OpNum ).EquipList( ListNum ).Comp( CompNum ).Name );
									ShowContinueError( "Remove pump component and place other plant equipment on the list to correct." );
									errFlag2 = true;
								}

								for ( CompOpNum = 1; CompOpNum <= PlantLoop( LoopPtr ).LoopSide( LoopSidePtr ).Branch( BranchPtr ).Comp( CompPtr ).NumOpSchemes; ++CompOpNum ) {
									if ( PlantLoop( LoopPtr ).LoopSide( LoopSidePtr ).Branch( BranchPtr ).Comp( CompPtr ).OpScheme( CompOpNum ).OpSchemePtr == OpNum ) {
										PlantLoop( LoopPtr ).LoopSide( LoopSidePtr ).Branch( BranchPtr ).Comp( CompPtr ).CurCompLevelOpNum = CompOpNum;
									}
								}
							}
						}
					} else {
						PlantLoop( LoopNum ).OpScheme( OpNum ).Available = false;
					}

				}
				//    IF(.NOT. foundscheme)THEN
				//      !'call warning 'no current control scheme specified.  Loop Equipment will be shut down'
				//    ENDIF
			}

		}

		if ( errFlag2 ) {
			ShowFatalError( "InitLoadDistribution: Fatal error caused by previous severe error(s)." );
		}

	}

	// End Initialization Section of the Plant Loop Module
	//******************************************************************************

	// Begin Load Calculation/Distribution Section of the Plant Loop Module
	//******************************************************************************

	void
	DistributePlantLoad(
		int const LoopNum,
		int const LoopSideNum,
		int const CurSchemePtr, // use as index in PlantLoop()OpScheme() data structure
		int const ListPtr, // use as index in PlantLoop()OpScheme() data structure
		Real64 const LoopDemand,
		Real64 & RemLoopDemand
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   July 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  July 2010
		//                      Sept 2010 B. Griffith, retain actual sign of load values

		// PURPOSE OF THIS SUBROUTINE: This subroutine distributes the load
		// to plant equipment according to one of two distribution schemes:
		//     OPTIMAL    = 1
		//     SEQUENTIAL = 2
		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// Using/Aliasing
		using namespace DataLoopNode;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Real64 ChangeInLoad;
		Real64 DivideLoad;
		Real64 UniformLoad;
		Real64 NewLoad;
		int LoadFlag;

		int BranchNum;
		int CompNum;
		int CompIndex;
		//  INTEGER                  :: EquipNum
		int NumCompsOnList;

		// load local variables
		NumCompsOnList = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).NumComps;
		RemLoopDemand = LoopDemand;
		if ( NumCompsOnList <= 0 ) return;
		//set flag to specify optimal or sequential loading of equipment
		LoadFlag = PlantLoop( LoopNum ).LoadDistribution;

		if ( std::abs( RemLoopDemand ) < SmallLoad ) {
			//no load to distribute
		} else {

			{ auto const SELECT_CASE_var( LoadFlag );
			if ( SELECT_CASE_var == OptimalLoading ) { // LoadFlag=1 indicates "optimal" load distribution
				//OPTIMAL DISTRIBUTION SCHEME
				//step 1: load all machines to optimal PLR
				for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
					BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
					CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
					if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;

					if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OptLoad > 0.0 ) {
						ChangeInLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OptLoad, std::abs( RemLoopDemand ) );
					} else {
						// this is for some components like cooling towers don't have well defined OptLoad
						ChangeInLoad = std::abs( RemLoopDemand );
					}

					AdjustChangeInLoadForLastStageUpperRangeLimit( LoopNum, CurSchemePtr, ListPtr, ChangeInLoad );

					AdjustChangeInLoadByEMSControls( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );

					AdjustChangeInLoadByHowServed( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );

					ChangeInLoad = max( 0.0, ChangeInLoad );
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( ChangeInLoad, RemLoopDemand );

					RemLoopDemand -= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad;
					if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0; //CR8631 don't just exit or %MyLoad on second device isn't reset
				}

				//step 2: Evenly distribute remaining loop demand
				if ( std::abs( RemLoopDemand ) > SmallLoad ) {
					DivideLoad = std::abs( RemLoopDemand ) / NumCompsOnList;
					for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
						BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
						CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
						if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;
						NewLoad = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad;
						NewLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad, std::abs( NewLoad ) + DivideLoad );
						ChangeInLoad = NewLoad - std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( NewLoad, RemLoopDemand );
						RemLoopDemand -= sign( ChangeInLoad, RemLoopDemand );
						if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0; //CR8631 don't just exit or %MyLoad on second device isn't reset
					}
				}

				// step 3: If RemLoopDemand is still greater than zero, look for any machine
				if ( std::abs( RemLoopDemand ) > SmallLoad ) {
					for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
						BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
						CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
						if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;
						DivideLoad = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad - std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
						ChangeInLoad = min( std::abs( RemLoopDemand ), DivideLoad );
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad += sign( ChangeInLoad, RemLoopDemand );
						RemLoopDemand -= sign( ChangeInLoad, RemLoopDemand );
						if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0; //CR8631 don't just exit or %MyLoad on second device isn't reset
					}
				}

				//SEQUENTIAL DISTRIBUTION SCHEME
			} else if ( SELECT_CASE_var == SequentialLoading ) { // LoadFlag=2 indicates "sequential" load distribution

				// step 1: Load machines in list order
				for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
					BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
					CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
					if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;

					if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad > 0.0 ) { // apply known limit
						ChangeInLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad, std::abs( RemLoopDemand ) );
					} else {
						// this is for some components like cooling towers don't have well defined MaxLoad
						ChangeInLoad = std::abs( RemLoopDemand );
					}

					AdjustChangeInLoadForLastStageUpperRangeLimit( LoopNum, CurSchemePtr, ListPtr, ChangeInLoad );

					AdjustChangeInLoadByEMSControls( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );

					AdjustChangeInLoadByHowServed( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );

					ChangeInLoad = max( 0.0, ChangeInLoad );
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( ChangeInLoad, RemLoopDemand );
					RemLoopDemand -= sign( ChangeInLoad, RemLoopDemand );
					if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0; //CR8631 don't just exit or %MyLoad on second device isn't reset
				}

				//UNIFORM DISTRIBUTION SCHEME
			} else if ( SELECT_CASE_var == UniformLoading ) { // LoadFlag=3 indicates "uniform" load distribution

				// step 1: distribute load equally to all machines
				UniformLoad = std::abs( RemLoopDemand ) / NumCompsOnList;
				for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
					BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
					CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
					if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;
					if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad > 0.0 ) {
						ChangeInLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad, UniformLoad );
					} else {
						// this is for some components like cooling towers don't have well defined MaxLoad
						ChangeInLoad = std::abs( RemLoopDemand );
					}

					AdjustChangeInLoadForLastStageUpperRangeLimit( LoopNum, CurSchemePtr, ListPtr, ChangeInLoad );

					AdjustChangeInLoadByEMSControls( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );

					AdjustChangeInLoadByHowServed( LoopNum, LoopSideNum, BranchNum, CompNum, ChangeInLoad );
					ChangeInLoad = max( 0.0, ChangeInLoad );
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( ChangeInLoad, RemLoopDemand );
					RemLoopDemand -= sign( ChangeInLoad, RemLoopDemand );
					if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0;
				}

				// step 2: If RemLoopDemand is not zero, then distribute remainder sequentially.
				if ( std::abs( RemLoopDemand ) > SmallLoad ) {
					for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
						BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
						CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
						if ( ! PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available ) continue;
						ChangeInLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad - std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ), std::abs( RemLoopDemand ) );
						ChangeInLoad = max( 0.0, ChangeInLoad );
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad += sign( ChangeInLoad, RemLoopDemand );
						RemLoopDemand -= sign( ChangeInLoad, RemLoopDemand );
						if ( std::abs( RemLoopDemand ) < SmallLoad ) RemLoopDemand = 0.0;
					}
				}
			}}

		} // load is small check

		// now update On flags according to result for MyLoad
		for ( CompIndex = 1; CompIndex <= NumCompsOnList; ++CompIndex ) {
			BranchNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).BranchNumPtr;
			CompNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( ListPtr ).Comp( CompIndex ).CompNumPtr;
			if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) < SmallLoad ) {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
			} else {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
			}

		}

	}

	void
	AdjustChangeInLoadForLastStageUpperRangeLimit(
		int const LoopNum, // component topology
		int const CurOpSchemePtr, // currect active operation scheme
		int const CurEquipListPtr, // current equipment list
		Real64 & ChangeInLoad // positive magnitude of load change
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         B. Griffith
		//       DATE WRITTEN   May 2012
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// if this is the last stage for a load based operation, then limit load to upper range

		// METHODOLOGY EMPLOYED:
		// <description>

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Real64 RangeHiLimit;

		if ( PlantLoop( LoopNum ).OpScheme( CurOpSchemePtr ).EquipListNumForLastStage == CurEquipListPtr ) { // at final last stage

			RangeHiLimit = PlantLoop( LoopNum ).OpScheme( CurOpSchemePtr ).EquipList( CurEquipListPtr ).RangeUpperLimit;
			ChangeInLoad = min( ChangeInLoad, RangeHiLimit );
		}

	}

	void
	AdjustChangeInLoadByHowServed(
		int const LoopNum, // component topology
		int const LoopSideNum, // component topology
		int const BranchNum, // component topology
		int const CompNum, // component topology
		Real64 & ChangeInLoad // positive magnitude of load change
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         B. Griffith
		//       DATE WRITTEN   Nov 2011
		//       MODIFIED       March 2012, B. Griffith add controls for free cooling heat exchanger overrides of chillers
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// central place to apply limits to machine load dispatch based on how the machine serves loads

		// METHODOLOGY EMPLOYED:
		// Components are machines on plant equipment operation lists.  Need to make adjustments to the
		// load dispatch to account for limits and floating capacities.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		//  USE EconomizerHeatExchanger,  ONLY: GetEconHeatExchangerCurrentCapacity
		// Using/Aliasing
		using DataLoopNode::Node;
		using DataEnvironment::OutDryBulbTemp;
		using DataEnvironment::OutWetBulbTemp;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		static Real64 CurMassFlowRate( 0.0 );
		static Real64 ToutLowLimit( 0.0 );
		static Real64 ToutHiLimit( 0.0 );
		static Real64 TinLowLimit( 0.0 );
		static Real64 Tinlet( 0.0 );
		static Real64 Tsensor( 0.0 );
		static Real64 CurSpecHeat( 0.0 );
		static Real64 QdotTmp( 0.0 );
		static int ControlNodeNum( 0 );

		//start of bad band-aid, need a general and comprehensive approach for determining current capacity of all kinds of equipment
		// Need to truncate the load down in case outlet temperature will hit a lower/upper limit
		{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).HowLoadServed );

		//Chillers
		if ( SELECT_CASE_var == HowMet_ByNominalCapLowOutLimit ) { // chillers with lower limit on outlet temperature

			//- Retrieve data from the plant loop data structure
			CurMassFlowRate = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).MassFlowRate;
			ToutLowLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MinOutletTemp;
			Tinlet = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).Temp;
			CurSpecHeat = GetSpecificHeatGlycol( PlantLoop( LoopNum ).FluidName, Tinlet, PlantLoop( LoopNum ).FluidIndex, "PlantCondLoopOperation:DistributePlantLoad" );
			QdotTmp = CurMassFlowRate * CurSpecHeat * ( Tinlet - ToutLowLimit );

			//        !- Don't correct if Q is zero, as this could indicate a component which this hasn't been implemented or not yet turned on
			if ( CurMassFlowRate > 0.0 ) {
				ChangeInLoad = min( ChangeInLoad, QdotTmp );
			}

		} else if ( SELECT_CASE_var == HowMet_ByNominalCapFreeCoolCntrl ) {
			// for chillers with free cooling shutdown (HeatExchanger:Hydronic currently)
			// determine if free cooling controls shut off chiller
			TinLowLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlMinCntrlTemp;
			{ auto const SELECT_CASE_var1( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlMode );
			if ( SELECT_CASE_var1 == FreeCoolControlMode_WetBulb ) {
				Tsensor = OutWetBulbTemp;
			} else if ( SELECT_CASE_var1 == FreeCoolControlMode_DryBulb ) {
				Tsensor = OutDryBulbTemp;
			} else if ( SELECT_CASE_var1 == FreeCoolControlMode_Loop ) {
				ControlNodeNum = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlNodeNum;
				if ( ControlNodeNum > 0 ) {
					Tsensor = Node( ControlNodeNum ).TempLastTimestep; // use lagged value for stability
				} else {
					Tsensor = 23.;
				}
			}}

			if ( Tsensor < TinLowLimit ) { // turn off chiller to initiate free cooling
				ChangeInLoad = 0.0;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = false;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlShutDown = true;
			} else {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = true;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlShutDown = false;
			}

		} else if ( SELECT_CASE_var == HowMet_ByNominalCapLowOutLimitFreeCoolCntrl ) {
			// for chillers with free cooling shutdown (HeatExchanger:Hydronic currently)
			// determine if free cooling controls shut off chiller
			TinLowLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlMinCntrlTemp;
			{ auto const SELECT_CASE_var1( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlMode );
			if ( SELECT_CASE_var1 == FreeCoolControlMode_WetBulb ) {
				Tsensor = OutWetBulbTemp;
			} else if ( SELECT_CASE_var1 == FreeCoolControlMode_DryBulb ) {
				Tsensor = OutDryBulbTemp;
			} else if ( SELECT_CASE_var1 == FreeCoolControlMode_Loop ) {
				ControlNodeNum = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlNodeNum;
				if ( ControlNodeNum > 0 ) {
					Tsensor = Node( ControlNodeNum ).TempLastTimestep; // use lagged value for stability
				} else {
					Tsensor = 23.;
				}
			}}

			if ( Tsensor < TinLowLimit ) { // turn off chiller to initiate free cooling
				ChangeInLoad = 0.0;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = false;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlShutDown = true;
			} else {
				//- Retrieve data from the plant loop data structure
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = true;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).FreeCoolCntrlShutDown = false;
				CurMassFlowRate = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).MassFlowRate;
				ToutLowLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MinOutletTemp;
				Tinlet = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).Temp;
				CurSpecHeat = GetSpecificHeatGlycol( PlantLoop( LoopNum ).FluidName, Tinlet, PlantLoop( LoopNum ).FluidIndex, "PlantCondLoopOperation:DistributePlantLoad" );
				QdotTmp = CurMassFlowRate * CurSpecHeat * ( Tinlet - ToutLowLimit );

				//        !- Don't correct if Q is zero, as this could indicate a component which this hasn't been implemented or not yet turned on
				if ( CurMassFlowRate > 0.0 ) {
					ChangeInLoad = min( ChangeInLoad, QdotTmp );
				}
			}

		} else if ( SELECT_CASE_var == HowMet_ByNominalCapHiOutLimit ) { // boilers with upper limit on outlet temperature
			//- Retrieve data from the plant loop data structure
			CurMassFlowRate = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).MassFlowRate;
			ToutHiLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxOutletTemp;
			Tinlet = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).Temp;
			CurSpecHeat = GetSpecificHeatGlycol( PlantLoop( LoopNum ).FluidName, Tinlet, PlantLoop( LoopNum ).FluidIndex, "PlantCondLoopOperation:DistributePlantLoad" );
			QdotTmp = CurMassFlowRate * CurSpecHeat * ( ToutHiLimit - Tinlet );

			if ( CurMassFlowRate > 0.0 ) {
				ChangeInLoad = min( ChangeInLoad, QdotTmp );
			}

		} else if ( SELECT_CASE_var == HowMet_PassiveCap ) { // need to estimate current capacity if more or less passive devices ??

		} else {

		}}

	}

	void
	FindCompSPLoad(
		int const LoopNum,
		int const LoopSideNum,
		int const BranchNum,
		int const CompNum,
		int const OpNum // index for Plant()%LoopSide()%Branch()%Comp()%OpScheme()
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Sankaranarayanan K P
		//       DATE WRITTEN   Jan 2005
		//       MODIFIED       na
		//       RE-ENGINEERED  Dan Fisher July 2010

		// PURPOSE OF THIS SUBROUTINE:
		// To calculate the load on a component controlled by
		// Component SetPoint based scheme.

		// Using/Aliasing
		using DataLoopNode::Node;
		using DataLoopNode::SensedNodeFlagValue;
		using FluidProperties::GetDensityGlycol;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Real64 CompDemand;
		Real64 DemandMdot;
		Real64 ActualMdot;
		Real64 TempIn;
		Real64 CurSpecHeat;
		Real64 TempSetPt;
		Real64 CompMinLoad;
		Real64 CompMaxLoad;
		Real64 CompOptLoad;
		int DemandNode;
		int CompPtr;
		int OpSchemePtr;
		int ListPtr;
		int SetPtNode;
		int NumEquipLists;
		Real64 rho;
		Real64 CurrentDemandForCoolingOp;
		Real64 CurrentDemandForHeatingOp;

		//find the pointer to the 'PlantLoop()%OpScheme()'...data structure
		NumEquipLists = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( OpNum ).NumEquipLists;
		if ( NumEquipLists != 1 ) {
			//CALL Severe error) there should be exactly one list associated with component setpoint scheme
		}

		OpSchemePtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( OpNum ).OpSchemePtr;
		ListPtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( OpNum ).EquipList( 1 ).ListPtr;
		CompPtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( OpNum ).EquipList( 1 ).CompPtr;

		//load local variables from the data structures
		CompMinLoad = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MinLoad;
		CompMaxLoad = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad;
		CompOptLoad = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OptLoad;
		DemandMdot = PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).SetPointFlowRate;
		DemandNode = PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).DemandNodeNum;
		SetPtNode = PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).SetPointNodeNum;
		TempIn = Node( DemandNode ).Temp;
		rho = GetDensityGlycol( PlantLoop( LoopNum ).FluidName, TempIn, PlantLoop( LoopNum ).FluidIndex, "FindCompSPLoad" );

		DemandMdot = PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).SetPointFlowRate * rho;
		//DSU?  DemandMDot is a constant design flow rate, next based on actual current flow rate for accurate current demand?
		ActualMdot = Node( DemandNode ).MassFlowRate;
		CurSpecHeat = GetSpecificHeatGlycol( PlantLoop( LoopNum ).FluidName, TempIn, PlantLoop( LoopNum ).FluidIndex, "FindCompSPLoad" );
		if ( ( ActualMdot > 0.0 ) && ( ActualMdot != DemandMdot ) ) {
			DemandMdot = ActualMdot;
		}

		{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).LoopDemandCalcScheme );
		if ( SELECT_CASE_var == SingleSetPoint ) {
			TempSetPt = Node( SetPtNode ).TempSetPoint;
		} else if ( SELECT_CASE_var == DualSetPointDeadBand ) {
			if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == CoolingOp ) {
				TempSetPt = Node( SetPtNode ).TempSetPointHi;
			} else if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == HeatingOp ) {
				TempSetPt = Node( SetPtNode ).TempSetPointLo;
			} else if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == DualOp ) {
				CurrentDemandForCoolingOp = DemandMdot * CurSpecHeat * ( Node( SetPtNode ).TempSetPointHi - TempIn );
				CurrentDemandForHeatingOp = DemandMdot * CurSpecHeat * ( Node( SetPtNode ).TempSetPointLo - TempIn );
				if ( ( CurrentDemandForCoolingOp < 0.0 ) && ( CurrentDemandForHeatingOp <= 0.0 ) ) { // cooling
					TempSetPt = Node( SetPtNode ).TempSetPointHi;
				} else if ( ( CurrentDemandForCoolingOp >= 0.0 ) && ( CurrentDemandForHeatingOp > 0.0 ) ) { // heating
					TempSetPt = Node( SetPtNode ).TempSetPointLo;
				} else { // deadband
					TempSetPt = TempIn;
				}

			}

		}}

		if ( TempSetPt == SensedNodeFlagValue ) {
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EquipDemand = 0.0;
		} else {

			CompDemand = ( DemandMdot * CurSpecHeat * ( TempSetPt - TempIn ) );

			if ( std::abs( CompDemand ) < LoopDemandTol ) CompDemand = 0.0;
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EquipDemand = CompDemand;

			//set MyLoad and runflag
			if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == CoolingOp ) {
				if ( CompDemand < ( -LoopDemandTol ) ) {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = CompDemand;
				} else {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				}
			} else if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == HeatingOp ) {
				if ( CompDemand > LoopDemandTol ) {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = CompDemand;
				} else {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				}
			} else if ( PlantLoop( LoopNum ).OpScheme( OpSchemePtr ).EquipList( ListPtr ).Comp( CompPtr ).CtrlTypeNum == DualOp ) {
				if ( CompDemand > LoopDemandTol || CompDemand < ( -LoopDemandTol ) ) {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = CompDemand;
				} else {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				}
			}

			//Check bounds on MyLoad
			if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) > CompMaxLoad ) {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( CompMaxLoad, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
			}
			//   PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MyLoad = &
			//   MIN(PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MyLoad,CompMaxLoad)

			if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) < CompMinLoad ) {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( CompMinLoad, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
			}
			//   PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MyLoad = &
			//   MAX(PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MyLoad,CompMinLoad)

		} //valid setpoint (TempSetPt /= SensedNodeFlagValue)
	}

	void
	DistributeUserDefinedPlantLoad(
		int const LoopNum,
		int const LoopSideNum,
		int const BranchNum,
		int const CompNum,
		int const CurCompLevelOpNum, // index for Plant()%LoopSide()%Branch()%Comp()%OpScheme()
		int const CurSchemePtr,
		Real64 const LoopDemand,
		Real64 & RemLoopDemand
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         B. Griffith
		//       DATE WRITTEN   August 2013
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// <description>

		// METHODOLOGY EMPLOYED:
		// <description>

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::emsCallFromUserDefinedComponentModel;
		using EMSManager::ManageEMS;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int CompPtr;
		int ListPtr;

		// ListPtr = PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%OpScheme(CurCompLevelOpNum)%EquipList(1)%ListPtr
		CompPtr = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).OpScheme( CurCompLevelOpNum ).EquipList( 1 ).CompPtr;

		// fill internal variable
		PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( 1 ).Comp( CompPtr ).EMSIntVarRemainingLoadValue = LoopDemand;

		// Call EMS program(s)
		if ( PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).ErlSimProgramMngr > 0 ) {
			ManageEMS( emsCallFromUserDefinedComponentModel, PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).ErlSimProgramMngr );
		}

		// move actuated value to MyLoad

		PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( 1 ).Comp( CompPtr ).EMSActuatorDispatchedLoadValue;
		PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EquipDemand = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).EquipList( 1 ).Comp( CompPtr ).EMSActuatorDispatchedLoadValue;
		if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) > LoopDemandTol ) {
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;

		} else {
			PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
		}

	}

	// End Load Calculation/Distribution Section of the Plant Loop Module
	//******************************************************************************

	//********************************

	Real64
	FindRangeVariable(
		int const LoopNum, // PlantLoop data structure loop counter
		int const CurSchemePtr, // set by PL()%LoopSide()%Branch()%Comp()%OpScheme()%OpSchemePtr
		int const CurSchemeType // identifier set in PlantData
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Sankaranarayanan K P
		//       DATE WRITTEN   Jan 2004
		//       MODIFIED       Chandan Sharma, August 2010
		//       RE-ENGINEERED  na

		// Using/Aliasing
		using namespace DataLoopNode;
		using DataEnvironment::OutWetBulbTemp;
		using DataEnvironment::OutDryBulbTemp;
		using DataEnvironment::OutDewPointTemp;

		// Return value
		Real64 FindRangeVariable;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// used to locate data in PL()%OpScheme(CurSchemePtr)
		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS
		// na

		// DERIVED TYPE DEFINITIONS
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int ReferenceNodeNum;
		Real64 NodeTemperature;

		OperationScheme: { auto const SELECT_CASE_var( CurSchemeType );

		if ( SELECT_CASE_var == DryBulbTDBOpSchemeType ) { // drybulb temp based controls
			ReferenceNodeNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).ReferenceNodeNumber;
			NodeTemperature = Node( ReferenceNodeNum ).Temp;
			FindRangeVariable = NodeTemperature - OutDryBulbTemp;
		} else if ( SELECT_CASE_var == WetBulbTDBOpSchemeType ) { // wetbulb temp based controls
			ReferenceNodeNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).ReferenceNodeNumber;
			NodeTemperature = Node( ReferenceNodeNum ).Temp;
			FindRangeVariable = NodeTemperature - OutWetBulbTemp;
		} else if ( SELECT_CASE_var == DewPointTDBOpSchemeType ) { // dewpoint temp based controls
			ReferenceNodeNum = PlantLoop( LoopNum ).OpScheme( CurSchemePtr ).ReferenceNodeNumber;
			NodeTemperature = Node( ReferenceNodeNum ).Temp;
			FindRangeVariable = NodeTemperature - OutDewPointTemp;
		}} // OperationScheme
		//Autodesk:Return Check/enforce that one of these CASEs holds or add a default case to assure return value is set

		return FindRangeVariable;

	}

	//********************************

	// Begin Plant Loop ON/OFF Utility Subroutines
	//******************************************************************************

	void
	TurnOnPlantLoopPipes(
		int const LoopNum,
		int const LoopSideNum
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         Dan Fisher
		//       DATE WRITTEN   July 1998
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE: This subroutine sets a logical flag
		// for the loop circulation pump to TRUE.

		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int MachineOnLoopNum;
		int Num;

		for ( Num = 1; Num <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++Num ) {
			for ( MachineOnLoopNum = 1; MachineOnLoopNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).TotalComponents; ++MachineOnLoopNum ) {
				{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnLoopNum ).TypeOf_Num );
				if ( ( SELECT_CASE_var == TypeOf_Pipe ) || ( SELECT_CASE_var == TypeOf_PipeInterior ) || ( SELECT_CASE_var == TypeOf_PipeExterior ) || ( SELECT_CASE_var == TypeOf_PipeUnderground ) ) {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnLoopNum ).ON = true;
				} else {
					//Don't do anything
				}}
			}
		}

	}

	void
	TurnOffLoopEquipment( int const LoopNum )
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         D.E. Fisher
		//       DATE WRITTEN   July 1998
		//       MODIFIED       D.E. Fisher, Aug. 2010
		//       RE-ENGINEERED

		// PURPOSE OF THIS SUBROUTINE:
		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int MachineOnBranch;
		int LoopSideNum;
		int Num;

		for ( LoopSideNum = 1; LoopSideNum <= 2; ++LoopSideNum ) {
			for ( Num = 1; Num <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++Num ) {
				for ( MachineOnBranch = 1; MachineOnBranch <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).TotalComponents; ++MachineOnBranch ) {
					//Sankar Non Integrated Economizer
					if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).GeneralEquipType != GenEquipTypes_Pump ) {
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).ON = false;
						PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).MyLoad = 0.0;
					}
				}
			}
		}

	}

	void
	TurnOffLoopSideEquipment(
		int const LoopNum,
		int const LoopSideNum
	)
	{
		// SUBROUTINE INFORMATION:
		//       AUTHOR         D.E. Fisher
		//       DATE WRITTEN   July 1998
		//       MODIFIED       D.E. Fisher, Aug. 2010
		//       RE-ENGINEERED

		// PURPOSE OF THIS SUBROUTINE:
		// METHODOLOGY EMPLOYED:
		// na
		// REFERENCES:
		// na
		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na
		// INTERFACE BLOCK SPECIFICATIONS
		// na
		// DERIVED TYPE DEFINITIONS
		// na
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int MachineOnBranch;
		int Num;

		for ( Num = 1; Num <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++Num ) {
			for ( MachineOnBranch = 1; MachineOnBranch <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).TotalComponents; ++MachineOnBranch ) {
				//Sankar Non Integrated Economizer
				if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).GeneralEquipType != GenEquipTypes_Pump ) {
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).ON = false;
					PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( Num ).Comp( MachineOnBranch ).MyLoad = 0.0;
				}
			}
		}

	}

	// End Plant Loop ON/OFF Utility Subroutines
	//******************************************************************************

	// Begin Plant EMS Control Routines
	//******************************************************************************

	void
	SetupPlantEMSActuators()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         D.E. Fisher
		//       DATE WRITTEN   Feb 2007
		//       MODIFIED       B. Griffith August 2009, D. Fisher, Aug. 2010
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine loads the plant EMS actuators

		// METHODOLOGY EMPLOYED:
		// Call the setupAcuator routine

		// REFERENCES:
		// na

		// USE STATEMENTS:

		// Using/Aliasing
		// SUBROUTINE ARGUMENT DEFINITIONS

		// SUBROUTINE PARAMETER DEFINITIONS
		// na

		// SUBROUTINE VARIABLE DEFINITIONS

		// Locals
		std::string ActuatorType;
		std::string ActuatorName;
		std::string UniqueIDName;
		static std::string Units( "[on/off]" );
		// INTEGER                      :: NumAct
		int LoopNum;
		int LoopSideNum;
		int BranchNum;
		int CompNum;

		for ( LoopNum = 1; LoopNum <= TotNumLoops; ++LoopNum ) {
			ActuatorName = "Plant Loop Overall";
			UniqueIDName = PlantLoop( LoopNum ).Name;
			ActuatorType = "On/Off Supervisory";
			SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, Units, PlantLoop( LoopNum ).EMSCtrl, PlantLoop( LoopNum ).EMSValue );

			ActuatorName = "Supply Side Half Loop";
			UniqueIDName = PlantLoop( LoopNum ).Name;
			ActuatorType = "On/Off Supervisory";
			SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, Units, PlantLoop( LoopNum ).LoopSide( SupplySide ).EMSCtrl, PlantLoop( LoopNum ).LoopSide( SupplySide ).EMSValue );

			ActuatorName = "Demand Side Half Loop";
			UniqueIDName = PlantLoop( LoopNum ).Name;
			ActuatorType = "On/Off Supervisory";
			SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, Units, PlantLoop( LoopNum ).LoopSide( DemandSide ).EMSCtrl, PlantLoop( LoopNum ).LoopSide( DemandSide ).EMSValue );

			for ( LoopSideNum = 1; LoopSideNum <= 2; ++LoopSideNum ) {
				for ( BranchNum = 1; BranchNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).TotalBranches; ++BranchNum ) {
					if ( LoopSideNum == SupplySide ) {
						ActuatorName = "Supply Side Branch";
						UniqueIDName = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Name;
						ActuatorType = "On/Off Supervisory";
						SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, Units, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideOn, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideValue );
					} else if ( LoopSideNum == DemandSide ) {
						ActuatorName = "Demand Side Branch";
						UniqueIDName = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Name;
						ActuatorType = "On/Off Supervisory";
						SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, Units, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideOn, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideValue );
					}
					for ( CompNum = 1; CompNum <= PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).TotalComponents; ++CompNum ) {
						ActuatorName = "Plant Component " + ccSimPlantEquipTypes( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).TypeOf_Num );
						UniqueIDName = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Name;
						ActuatorType = "On/Off Supervisory";
						SetupEMSActuator( ActuatorName, UniqueIDName, ActuatorType, "[W]", PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideOn, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideValue );
					}
				}
			}
		}

	}

	void
	ActivateEMSControls(
		int const LoopNum,
		int const LoopSideNum,
		int const BranchNum,
		int const CompNum,
		bool & LoopShutDownFlag
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         D.E. Fisher
		//       DATE WRITTEN   Feb 2007
		//       MODIFIED
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// This subroutine loads the plant EMS actuators

		// METHODOLOGY EMPLOYED: The EMS flags are evaluated in hierarchical order:
		//     LOOP flags override branch and component flags
		//     BRANCH flags override component flags
		// If the loop flag (EMSCtrl) is true, then
		//     IF EMSValue <= 0, shut down the entire loop including the pumps
		//     IF EMSValue > 0, no action
		// If the LoopSide flag (EMSCtrl) is true, then:
		//     IF EMSValue <=0, shut down all components on the LoopSide except the pumps
		//     IF EMSValue > 0, no action
		// If a component flag (EMSCtrl) is true, then:
		//     EMSValue <=0, shut down the component
		//     EMSValue > 0, calc. component load: MyLoad=MIN(MaxCompLoad,MaxCompLoad*EMSValue)

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataLoopNode;

		// SUBROUTINE ARGUMENT DEFINITIONS

		// Locals
		// SUBROUTINE PARAMETER DEFINITIONS
		// na

		// SUBROUTINE VARIABLE DEFINITIONS
		Real64 CurMassFlowRate;
		Real64 ToutLowLimit;
		Real64 Tinlet;
		Real64 CurSpecHeat;
		Real64 QTemporary;
		//unused REAL(r64)                  :: ChangeInLoad

		//MODULE VARIABLE DECLARATIONS:

		//Loop Control
		if ( PlantLoop( LoopNum ).EMSCtrl ) {
			if ( PlantLoop( LoopNum ).EMSValue <= 0.0 ) {
				LoopShutDownFlag = true;
				TurnOffLoopEquipment( LoopNum );
				return;
			} else {
				LoopShutDownFlag = false;
			}
		} else {
			LoopShutDownFlag = false;
		}

		//Half-loop control
		if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).EMSCtrl ) {
			if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).EMSValue <= 0.0 ) {
				TurnOffLoopSideEquipment( LoopNum, LoopSideNum );
				return;
			} else {
				//do nothing:  can't turn all LoopSide equip. ON with loop switch
			}
		}

		if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideOn ) {
			//EMSValue <= 0 turn component OFF
			if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideValue <= 0.0 ) {
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = false;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = false;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = 0.0;
				return;
			} else {
				//EMSValue > 0 Set Component Load and Turn component ON
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).ON = true;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).Available = false;
				PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = min( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad, ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad * PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideValue ) );

				// Check lower/upper temperature limit for chillers
				{ auto const SELECT_CASE_var( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).TypeOf_Num );
				if ( ( SELECT_CASE_var == TypeOf_Chiller_ElectricEIR ) || ( SELECT_CASE_var == TypeOf_Chiller_Electric ) || ( SELECT_CASE_var == TypeOf_Chiller_ElectricReformEIR ) ) {

					//- Retrieve data from the plant loop data structure
					CurMassFlowRate = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).MassFlowRate;
					ToutLowLimit = PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MinOutletTemp;
					Tinlet = Node( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).NodeNumIn ).Temp;
					CurSpecHeat = GetSpecificHeatGlycol( PlantLoop( LoopNum ).FluidName, Tinlet, PlantLoop( LoopNum ).FluidIndex, "ActivateEMSControls" );
					QTemporary = CurMassFlowRate * CurSpecHeat * ( Tinlet - ToutLowLimit );

					//- Don't correct if Q is zero, as this could indicate a component which this hasn't been implemented
					if ( QTemporary > 0.0 ) {

						//unused               ChangeInLoad = MIN(ChangeInLoad,QTemporary)
						// DSU?  weird ems thing here?
						if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) > PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad ) {
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MaxLoad, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
						}
						if ( std::abs( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad ) > QTemporary ) {
							PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad = sign( QTemporary, PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).MyLoad );
						}

						//               PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MyLoad = &
						//               MIN((PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%MaxLoad * &
						//                  PlantLoop(LoopNum)%LoopSide(LoopSideNum)%Branch(BranchNum)%Comp(CompNum)%EMSValue),Qtemporary)
					}
				} else {
					//Nothing Changes for now, could add in case statements for boilers, which would use upper limit temp check
				}}
				return;
			} //EMSValue <=> 0
		} //EMSFlag

	}

	void
	AdjustChangeInLoadByEMSControls(
		int const LoopNum,
		int const LoopSideNum,
		int const BranchNum,
		int const CompNum,
		Real64 & ChangeInLoad // positive magnitude of load change
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         B. Griffith
		//       DATE WRITTEN   April 2012
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		// modify load dispatch if EMS controls are in place for a specific component

		// METHODOLOGY EMPLOYED:
		// Check if Loop Side is shutdown
		//  then check if branch is shutdown
		// then  check if component is overridden and use the value if it is.
		// take ABS() of EMS value to ensure sign is correct.

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:

		if ( ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).EMSCtrl ) && ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).EMSValue <= 0.0 ) ) {
			ChangeInLoad = 0.0;
			return;
		}

		if ( ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideOn ) && ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).EMSCtrlOverrideValue <= 0.0 ) ) {
			ChangeInLoad = 0.0;
			return;
		}

		if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideOn ) {
			if ( PlantLoop( LoopNum ).LoopSide( LoopSideNum ).Branch( BranchNum ).Comp( CompNum ).EMSLoadOverrideValue == 0.0 ) {
				ChangeInLoad = 0.0;
			}
		}

	}

	//*END PLANT EMS CONTROL ROUTINES!
	//******************************************************************************

	//     NOTICE

	//     Copyright � 1996-2014 The Board of Trustees of the University of Illinois
	//     and The Regents of the University of California through Ernest Orlando Lawrence
	//     Berkeley National Laboratory.  All rights reserved.

	//     Portions of the EnergyPlus software package have been developed and copyrighted
	//     by other individuals, companies and institutions.  These portions have been
	//     incorporated into the EnergyPlus software package under license.   For a complete
	//     list of contributors, see "Notice" located in EnergyPlus.f90.

	//     NOTICE: The U.S. Government is granted for itself and others acting on its
	//     behalf a paid-up, nonexclusive, irrevocable, worldwide license in this data to
	//     reproduce, prepare derivative works, and perform publicly and display publicly.
	//     Beginning five (5) years after permission to assert copyright is granted,
	//     subject to two possible five year renewals, the U.S. Government is granted for
	//     itself and others acting on its behalf a paid-up, non-exclusive, irrevocable
	//     worldwide license in this data to reproduce, prepare derivative works,
	//     distribute copies to the public, perform publicly and display publicly, and to
	//     permit others to do so.

	//     TRADEMARKS: EnergyPlus is a trademark of the US Department of Energy.

} // PlantCondLoopOperation

} // EnergyPlus
