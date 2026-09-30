/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: OCLUpdate.h /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, August2002
// Desc:   Update Spits out an OCL on a timer
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/ProductionUpdate.h"

class ObjectCreationList;

//-------------------------------------------------------------------------------------------------
class OCLUpdateModuleData : public UpdateModuleData
{
public:

	struct FactionOCLInfo
	{
		std::string									m_factionName;
		const ObjectCreationList *	m_ocl;
	};

	typedef std::list<FactionOCLInfo> FactionOCLList;

	const ObjectCreationList *	m_ocl;
	FactionOCLList							m_factionOCL;
	UnsignedInt									m_minDelay;
	UnsignedInt									m_maxDelay;
	Bool												m_isCreateAtEdge;				///< Otherwise, it is created on top of myself
	Bool												m_isFactionTriggered;		///< Faction has to be present before update will happen

	OCLUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class OCLUpdate : public UpdateModule
#if !RETAIL_COMPATIBLE_CRC
	, public ProductionUpdateInterface
#endif
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( OCLUpdate, "OCLUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( OCLUpdate, OCLUpdateModuleData )

public:

	OCLUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override;

#if !RETAIL_COMPATIBLE_CRC
	virtual ProductionUpdateInterface* getProductionUpdateInterface() override;

	virtual CanMakeType canQueueCreateUnit( const ThingTemplate *unitType ) const override;
	virtual CanMakeType canQueueUpgrade( const UpgradeTemplate *upgrade ) const override;
	virtual ProductionID requestUniqueUnitID() override;
	virtual Bool queueUpgrade( const UpgradeTemplate *upgrade ) override { return FALSE; }
	virtual Bool cancelUpgrade( const UpgradeTemplate *upgrade ) override { return FALSE; }
	virtual Bool isUpgradeInQueue( const UpgradeTemplate *upgrade ) const override { return FALSE; }
	virtual UnsignedInt countUnitTypeInQueue( const ThingTemplate *unitType ) const override;
	virtual Bool toggleRepeatUnit( const ThingTemplate *unitType ) override { return FALSE; }
	virtual Bool isUnitInRepeatQueue( const ThingTemplate *unitType ) const override { return FALSE; }
	virtual Bool isNextRepeatUnit( const ThingTemplate *unitType ) const override { return FALSE; }
	virtual UnsignedInt getRepeatProductionCount() const override { return 0; }
	virtual Bool queueCreateUnit( const ThingTemplate *unitType, ProductionID productionID ) override;
	virtual Bool cancelUnitCreate( ProductionID productionID ) override;
	virtual void cancelAllUnitsOfType( const ThingTemplate *unitType ) override;
	virtual void cancelAndRefundAllProduction() override;
	virtual UnsignedInt getProductionCount() const override { return m_manifestCount; }
	virtual const ProductionEntry *firstProduction() const override { return m_manifestHead; }
	virtual const ProductionEntry *nextProduction( const ProductionEntry *p ) const override { return p ? p->m_next : nullptr; }
	virtual void setHoldDoorOpen( ExitDoorType exitDoor, Bool holdIt ) override {}
	virtual const CommandButton* getSpecialPowerConstructionCommandButton() const override { return nullptr; }
	virtual void setSpecialPowerConstructionCommandButton( const CommandButton *commandButton ) override {}
#endif

	Real getCountdownPercent() const; ///< goes from 0% to 100%
	UnsignedInt getRemainingFrames() const; ///< For feedback display
	void resetTimer(); ///< added for sabotage purposes.
	virtual DisabledMaskType getDisabledTypesToProcess() const override { return DISABLEDMASK_ALL; }

protected:

	UnsignedInt			m_nextCreationFrame;
	UnsignedInt			m_timerStartedFrame;
	Bool						m_isFactionNeutral;
	Color						m_currentPlayerColor;

	Bool shouldCreate();
	void setNextCreationFrame();

#if !RETAIL_COMPATIBLE_CRC
	Bool isSupplyDropZone() const;
	Bool isEligibleSupplyManifestUnit( const ThingTemplate *unitType ) const;
	Int getSupplyManifestCost() const;
	void appendManifestEntry( ProductionEntry *entry );
	void removeManifestEntry( ProductionEntry *entry );
	void clearSupplyManifest();
	Bool deliverSupplyManifest( const Coord3D& edgePoint );

	ProductionEntry *m_manifestHead;
	ProductionEntry *m_manifestTail;
	UnsignedInt m_manifestCount;
#endif

};
