//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "base_playeranimstate.h"
#include "tier0/vprof.h"
#include "animation.h"
#include "studio.h"
#include "apparent_velocity_helper.h"
#include "utldict.h"

#include "sdk_playeranimstate.h"
#include "base_playeranimstate.h"
#include "datacache/imdlcache.h"

#ifdef CLIENT_DLL
#include "c_sdk_player.h"
#else
#include "sdk_player.h"
#endif

#define SDK_RUN_SPEED				320.0f
#define SDK_WALK_SPEED				75.0f
#define SDK_CROUCHWALK_SPEED		110.0f

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
// Output : CMultiPlayerAnimState*
//-----------------------------------------------------------------------------
CSDKPlayerAnimState* CreateSDKPlayerAnimState( CSDKPlayer *pPlayer )
{
	MDLCACHE_CRITICAL_SECTION();

	// Setup the movement data.
	MultiPlayerMovementData_t movementData;
	movementData.m_flBodyYawRate = 720.0f;
	movementData.m_flRunSpeed = SDK_RUN_SPEED;
	movementData.m_flWalkSpeed = SDK_WALK_SPEED;
	movementData.m_flSprintSpeed = -1.0f;

	// Create animation state for this player.
	CSDKPlayerAnimState *pRet = new CSDKPlayerAnimState( pPlayer, movementData );

	// Specific SDK player initialization.
	pRet->InitSDKAnimState( pPlayer );

	return pRet;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :  - 
//-----------------------------------------------------------------------------
CSDKPlayerAnimState::CSDKPlayerAnimState()
{
	m_pSDKPlayer = NULL;

	// Don't initialize SDK specific variables here. Init them in InitSDKAnimState()
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//			&movementData - 
//-----------------------------------------------------------------------------
CSDKPlayerAnimState::CSDKPlayerAnimState( CBasePlayer *pPlayer, MultiPlayerMovementData_t &movementData )
: CMultiPlayerAnimState( pPlayer, movementData )
{
	m_pSDKPlayer = NULL;

	// Don't initialize SDK specific variables here. Init them in InitSDKAnimState()
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :  - 
//-----------------------------------------------------------------------------
CSDKPlayerAnimState::~CSDKPlayerAnimState()
{
}

//-----------------------------------------------------------------------------
// Purpose: Initialize Team Fortress specific animation state.
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CSDKPlayerAnimState::InitSDKAnimState( CSDKPlayer *pPlayer )
{
	m_pSDKPlayer = pPlayer;
#if defined ( SDK_USE_PRONE )
	m_iProneActivity = ACT_MP_STAND_TO_PRONE;
	m_bProneTransition = false;
	m_bProneTransitionFirstFrame = false;
#endif

	m_iSlideActivity = ACT_DAB_SLIDESTART;
	m_bSlideTransition = false;
	m_bSlideTransitionFirstFrame = false;

	m_iRollActivity = ACT_DAB_ROLL;
	m_bRollTransition = false;
	m_bRollTransitionFirstFrame = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSDKPlayerAnimState::ClearAnimationState( void )
{
#if defined ( SDK_USE_PRONE )
	m_bProneTransition = false;
	m_bProneTransitionFirstFrame = false;
#endif

	m_bSlideTransition = false;
	m_bSlideTransitionFirstFrame = false;

	m_bRollTransition = false;
	m_bRollTransitionFirstFrame = false;

	BaseClass::ClearAnimationState();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : actDesired - 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CSDKPlayerAnimState::TranslateActivity( Activity actDesired )
{
	Activity translateActivity = BaseClass::TranslateActivity( actDesired );

	if ( GetSDKPlayer()->GetActiveWeapon() )
	{
		translateActivity = GetSDKPlayer()->GetActiveWeapon()->ActivityOverride( translateActivity, false );
	}

	return translateActivity;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSDKPlayerAnimState::Update( float eyeYaw, float eyePitch )
{
	// Profile the animation update.
	VPROF( "CMultiPlayerAnimState::Update" );

	// Get the SDK player.
	CSDKPlayer *pSDKPlayer = GetSDKPlayer();
	if ( !pSDKPlayer )
		return;

	// Get the studio header for the player.
	CStudioHdr *pStudioHdr = pSDKPlayer->GetModelPtr();
	if ( !pStudioHdr )
		return;

	// Check to see if we should be updating the animation state - dead, ragdolled?
	if ( !ShouldUpdateAnimState() )
	{
		ClearAnimationState();
		return;
	}

	// Store the eye angles.
	m_flEyeYaw = AngleNormalize( eyeYaw );
	m_flEyePitch = AngleNormalize( eyePitch );

	// Compute the player sequences.
	ComputeSequences( pStudioHdr );

	if ( SetupPoseParameters( pStudioHdr ) )
	{
		// Pose parameter - what direction are the player's legs running in.
		ComputePoseParam_MoveYaw( pStudioHdr );

		// Pose parameter - Torso aiming (up/down).
		ComputePoseParam_AimPitch( pStudioHdr );

		// Pose parameter - Torso aiming (rotation).
		ComputePoseParam_AimYaw( pStudioHdr );
	}

#ifdef CLIENT_DLL 
	if ( C_BasePlayer::ShouldDrawLocalPlayer() )
	{
		m_pSDKPlayer->SetPlaybackRate( 1.0f );
	}
#endif
}
extern ConVar mp_slammoveyaw;
float SnapYawTo( float flValue );
void CSDKPlayerAnimState::ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr )
{
	// Get the estimated movement yaw.
	EstimateYaw();

	// Get the view yaw.
	float flAngle = AngleNormalize( m_flEyeYaw );

	// Calc side to side turning - the view vs. movement yaw.
	float flYaw = flAngle - m_PoseParameterData.m_flEstimateYaw;
	flYaw = AngleNormalize( -flYaw );

	// Get the current speed the character is running.
	bool bIsMoving;
	float flPlaybackRate = CalcMovementPlaybackRate( &bIsMoving );

	// Setup the 9-way blend parameters based on our speed and direction.
	Vector2D vecCurrentMoveYaw( 0.0f, 0.0f );
	if ( bIsMoving )
	{
		if ( mp_slammoveyaw.GetBool() )
		{
			flYaw = SnapYawTo( flYaw );
		}
		vecCurrentMoveYaw.x = cos( DEG2RAD( flYaw ) ) * flPlaybackRate;
		vecCurrentMoveYaw.y = -sin( DEG2RAD( flYaw ) ) * flPlaybackRate;
	}

	// Set the 9-way blend movement pose parameters.
	GetBasePlayer()->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveX, vecCurrentMoveYaw.x );
	GetBasePlayer()->SetPoseParameter( pStudioHdr, m_PoseParameterData.m_iMoveY, -vecCurrentMoveYaw.y ); //Tony; flip it

	m_DebugAnimData.m_vecMoveYaw = vecCurrentMoveYaw;
}
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : event - 
//-----------------------------------------------------------------------------
void CSDKPlayerAnimState::DoAnimationEvent( PlayerAnimEvent_t event, int nData )
{
	Activity iGestureActivity = ACT_INVALID;

	switch( event )
	{
	case PLAYERANIMEVENT_ATTACK_PRIMARY:
		{
			// Weapon primary fire.
			if ( m_pSDKPlayer->m_Shared.IsProne() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_PRONE );
			else if ( m_pSDKPlayer->m_Shared.IsSliding() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_SLIDE );
			else if ( m_pSDKPlayer->m_Shared.IsRolling() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_ROLL );
			else if ( m_pSDKPlayer->m_Shared.IsDiving() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_DIVE );
			else if ( m_pSDKPlayer->GetFlags() & FL_DUCKING )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_CROUCH );
			else
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK );

			iGestureActivity = ACT_VM_PRIMARYATTACK;
			break;
		}

	case PLAYERANIMEVENT_VOICE_COMMAND_GESTURE:
		{
			if ( !IsGestureSlotActive( GESTURE_SLOT_ATTACK_AND_RELOAD ) )
			{
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, (Activity)nData );
			}
			iGestureActivity = ACT_VM_IDLE; //TODO?
			break;
		}
	case PLAYERANIMEVENT_ATTACK_SECONDARY:
		{
			// Weapon secondary fire.
			if ( m_pSDKPlayer->m_Shared.IsProne() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_PRONE );
			else if ( m_pSDKPlayer->m_Shared.IsSliding() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_SLIDE );
			else if ( m_pSDKPlayer->m_Shared.IsRolling() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_ROLL );
			else if ( m_pSDKPlayer->m_Shared.IsDiving() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_DIVE );
			else if ( m_pSDKPlayer->GetFlags() & FL_DUCKING )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK_CROUCH );
			else
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_PRIMARYATTACK );

			iGestureActivity = ACT_VM_PRIMARYATTACK;
			break;
		}
	case PLAYERANIMEVENT_ATTACK_PRE:
		{
			if ( m_pSDKPlayer->GetFlags() & FL_DUCKING ) 
			{
				// Weapon pre-fire. Used for minigun windup, sniper aiming start, etc in crouch.
				iGestureActivity = ACT_MP_ATTACK_CROUCH_PREFIRE;
			}
			else
			{
				// Weapon pre-fire. Used for minigun windup, sniper aiming start, etc.
				iGestureActivity = ACT_MP_ATTACK_STAND_PREFIRE;
			}

			RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, iGestureActivity, false );
			iGestureActivity = ACT_VM_IDLE; //TODO?

			break;
		}
	case PLAYERANIMEVENT_ATTACK_POST:
		{
			RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_ATTACK_STAND_POSTFIRE );
			iGestureActivity = ACT_VM_IDLE; //TODO?
			break;
		}

	case PLAYERANIMEVENT_RELOAD:
		{
			// Weapon reload.
			if ( m_pSDKPlayer->m_Shared.IsProne() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_RELOAD_PRONE );
			else if ( m_pSDKPlayer->m_Shared.IsSliding() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_RELOAD_SLIDE );
			else if ( GetBasePlayer()->GetFlags() & FL_DUCKING )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_RELOAD_CROUCH );
			else
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_DAB_RELOAD );

			iGestureActivity = ACT_VM_RELOAD; //Make view reload if it isn't already
			break;
		}
	case PLAYERANIMEVENT_RELOAD_LOOP:
		{
			// Weapon reload.
			if ( m_pSDKPlayer->m_Shared.IsProne() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_PRONE_LOOP );
			else if ( GetBasePlayer()->GetFlags() & FL_DUCKING )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_CROUCH_LOOP );
			else
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_STAND_LOOP );

			iGestureActivity = ACT_INVALID; //TODO: fix
			break;
		}
	case PLAYERANIMEVENT_RELOAD_END:
		{
			// Weapon reload.
			if ( m_pSDKPlayer->m_Shared.IsProne() )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_PRONE_END );
			else if ( GetBasePlayer()->GetFlags() & FL_DUCKING )
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_CROUCH_END );
			else
				RestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD, ACT_MP_RELOAD_STAND_END );

			iGestureActivity = ACT_INVALID; //TODO: fix
			break;
		}
#if defined ( SDK_USE_PRONE )
	case PLAYERANIMEVENT_STAND_TO_PRONE:
		{
			m_bProneTransition = true;
			m_bProneTransitionFirstFrame = true;
			m_iProneActivity = ACT_MP_STAND_TO_PRONE;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no stand->prone so just idle.
		}
		break;
	case PLAYERANIMEVENT_CROUCH_TO_PRONE:
		{
			m_bProneTransition = true;
			m_bProneTransitionFirstFrame = true;
			m_iProneActivity = ACT_MP_CROUCH_TO_PRONE;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no crouch->prone so just idle.
		}
		break;
	case PLAYERANIMEVENT_PRONE_TO_STAND:
		{
			m_bProneTransition = true;
			m_bProneTransitionFirstFrame = true;
			m_iProneActivity = ACT_MP_PRONE_TO_STAND;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no prone->stand so just idle.
		}
		break;
	case PLAYERANIMEVENT_PRONE_TO_CROUCH:
		{
			m_bProneTransition = true;
			m_bProneTransitionFirstFrame = true;
			m_iProneActivity = ACT_MP_PRONE_TO_CROUCH;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no prone->crouch so just idle.
		}
		break;
#endif

	case PLAYERANIMEVENT_STAND_TO_SLIDE:
		{
			m_bSlideTransition = true;
			m_bSlideTransitionFirstFrame = true;
			m_iSlideActivity = ACT_DAB_SLIDESTART;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no stand->slide so just idle.
		}
		break;

	case PLAYERANIMEVENT_STAND_TO_ROLL:
		{
			m_bRollTransition = true;
			m_bRollTransitionFirstFrame = true;
			m_iRollActivity = ACT_DAB_ROLL;
			RestartMainSequence();
			iGestureActivity = ACT_VM_IDLE; //Clear for weapon, we have no stand->slide so just idle.
		}
		break;

	default:
		{
			BaseClass::DoAnimationEvent( event, nData );
			break;
		}
	}

#ifdef CLIENT_DLL
	// Make the weapon play the animation as well
	if ( iGestureActivity != ACT_INVALID && GetSDKPlayer() != CSDKPlayer::GetLocalSDKPlayer())
	{
		CBaseCombatWeapon *pWeapon = GetSDKPlayer()->GetActiveWeapon();
		if ( pWeapon )
		{
			pWeapon->EnsureCorrectRenderingModel();
			pWeapon->SendWeaponAnim( iGestureActivity );
			// Force animation events!
			pWeapon->ResetEventsParity();		// reset event parity so the animation events will occur on the weapon. 
			pWeapon->DoAnimationEvents( pWeapon->GetModelPtr() );
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleSwimming( Activity &idealActivity )
{
	bool bInWater = BaseClass::HandleSwimming( idealActivity );

	return bInWater;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleMoving( Activity &idealActivity )
{
	// In TF we run all the time now.
	float flSpeed = GetOuterXYSpeed();

	if ( flSpeed > MOVING_MINIMUM_SPEED )
	{
		// Always assume a run.
		idealActivity = ACT_DAB_RUN_IDLE;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleDucking( Activity &idealActivity )
{
	if ( m_pSDKPlayer->GetFlags() & FL_DUCKING )
	{
		if ( GetOuterXYSpeed() < MOVING_MINIMUM_SPEED )
			idealActivity = ACT_DAB_CROUCH_IDLE;		
		else
			idealActivity = ACT_DAB_CROUCHWALK_IDLE;		

		return true;
	}

	return false;
}
#if defined ( SDK_USE_PRONE )
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleProne( Activity &idealActivity )
{
	if ( m_pSDKPlayer->m_Shared.IsProne() )
	{
		if ( GetOuterXYSpeed() < MOVING_MINIMUM_SPEED )
			idealActivity = ACT_DAB_PRONECHEST_IDLE;		
		else
			idealActivity = ACT_DAB_CRAWL_IDLE;		

		return true;
	}
	
	return false;
}
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleProneTransition( Activity &idealActivity )
{
	if ( m_bProneTransition )
	{
		if (m_bProneTransitionFirstFrame)
		{
			m_bProneTransitionFirstFrame = false;
			RestartMainSequence();	// Reset the animation.
		}

		//Tony; check the cycle, and then stop overriding
		if ( GetBasePlayer()->GetCycle() >= 0.99 )
			m_bProneTransition = false;
		else
			idealActivity = m_iProneActivity;
	}

	return m_bProneTransition;
}
#endif // SDK_USE_PRONE

bool CSDKPlayerAnimState::HandleSliding( Activity &idealActivity )
{
	if ( m_pSDKPlayer->m_Shared.IsSliding() )
	{
		idealActivity = ACT_DAB_SLIDE;		

		return true;
	}
	
	return false;
}

bool CSDKPlayerAnimState::HandleSlideTransition( Activity &idealActivity )
{
	if (!m_pSDKPlayer->m_Shared.IsSliding())
		m_bSlideTransition = false;

	if ( m_bSlideTransition )
	{
		if (m_bSlideTransitionFirstFrame)
		{
			m_bSlideTransitionFirstFrame = false;
			RestartMainSequence();	// Reset the animation.
		}

		//Tony; check the cycle, and then stop overriding
		if ( GetBasePlayer()->GetCycle() >= 0.99 )
			m_bSlideTransition = false;
		else
			idealActivity = m_iSlideActivity;
	}

	return m_bSlideTransition;
}

bool CSDKPlayerAnimState::HandleRollTransition( Activity &idealActivity )
{
	if (!m_pSDKPlayer->m_Shared.IsRolling())
		m_bRollTransition = false;

	if ( m_bRollTransition )
	{
		if (m_bRollTransitionFirstFrame)
		{
			m_bRollTransitionFirstFrame = false;
			RestartMainSequence();	// Reset the animation.
		}

		//Tony; check the cycle, and then stop overriding
		if ( GetBasePlayer()->GetCycle() >= 0.99 )
			m_bRollTransition = false;
		else
			idealActivity = m_iRollActivity;
	}

	return m_bRollTransition;
}

#if defined ( SDK_USE_SPRINTING )
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSDKPlayerAnimState::HandleSprinting( Activity &idealActivity )
{
	if ( m_pSDKPlayer->m_Shared.IsSprinting() )
	{
		idealActivity = ACT_SPRINT;		

		return true;
	}
	
	return false;
}
#endif // SDK_USE_SPRINTING
//-----------------------------------------------------------------------------
// Purpose: 
bool CSDKPlayerAnimState::HandleJumping( Activity &idealActivity )
{
	Vector vecVelocity;
	GetOuterAbsVelocity( vecVelocity );

	if ( m_bJumping )
	{
		if ( m_bFirstJumpFrame )
		{
			m_bFirstJumpFrame = false;
			RestartMainSequence();	// Reset the animation.
		}

		// Reset if we hit water and start swimming.
		if ( m_pSDKPlayer->GetWaterLevel() >= WL_Waist )
		{
			m_bJumping = false;
			RestartMainSequence();
		}
		// Don't check if he's on the ground for a sec.. sometimes the client still has the
		// on-ground flag set right when the message comes in.
		else if ( gpGlobals->curtime - m_flJumpStartTime > 0.2f )
		{
			if ( m_pSDKPlayer->GetFlags() & FL_ONGROUND )
			{
				m_bJumping = false;
				RestartMainSequence();

				RestartGesture( GESTURE_SLOT_JUMP, ACT_DAB_JUMP_LAND );					
			}
		}

		// if we're still jumping
		if ( m_bJumping )
		{
			if ( gpGlobals->curtime - m_flJumpStartTime > 0.5 )
				idealActivity = ACT_DAB_JUMP_FLOAT;
			else
				idealActivity = ACT_DAB_JUMP_START;
		}
	}	

	if ( m_bJumping )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Overriding CMultiplayerAnimState to add prone and sprinting checks as necessary.
// Input  :  - 
// Output : Activity
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
extern ConVar anim_showmainactivity;
#endif

Activity CSDKPlayerAnimState::CalcMainActivity()
{
	Activity idealActivity = ACT_DAB_STAND_IDLE;

	if ( HandleJumping( idealActivity ) || 
#if defined ( SDK_USE_PRONE )
		//Tony; handle these before ducking !!
		HandleProneTransition( idealActivity ) ||
		HandleProne( idealActivity ) ||
#endif
		HandleRollTransition( idealActivity ) ||
		HandleSlideTransition( idealActivity ) ||
		HandleSliding( idealActivity ) ||
		HandleDucking( idealActivity ) || 
		HandleSwimming( idealActivity ) || 
		HandleDying( idealActivity ) 
#if defined ( SDK_USE_SPRINTING )
		|| HandleSprinting( idealActivity )
#endif
		)
	{
		// intentionally blank
	}
	else
	{
		HandleMoving( idealActivity );
	}

	ShowDebugInfo();

	// Client specific.
#ifdef CLIENT_DLL

	if ( anim_showmainactivity.GetBool() )
	{
		DebugShowActivity( idealActivity );
	}

#endif

	return idealActivity;
}
