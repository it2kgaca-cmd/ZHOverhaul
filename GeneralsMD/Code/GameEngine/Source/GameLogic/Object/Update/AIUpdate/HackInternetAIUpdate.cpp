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

// HackInternetAIUpdate.cpp ////////////
// Author: Kris Morness, June 2002
// Desc:   State machine that handles internet hacking (free cash)

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameText.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
//#include "GameLogic/PartitionManager.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIStateMachine* HackInternetAIUpdate::makeStateMachine()
{
	// Passive cash is no longer a command mode. Use the normal AI machine so a movement
	// order never waits for an internet-hack pack/unpack sequence.
	return AIUpdateInterface::makeStateMachine();
}

//-------------------------------------------------------------------------------------------------
HackInternetAIUpdate::HackInternetAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_hasPendingCommand = false;
	m_cashFramesRemaining = getHackInternetAIUpdateModuleData()->m_cashUpdateDelay;
	if (m_cashFramesRemaining == 0)
		m_cashFramesRemaining = 1;
	m_cashRemainderPercent = 0;
}

//-------------------------------------------------------------------------------------------------
HackInternetAIUpdate::~HackInternetAIUpdate()
{
}

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isIdle() const
{
	// we need to do this because we enter an idle state briefly between takeoff/landing in these cases,
	// but scripting relies on us never claiming to be "idle"...
	if (m_hasPendingCommand)
		return false;

	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isHacking() const
{
	// Passive income is not an active command-mode state.
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isHackingPackingOrUnpacking() const
{
	return false;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime HackInternetAIUpdate::update()
{
	AIUpdateInterface::update();
	updateCashIncome();
	updateIdleHackAnimation();

	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// The briefcase/laptop is presentation only. Commands execute immediately.
	m_hasPendingCommand = false;
	AIUpdateInterface::aiDoCommand(parms);
}


//-------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::hackInternet()
{
	// Compatibility entry point for old scripts/InternetHackContain. Cash is already running.
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getUnpackTime() const
{
	return 0;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getPackTime() const
{
	return 0;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getCashUpdateDelay() const
{
	UnsignedInt delay = getHackInternetAIUpdateModuleData()->m_cashUpdateDelay;
	return delay ? delay : 1;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getBaseCashAmount() const
{
	const Object *owner = getObject();
	ExperienceTracker *xp = owner ? owner->getExperienceTracker() : nullptr;
	if (!xp)
		return 1;

	UnsignedInt amount = 0;
	switch (xp->getVeterancyLevel())
	{
		case LEVEL_HEROIC:
			amount = getHeroicCashAmount();
			if (amount) break;
			FALLTHROUGH;
		case LEVEL_ELITE:
			amount = getEliteCashAmount();
			if (amount) break;
			FALLTHROUGH;
		case LEVEL_VETERAN:
			amount = getVeteranCashAmount();
			if (amount) break;
			FALLTHROUGH;
		case LEVEL_REGULAR:
			amount = getRegularCashAmount();
			if (amount) break;
			FALLTHROUGH;
		default:
			amount = 1;
			break;
	}
	return amount;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getCashIncomePercent() const
{
	const Object *owner = getObject();
	if (!owner)
		return 100;

	const Object *container = owner->getContainedBy();
	if (container)
	{
		const ThingTemplate *containerTemplate = container->getTemplate();
		const char *containerName = containerTemplate ? containerTemplate->getName().str() : nullptr;
		// Base and general-prefixed Internet Centers all retain this canonical suffix.
		return (containerName && strstr(containerName, "ChinaInternetCenter")) ? 200 : 100;
	}

	const PhysicsBehavior *physics = owner->getPhysics();
	const Bool moving = physics && physics->getVelocityMagnitude() > 0.01f;
	return moving ? 125 : 150;
}

// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::updateCashIncome()
{
	Object *owner = getObject();
	if (!owner || owner->isEffectivelyDead())
		return;

	if (m_cashFramesRemaining > 0)
	{
		--m_cashFramesRemaining;
		return;
	}
	m_cashFramesRemaining = getCashUpdateDelay();

	if (owner->isDisabledByType(DISABLED_HACKED))
		return;

	Player *player = owner->getControllingPlayer();
	Money *money = player ? player->getMoney() : nullptr;
	ExperienceTracker *xp = owner->getExperienceTracker();
	if (!money || !xp)
		return;

	const UnsignedInt baseAmount = getBaseCashAmount();
	const UnsignedInt incomePercent = getCashIncomePercent();
	const UnsignedInt scaledHundredths = baseAmount * incomePercent + m_cashRemainderPercent;
	const UnsignedInt amount = scaledHundredths / 100;
	m_cashRemainderPercent = scaledHundredths % 100;

	if (amount == 0)
		return;

	money->deposit(amount);
	player->getScoreKeeper()->addMoneyEarned(amount);
	xp->addExperiencePoints(getXpPerCashUpdate());

	if (owner->isLogicallyVisible())
	{
		UnicodeString moneyString;
		moneyString.format(TheGameText->fetch("GUI:AddCash"), amount);
		Coord3D pos = *owner->getPosition();
		pos.z += 20.0f;

		Object *container = owner->getContainedBy();
		if (container)
		{
			Real width = container->getGeometryInfo().getMajorRadius() * 0.3f;
			Real depth = container->getGeometryInfo().getMinorRadius() * 0.3f;
			pos.x += GameClientRandomValue(-width, width);
			pos.y += GameClientRandomValue(-depth, depth);
		}

		TheInGameUI->addFloatingText(moneyString, &pos, GameMakeColor(0, 255, 0, 255));
	}

	AudioEventRTS sound = *(owner->getTemplate()->getPerUnitSound("UnitCashPing"));
	sound.setObjectID(owner->getID());
	TheAudio->addAudioEvent(&sound);
}

// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::updateIdleHackAnimation()
{
	Object *owner = getObject();
	if (!owner || owner->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
		return;

	const PhysicsBehavior *physics = owner->getPhysics();
	const Bool moving = physics && physics->getVelocityMagnitude() > 0.01f;
	const Bool showLaptop = owner->getContainedBy() == nullptr && !moving;

	owner->clearModelConditionState(MODELCONDITION_PACKING);
	owner->clearModelConditionState(MODELCONDITION_UNPACKING);
	if (showLaptop)
		owner->setModelConditionState(MODELCONDITION_FIRING_A);
	else
		owner->clearModelConditionState(MODELCONDITION_FIRING_A);
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Passive cash ticker state */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);
	xfer->xferBool(&m_hasPendingCommand);
	if (m_hasPendingCommand) {
		m_pendingCommand.doXfer(xfer);
	}

	if (version >= 2)
	{
		xfer->xferUnsignedInt(&m_cashFramesRemaining);
		xfer->xferUnsignedInt(&m_cashRemainderPercent);
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		m_cashFramesRemaining = getCashUpdateDelay();
		m_cashRemainderPercent = 0;
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::loadPostProcess()
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
HackInternetStateMachine::HackInternetStateMachine( Object *owner, AsciiString name ) : AIStateMachine( owner, "HackInternetStateMachine" )
{
	//HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	// order matters: first state is the default state.
	defineState( UNPACKING,						newInstance(UnpackingState)( this ), HACK_INTERNET, HACK_INTERNET );
	defineState( HACK_INTERNET,				newInstance(HackInternetState)( this ), PACKING, PACKING );
	defineState( PACKING,							newInstance(PackingState)( this ), AI_IDLE, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
HackInternetStateMachine::~HackInternetStateMachine()
{
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void UnpackingState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void UnpackingState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void UnpackingState::loadPostProcess()
{
}

//-------------------------------------------------------------------------------------------------
StateReturnType UnpackingState::onEnter()
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	owner->clearModelConditionFlags( MAKE_MODELCONDITION_MASK3( MODELCONDITION_PACKING, MODELCONDITION_FIRING_A, MODELCONDITION_UNPACKING ) );

	owner->setModelConditionState( MODELCONDITION_UNPACKING );

	AudioEventRTS sound = *owner->getTemplate()->getPerUnitSound( "UnitUnpack" );
	sound.setObjectID( owner->getID() );
	TheAudio->addAudioEvent( &sound );

	Real variationFactor = ai->getPackUnpackVariationFactor();
	Real variation = GameLogicRandomValueReal( 1.0f - variationFactor, 1.0f + variationFactor );
	m_framesRemaining = ai->getUnpackTime() * variation; //In frames
	owner->getDrawable()->setAnimationLoopDuration( m_framesRemaining );

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType UnpackingState::update()
{
	Object *owner = getMachineOwner();
//	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	// This is a bit hacky, no pun intended, but if this Update is engeged specialability (disablebuilding)
	// The unpacking modelconditionflag gets cleared by specialability::cleanup() after my onEnter() sets it!
	// Why HackInterent wasn't included within specialability I can't figure out, but... too late to change now.
	owner->setModelConditionState( MODELCONDITION_UNPACKING );

	if( m_framesRemaining > 0 )
	{
		m_framesRemaining--;
	}
	else
	{
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void UnpackingState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_UNPACKING );
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PackingState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void PackingState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PackingState::loadPostProcess()
{
}

//-------------------------------------------------------------------------------------------------
StateReturnType PackingState::onEnter()
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	owner->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_FIRING_A ),
																				 MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );

	AudioEventRTS sound = *owner->getTemplate()->getPerUnitSound( "UnitPack" );
	sound.setObjectID( owner->getID() );
	TheAudio->addAudioEvent( &sound );

	Real variationFactor = ai->getPackUnpackVariationFactor();
	Real variation = GameLogicRandomValueReal( 1.0f - variationFactor, 1.0f + variationFactor );
	m_framesRemaining = ai->getPackTime() * variation; //In frames
	owner->getDrawable()->setAnimationLoopDuration( m_framesRemaining );
	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType PackingState::update()
{
//	Object *owner = getMachineOwner();
//	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	if( m_framesRemaining > 0 )
	{
		m_framesRemaining--;
	}
	else
	{
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void PackingState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_PACKING );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HackInternetState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void HackInternetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HackInternetState::loadPostProcess()
{
}

//-------------------------------------------------------------------------------------------------
StateReturnType HackInternetState::onEnter()
{
	//Go into the hack internet stance.
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	owner->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ),
																				 MAKE_MODELCONDITION_MASK( MODELCONDITION_FIRING_A ) );

	m_framesRemaining = ai->getCashUpdateDelay();

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType HackInternetState::update()
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	if( owner->isDisabledByType( DISABLED_HACKED ) )
	{
		//Don't hack while hacked, hehe.
		return STATE_CONTINUE;
	}

	if( m_framesRemaining > 0 )
	{
		//Decrement frame counter.
		m_framesRemaining--;
	}
	else
	{
		//We have waited the full amount of the delay, so hack some cash from the heavens!

		//Add cash
		Money *money = owner->getControllingPlayer()->getMoney();
		if( money )
		{
			ExperienceTracker *xp = owner->getExperienceTracker();
			if( xp )
			{
				UnsignedInt amount = 0;
				switch( xp->getVeterancyLevel() )
				{
					case LEVEL_HEROIC:
						amount = ai->getHeroicCashAmount();
						if( amount )
						{
							break;
						}
						FALLTHROUGH; //If entry missing, fall through!
					case LEVEL_ELITE:
						amount = ai->getEliteCashAmount();
						if( amount )
						{
							break;
						}
						FALLTHROUGH; //If entry missing, fall through!
					case LEVEL_VETERAN:
						amount = ai->getVeteranCashAmount();
						if( amount )
						{
							break;
						}
						FALLTHROUGH; //If entry missing, fall through!
					case LEVEL_REGULAR:
						amount = ai->getRegularCashAmount();
						if( amount )
						{
							break;
						}
						FALLTHROUGH; //If entry missing, fall through!
					default:
						amount = 1;
						break;
				}
				money->deposit( amount );
				owner->getControllingPlayer()->getScoreKeeper()->addMoneyEarned( amount );

				//Grant the unit some experience for a successful hack.
				xp->addExperiencePoints( ai->getXpPerCashUpdate() );

				if (owner->isLogicallyVisible())
				{
					// OY LOOK!  I AM USING LOCAL PLAYER.  Do not put anything other than TheInGameUI->addFloatingText in the block this controls!!!
					//Display cash income floating over the hacker.
					UnicodeString moneyString;
					moneyString.format( TheGameText->fetch( "GUI:AddCash" ), amount );
					Coord3D pos;
					pos.zero();
					pos.add( *owner->getPosition() );
					pos.z += 20.0f; //add a little z to make it show up above the unit.


          Object *internetCenter = owner->getContainedBy();
          if ( internetCenter )
          {
            Real width = internetCenter->getGeometryInfo().getMajorRadius() * 0.3f;
            Real depth = internetCenter->getGeometryInfo().getMinorRadius() * 0.3f;
            pos.x += GameClientRandomValue(-width,width);
            pos.y += GameClientRandomValue(-depth,depth);
          }


					TheInGameUI->addFloatingText( moneyString, &pos, GameMakeColor( 0, 255, 0, 255 ) );
				}

				AudioEventRTS sound = *(owner->getTemplate()->getPerUnitSound( "UnitCashPing" ));
				sound.setObjectID( owner->getID() );
				TheAudio->addAudioEvent( &sound );
			}
		}


		//Reset timer and start a new cycle.
		m_framesRemaining = ai->getCashUpdateDelay();

	}

	//This is a persistent state until told otherwise.
	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void HackInternetState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_FIRING_A );
}
