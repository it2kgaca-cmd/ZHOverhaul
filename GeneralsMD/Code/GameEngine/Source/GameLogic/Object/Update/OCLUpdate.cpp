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

// FILE: OCLUpdate.cpp /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, August2002
// Desc:   Update Spits out an OCL on a timer
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "Common/BuildAssistant.h"
#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/UnicodeString.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/MoneyCrateCollide.h"
#include "GameLogic/Module/OCLUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/TerrainLogic.h"

//-------------------------------------------------------------------------------------------------
void parseFactionObjectCreationList( INI *ini, void *instance, void *store, const void *userData )
{
	OCLUpdateModuleData::FactionOCLInfo info;
	info.m_ocl = nullptr;

	const char *token = ini->getNextToken( ini->getSepsColon() );
	if ( stricmp(token, "Faction") == 0 )
	{
		token = ini->getNextToken( ini->getSepsColon() );
		info.m_factionName = token;
	}
	else
		throw INI_INVALID_DATA;


	token = ini->getNextToken( ini->getSepsColon() );
	if ( stricmp(token, "OCL") == 0 )
		ini->parseObjectCreationList( ini, instance, &info.m_ocl, nullptr );
	else
		throw INI_INVALID_DATA;

	// Insert the info into the ocl hashmap
	OCLUpdateModuleData::FactionOCLList * theList = (OCLUpdateModuleData::FactionOCLList*)store;
	theList->push_back(info);

}

//-------------------------------------------------------------------------------------------------
OCLUpdateModuleData::OCLUpdateModuleData()
{
	m_minDelay = 0;
	m_maxDelay = 0;
	m_ocl = nullptr;
	m_factionOCL.clear();
	m_isCreateAtEdge = FALSE;
	m_isFactionTriggered = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void OCLUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "OCL",					INI::parseObjectCreationList,		nullptr, offsetof( OCLUpdateModuleData, m_ocl ) },
		{ "FactionOCL",		parseFactionObjectCreationList,	nullptr, offsetof( OCLUpdateModuleData, m_factionOCL ) },
		{ "MinDelay",			INI::parseDurationUnsignedInt,	nullptr, offsetof( OCLUpdateModuleData, m_minDelay ) },
		{ "MaxDelay",			INI::parseDurationUnsignedInt,	nullptr, offsetof( OCLUpdateModuleData, m_maxDelay ) },
		{ "CreateAtEdge",	INI::parseBool,									nullptr, offsetof( OCLUpdateModuleData, m_isCreateAtEdge ) },
		{ "FactionTriggered",	INI::parseBool,							nullptr, offsetof( OCLUpdateModuleData, m_isFactionTriggered ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OCLUpdate::OCLUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_nextCreationFrame = 0;
	m_timerStartedFrame = 0;
	m_isFactionNeutral = TRUE;
	m_currentPlayerColor = 0;
#if !RETAIL_COMPATIBLE_CRC
	m_manifestHead = nullptr;
	m_manifestTail = nullptr;
	m_manifestCount = 0;
	m_manifestUniqueID = (ProductionID)1;
#endif
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OCLUpdate::~OCLUpdate()
{
#if !RETAIL_COMPATIBLE_CRC
	clearSupplyManifest();
#endif
}

#if !RETAIL_COMPATIBLE_CRC
static const Int SUPPLY_DROP_MANIFEST_BUDGET = 1500;
static const UnsignedInt SUPPLY_DROP_MANIFEST_MAX_ENTRIES = 9;

static Bool attachSupplyDropPayload( Object *transport, Object *payload, const char *containerTemplateName, const Coord3D& startPos )
{
	if( transport == nullptr || payload == nullptr )
		return FALSE;

	payload->setPosition( &startPos );
	payload->setProducer( transport );

	Object *outerPayload = payload;
	if( containerTemplateName != nullptr && containerTemplateName[0] != 0 )
	{
		const ThingTemplate *containerTemplate = TheThingFactory->findTemplate( containerTemplateName );
		if( containerTemplate == nullptr )
			return FALSE;

		Object *container = TheThingFactory->newObject( containerTemplate, transport->getTeam() );
		if( container == nullptr )
			return FALSE;

		container->setPosition( &startPos );
		container->setProducer( transport );
		if( container->getContain() == nullptr || !container->getContain()->isValidContainerFor( payload, true ) )
			return FALSE;

		container->getContain()->addToContain( payload );
		outerPayload = container;
	}

	if( transport->getContain() == nullptr || !transport->getContain()->isValidContainerFor( outerPayload, true ) )
		return FALSE;

	transport->getContain()->addToContain( outerPayload );
	return TRUE;
}

ProductionUpdateInterface* OCLUpdate::getProductionUpdateInterface()
{
	return isSupplyDropZone() ? this : nullptr;
}

Bool OCLUpdate::isSupplyDropZone() const
{
	return getObject() != nullptr && getObject()->isKindOf( KINDOF_FS_SUPPLY_DROPZONE );
}

Bool OCLUpdate::isEligibleSupplyManifestUnit( const ThingTemplate *unitType ) const
{
	if( !isSupplyDropZone() || unitType == nullptr )
		return FALSE;

	if( !(unitType->isKindOf( KINDOF_INFANTRY ) || unitType->isKindOf( KINDOF_VEHICLE )) )
		return FALSE;
	if( unitType->isKindOf( KINDOF_AIRCRAFT ) || unitType->isKindOf( KINDOF_DOZER ) || unitType->isKindOf( KINDOF_HERO ) )
		return FALSE;

	Player *player = getObject()->getControllingPlayer();
	if( player == nullptr || !player->isPlayableSide() || !player->canBuild( unitType ) )
		return FALSE;

	return TRUE;
}

Int OCLUpdate::getSupplyManifestCost() const
{
	Player *player = getObject()->getControllingPlayer();
	if( player == nullptr )
		return 0;
	Int total = 0;
	for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
		if( entry->m_objectToProduce )
			total += MAX( 0, entry->m_objectToProduce->calcCostToBuild( player ) );
	return total;
}

CanMakeType OCLUpdate::canQueueCreateUnit( const ThingTemplate *unitType ) const
{
	if( !isEligibleSupplyManifestUnit( unitType ) )
		return CANMAKE_NO_PREREQ;
	if( getObject()->isDisabled() )
		return CANMAKE_FACTORY_IS_DISABLED;
	if( m_manifestCount >= SUPPLY_DROP_MANIFEST_MAX_ENTRIES )
		return CANMAKE_QUEUE_FULL;

	Player *player = getObject()->getControllingPlayer();
	const Int cost = unitType->calcCostToBuild( player );
	if( cost <= 0 || cost > SUPPLY_DROP_MANIFEST_BUDGET )
		return CANMAKE_NO_MONEY;
	if( getSupplyManifestCost() + cost > SUPPLY_DROP_MANIFEST_BUDGET )
		return CANMAKE_NO_MONEY;
	return CANMAKE_OK;
}

ProductionID OCLUpdate::requestUniqueUnitID()
{
	ProductionID result = m_manifestUniqueID;
	m_manifestUniqueID = (ProductionID)(m_manifestUniqueID + 1);
	return result;
}

void OCLUpdate::appendManifestEntry( ProductionEntry *entry )
{
	if( entry == nullptr ) return;
	entry->m_prev = m_manifestTail;
	entry->m_next = nullptr;
	if( m_manifestTail ) m_manifestTail->m_next = entry;
	else m_manifestHead = entry;
	m_manifestTail = entry;
	++m_manifestCount;
}

void OCLUpdate::removeManifestEntry( ProductionEntry *entry )
{
	if( entry == nullptr ) return;
	if( entry->m_prev ) entry->m_prev->m_next = entry->m_next;
	else m_manifestHead = entry->m_next;
	if( entry->m_next ) entry->m_next->m_prev = entry->m_prev;
	else m_manifestTail = entry->m_prev;
	if( m_manifestCount > 0 ) --m_manifestCount;
	deleteInstance( entry );
}

void OCLUpdate::clearSupplyManifest()
{
	while( m_manifestHead ) removeManifestEntry( m_manifestHead );
	m_manifestHead = nullptr;
	m_manifestTail = nullptr;
	m_manifestCount = 0;
}

Bool OCLUpdate::queueCreateUnit( const ThingTemplate *unitType, ProductionID productionID )
{
	if( canQueueCreateUnit( unitType ) != CANMAKE_OK ) return FALSE;
	if( productionID == PRODUCTIONID_INVALID ) productionID = requestUniqueUnitID();

	ProductionEntry *entry = newInstance( ProductionEntry );
	entry->m_type = PRODUCTION_UNIT;
	entry->m_objectToProduce = unitType;
	entry->m_productionID = productionID;
	entry->m_percentComplete = 0.0f;
	entry->m_framesUnderConstruction = 0;
	entry->m_productionQuantityTotal = 1;
	entry->m_productionQuantityProduced = 0;
	entry->m_exitDoor = DOOR_NONE_AVAILABLE;
	appendManifestEntry( entry );
	return TRUE;
}

Bool OCLUpdate::cancelUnitCreate( ProductionID productionID )
{
	for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
		if( entry->m_productionID == productionID )
		{
			removeManifestEntry( entry );
			return TRUE;
		}
	return FALSE;
}

void OCLUpdate::cancelAllUnitsOfType( const ThingTemplate *unitType )
{
	for( ProductionEntry *entry = m_manifestHead; entry; )
	{
		ProductionEntry *next = entry->m_next;
		if( entry->m_objectToProduce == unitType ) removeManifestEntry( entry );
		entry = next;
	}
}

void OCLUpdate::cancelAndRefundAllProduction()
{
	clearSupplyManifest();
}

UnsignedInt OCLUpdate::countUnitTypeInQueue( const ThingTemplate *unitType ) const
{
	UnsignedInt count = 0;
	for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
		if( entry->m_objectToProduce == unitType ) ++count;
	return count;
}

Bool OCLUpdate::deliverSupplyManifest( const Coord3D& edgePoint )
{
	if( !isSupplyDropZone() || m_manifestHead == nullptr ) return FALSE;
	Object *dropZone = getObject();
	Player *player = dropZone->getControllingPlayer();
	if( player == nullptr || !player->isPlayableSide() ) return FALSE;

	std::vector<const ThingTemplate*> payloadTemplates;
	Int spent = 0;
	for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
	{
		const ThingTemplate *unitType = entry->m_objectToProduce;
		if( !isEligibleSupplyManifestUnit( unitType ) ) continue;
		const Int cost = MAX( 0, unitType->calcCostToBuild( player ) );
		if( cost <= 0 || cost > SUPPLY_DROP_MANIFEST_BUDGET || spent + cost > SUPPLY_DROP_MANIFEST_BUDGET ) continue;
		payloadTemplates.push_back( unitType );
		spent += cost;
	}
	if( payloadTemplates.empty() ) return FALSE;

	const ThingTemplate *transportTemplate = TheThingFactory->findTemplate( "AmericaJetCargoPlane" );
	if( transportTemplate == nullptr ) return FALSE;

	Coord3D targetPos = *dropZone->getPosition();
	Coord3D startPos = edgePoint;
	const Real deliveryDistance = 410.0f;
	const Real orient = atan2( targetPos.y - startPos.y, targetPos.x - startPos.x );
	startPos.x -= Cos( orient ) * deliveryDistance * 1.5f;
	startPos.y -= Sin( orient ) * deliveryDistance * 1.5f;

	Object *transport = TheThingFactory->newObject( transportTemplate, player->getDefaultTeam() );
	if( transport == nullptr ) return FALSE;
	transport->setPosition( &startPos );
	transport->setOrientation( orient );
	transport->setProducer( dropZone );
	transport->setScriptStatus( OBJECT_STATUS_SCRIPT_TARGETABLE );

	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );
	DeliverPayloadAIUpdate *ai = (DeliverPayloadAIUpdate*)transport->findUpdateModule( key_DeliverPayloadAIUpdate );
	if( ai == nullptr ) return FALSE;

	DeliverPayloadData delivery;
	delivery.m_distToTarget = deliveryDistance;
	delivery.m_maxAttempts = 4;
	delivery.m_dropOffset.z = -5.0f;
	delivery.m_dropDelay = REAL_TO_INT_CEIL( ConvertDurationFromMsecsToFrames( 350.0f ) );
	delivery.m_isParachuteDirectly = TRUE;
	ai->deliverPayload( &targetPos, &targetPos, &delivery );

	startPos.z = TheTerrainLogic->getGroundHeight( startPos.x, startPos.y ) + ai->getCurLocomotor()->getPreferredHeight();
	transport->setPosition( &startPos );
	PhysicsBehavior *physics = transport->getPhysics();
	if( physics )
	{
		Coord3D startingForce = *transport->getUnitDirectionVector2D();
		const Real maxSpeed = ai->getCurLocomotor()->getMaxSpeedForCondition( transport->getBodyModule()->getDamageState() );
		startingForce.scale( maxSpeed * physics->getMass() );
		physics->applyMotiveForce( &startingForce );
	}

	for( std::vector<const ThingTemplate*>::const_iterator it = payloadTemplates.begin(); it != payloadTemplates.end(); ++it )
	{
		const ThingTemplate *unitType = *it;
		Object *payload = TheThingFactory->newObject( unitType, player->getDefaultTeam() );
		if( payload == nullptr ) continue;
		attachSupplyDropPayload( transport, payload, unitType->isKindOf( KINDOF_VEHICLE ) ? "LargeParachute" : "AmericaParachute", startPos );
	}

	const Int cashRemainder = SUPPLY_DROP_MANIFEST_BUDGET - spent;
	if( cashRemainder > 0 )
	{
		const ThingTemplate *cashTemplate = TheThingFactory->findTemplate( "SupplyDropManifestCashCrate" );
		if( cashTemplate )
		{
			Object *cash = TheThingFactory->newObject( cashTemplate, player->getDefaultTeam() );
			if( cash )
			{
				static const NameKeyType key_MoneyCrateCollide = NAMEKEY( "MoneyCrateCollide" );
				MoneyCrateCollide *money = (MoneyCrateCollide*)cash->findModule( key_MoneyCrateCollide );
				if( money ) money->setMoneyProvidedOverride( (UnsignedInt)cashRemainder );
				attachSupplyDropPayload( transport, cash, "AmericaCrateParachute", startPos );
			}
		}
	}
	return TRUE;
}
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime OCLUpdate::update()
{
#if RETAIL_COMPATIBLE_CRC
	if( getObject()->isDisabled() )
	{
		m_nextCreationFrame++;
		return UPDATE_SLEEP_NONE;
	}
#else
	// TheSuperHackers @bugfix dizzyj/Caball009/Mauller 14/07/2025 prevent triggering supply drop when subdued while under construction
	// When the construction is finished, we allow the timer to be initialized and then start shifting the timer while subdued
	if ( m_timerStartedFrame > 0 && getObject()->isDisabled() )
	{
		m_nextCreationFrame++;
		m_timerStartedFrame++;
		return UPDATE_SLEEP_NONE;
	}
#endif

	const OCLUpdateModuleData *data = getOCLUpdateModuleData();

	// Test if the OCL update is faction dependent. If so, check for faction changes
	if (data->m_isFactionTriggered)
	{
		Player *player = getObject()->getControllingPlayer();

		// Test for when a player captures the building
		if (m_isFactionNeutral)
		{
			if( player && player->isPlayableSide() )
			{
				m_currentPlayerColor = player->getPlayerColor();
				m_isFactionNeutral = FALSE;
				setNextCreationFrame();
			}
		}
		// Test for when the building has been made neutral or when it changes faction
		else
		{
			// If this is no longer under player control, then we set the faction to neutral
			if( !player || !player->isPlayableSide() )
			{
				m_isFactionNeutral = TRUE;
			}
			// If another player has taken control, reset the timer
			else if( player && player->getPlayerColor() != m_currentPlayerColor)
			{
				m_currentPlayerColor = player->getPlayerColor();
				setNextCreationFrame();
			}
		}

		// If the building is neutal, skip further update
		if (m_isFactionNeutral)
			return UPDATE_SLEEP_NONE;
	}

/// @todo srj use SLEEPY_UPDATE here
	if( shouldCreate() )
	{
		if( m_nextCreationFrame == 0 )
		{
			// You don't get to actually spread the first try, you start on a timer, then go
			setNextCreationFrame();
			return UPDATE_SLEEP_NONE;
		}

		setNextCreationFrame();

		Coord3D creationCoord;
		if( getOCLUpdateModuleData()->m_isCreateAtEdge )
			creationCoord = TheTerrainLogic->findClosestEdgePoint( getObject()->getPosition() );
		else
			creationCoord = *getObject()->getPosition();

#if !RETAIL_COMPATIBLE_CRC
		if( isSupplyDropZone() && m_manifestHead != nullptr && deliverSupplyManifest( creationCoord ) )
			return UPDATE_SLEEP_NONE;
#endif

		// If this is faction triggered, search through the faction specific OCLs to find the match
		if (data->m_isFactionTriggered)
		{
			std::string playerFactionName;

			Player *player = getObject()->getControllingPlayer();
			if (!player) return UPDATE_SLEEP_NONE;

			const PlayerTemplate *playerT = player->getPlayerTemplate();
			if (!playerT) return UPDATE_SLEEP_NONE;

			// Get and store the faction side to compare with the faction ocl list
			if (playerT->getSide().str()) playerFactionName = playerT->getSide().str();

			// Loop through the list of faction ocls to find the matching faction that triggers the specific ocls
			for (OCLUpdateModuleData::FactionOCLList::const_iterator it = data->m_factionOCL.begin(); it != data->m_factionOCL.end(); ++it)
			{
				const OCLUpdateModuleData::FactionOCLInfo &info = *it;
				if (playerFactionName == info.m_factionName)
				{
					ObjectCreationList::create( info.m_ocl, getObject(), &creationCoord, getObject()->getPosition(), getObject()->getOrientation() );
					break;
				}
			}
		}
		// Use the non faction OCL information
		else
		{
			ObjectCreationList::create( data->m_ocl, getObject(), &creationCoord, getObject()->getPosition(), getObject()->getOrientation() );
		}
	}
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void OCLUpdate::resetTimer()
{
	setNextCreationFrame();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool OCLUpdate::shouldCreate()
{
	if( TheGameLogic->getFrame() < m_nextCreationFrame )
		return FALSE;//too soon

	if( getObject()->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return FALSE;// not built yet

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void OCLUpdate::setNextCreationFrame()
{
	UnsignedInt delay = GameLogicRandomValue( getOCLUpdateModuleData()->m_minDelay,
																						getOCLUpdateModuleData()->m_maxDelay );
	m_timerStartedFrame = TheGameLogic->getFrame();
	m_nextCreationFrame = m_timerStartedFrame + delay;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Real OCLUpdate::getCountdownPercent() const
{
	UnsignedInt now = TheGameLogic->getFrame();

	return 1.0f - (( m_nextCreationFrame - now ) / (float)( m_nextCreationFrame - m_timerStartedFrame ));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UnsignedInt OCLUpdate::getRemainingFrames() const
{
	UnsignedInt now = TheGameLogic->getFrame();

	return ( m_nextCreationFrame - now );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void OCLUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

#if !RETAIL_COMPATIBLE_CRC
	xfer->xferUnsignedInt( &m_manifestCount );
	xfer->xferUser( &m_manifestUniqueID, sizeof( ProductionID ) );
	for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
	{
		AsciiString name = entry->m_objectToProduce ? entry->m_objectToProduce->getName() : AsciiString::TheEmptyString;
		xfer->xferAsciiString( &name );
		xfer->xferUser( &entry->m_productionID, sizeof( ProductionID ) );
	}
#endif
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void OCLUpdate::xfer( Xfer *xfer )
{

	// version
#if !RETAIL_COMPATIBLE_CRC && !RETAIL_COMPATIBLE_XFER_SAVE
	XferVersion currentVersion = 2;
#else
	XferVersion currentVersion = 1;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// next creation frame
	xfer->xferUnsignedInt( &m_nextCreationFrame );

	// timer stated frame
	xfer->xferUnsignedInt( &m_timerStartedFrame );

	// faction status
	xfer->xferBool( &m_isFactionNeutral );

	// current owning player color
	xfer->xferInt( &m_currentPlayerColor );

#if !RETAIL_COMPATIBLE_CRC
	if( version >= 2 )
	{
		UnsignedShort manifestCount = (UnsignedShort)m_manifestCount;
		xfer->xferUnsignedShort( &manifestCount );
		if( xfer->getXferMode() == XFER_SAVE )
		{
			for( ProductionEntry *entry = m_manifestHead; entry; entry = entry->m_next )
			{
				AsciiString name = entry->m_objectToProduce->getName();
				xfer->xferAsciiString( &name );
				xfer->xferUser( &entry->m_productionID, sizeof( ProductionID ) );
			}
		}
		else
		{
			clearSupplyManifest();
			for( UnsignedShort i = 0; i < manifestCount; ++i )
			{
				AsciiString name;
				ProductionID productionID = PRODUCTIONID_INVALID;
				xfer->xferAsciiString( &name );
				xfer->xferUser( &productionID, sizeof( ProductionID ) );
				const ThingTemplate *unitType = TheThingFactory->findTemplate( name );
				if( unitType == nullptr ) throw SC_INVALID_DATA;
				ProductionEntry *entry = newInstance( ProductionEntry );
				entry->m_type = PRODUCTION_UNIT;
				entry->m_objectToProduce = unitType;
				entry->m_productionID = productionID;
				entry->m_percentComplete = 0.0f;
				entry->m_framesUnderConstruction = 0;
				entry->m_productionQuantityTotal = 1;
				entry->m_productionQuantityProduced = 0;
				entry->m_exitDoor = DOOR_NONE_AVAILABLE;
				appendManifestEntry( entry );
			}
		}
		xfer->xferUser( &m_manifestUniqueID, sizeof( ProductionID ) );
	}
#endif
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void OCLUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
