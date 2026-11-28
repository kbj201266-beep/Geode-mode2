#pragma once
// The physics state of a PlayerObject that the game's own checkpoints do NOT keep.
// GD checkpoints only keep part of the player (position, speed, gamemode...). Everything else - y velocity
// precision, ground / slope state, jump buffer, held buttons, dash, collision bookkeeping - stays whatever it was,
// so a run from a checkpoint is not the same as the recorded run. Bots (xdBot, Antic's Counter) save these fields
// and put them back after loading a checkpoint. Used by the window counter (checkpoints) and the pathfinder
// (exact copy of the real player into the preview dummy).
#include <Geode/Geode.hpp>
#include <type_traits>

#define KT_PLAYER_FIELDS(X) \
    X(m_wasTeleported) X(m_fixGravityBug) X(m_reverseSync) X(m_yVelocityBeforeSlope) X(m_dashX) X(m_dashY) \
    X(m_dashAngle) X(m_dashStartTime) X(m_dashRing) X(m_slopeStartTime) X(m_maybeLastGroundObject) \
    X(m_lastCollisionBottom) X(m_lastCollisionTop) X(m_lastCollisionLeft) X(m_lastCollisionRight) \
    X(m_prevCollisionBottom) X(m_prevCollisionTop) X(m_currentSlope2) X(m_preLastGroundObject) X(m_slopeAngle) \
    X(m_slopeSlidingMaybeRotated) X(m_collidedObject) X(m_lastGroundObject) X(m_collidingWithLeft) \
    X(m_collidingWithRight) X(m_maybeSavedPlayerFrame) X(m_scaleXRelated2) X(m_groundYVelocity) \
    X(m_yVelocityRelated) X(m_scaleXRelated3) X(m_collidingBetweenSteps) X(m_scaleXRelated4) X(m_scaleXRelated5) \
    X(m_isCollidingWithSlope) X(m_isBallRotating) X(m_noPotentialSlope) X(m_currentPotentialSlope) \
    X(m_currentSlope) X(m_slopeCenterOffset) X(m_collidingWithSlopeId) X(m_slopeIsDownhill) X(m_slopeAngleRadians) \
    X(m_rotateObjectsRelated) X(m_potentialSlopeMap) X(m_rotationSpeed) X(m_rotateSpeed) X(m_isRotating) \
    X(m_ballSpinning) X(m_speedMultiplier) X(m_yStart) X(m_gravity) X(m_gameModeChangedTime) \
    X(m_ringJumpedSincePress) X(m_hasStartedFalling) X(m_landedSinceFlip) X(m_lastJumpTime) X(m_lastFlipTime) \
    X(m_lastSpiderFlipTime) X(m_accelerationOrSpeed) X(m_snapDistance) X(m_usedRingThisStep) X(m_usedRings) \
    X(m_objectSnappedTo) X(m_slopeRotation) X(m_currentSlopeYVelocity) X(m_lastSlopeSnapY) \
    X(m_lastSlopeSnapOffset) X(m_lastSlopeSnapAtFarEdge) X(m_lastSlopeSnapAtNearEdge) X(m_isAccelerating) \
    X(m_isCurrentSlopeTop) X(m_collidedTopMinY) X(m_collidedBottomMaxY) X(m_collidedLeftMaxX) \
    X(m_collidedRightMinX) X(m_maybeIsColliding) X(m_jumpBuffered) X(m_stateRingJump) X(m_wasJumpBuffered) \
    X(m_wasRobotJump) X(m_stateJumpBuffered) X(m_statePreJump) X(m_touchedRing) X(m_touchedCustomRing) \
    X(m_touchedGravityPortal) X(m_brokeBlockSincePress) X(m_touchedPad) X(m_yVelocity) X(m_fallSpeed) \
    X(m_isOnSlope) X(m_wasOnSlope) X(m_slopeVelocity) X(m_slopeResnapped) X(m_isShip) X(m_isBird) X(m_isBall) \
    X(m_isDart) X(m_isRobot) X(m_isSpider) X(m_isUpsideDown) X(m_isOnGround) X(m_isGoingLeft) X(m_isSideways) \
    X(m_isSwing) X(m_reverseCooldown) X(m_maybeReverseSpeed) X(m_reverseSyncStep) X(m_ballRollSpeed) \
    X(m_isDashing) X(m_dashFireFrame) X(m_groundObjectMaterial) X(m_vehicleSize) X(m_playerSpeed) \
    X(m_shipRotation) X(m_lastPortalPos) X(m_touchingGround) X(m_lastLandTime) X(m_decelerationRamp) \
    X(m_isRising) X(m_lastGroundXDeltaTime) X(m_decreaseBoostSlide) X(m_isLocked) X(m_controlsDisabled) \
    X(m_lastGroundedPos) X(m_touchedRings) X(m_lastActivatedPortal) X(m_hasEverJumped) X(m_hasEverHitRing) \
    X(m_position) X(m_totalTime) X(m_diagonalYStep) X(m_stateOnGround) X(m_stateBoostY) X(m_stateBoostX) \
    X(m_stateNoWallPush) X(m_stateScale) X(m_platformerXVelocity) X(m_holdingRight) X(m_holdingLeft) \
    X(m_leftPressedFirst) X(m_platformXMove) X(m_hasPlatformMomentum) X(m_platformMomentum) \
    X(m_forcedSlopeSlide) X(m_isSliding) X(m_slopeSpeedBonus) X(m_isOnIce) X(m_platformRideTime) \
    X(m_wasTouchingGround) X(m_wasCollidingTopOrBottom) X(m_slopeJumpCount) X(m_slopeJumpResetTime) \
    X(m_changedDirectionsTime) X(m_slopeEndTime) X(m_isMoving) X(m_forcedSlideLeft) X(m_forcedSlideRight) \
    X(m_lastSlideLeft) X(m_slideSlopeAngle) X(m_isPlatformer) X(m_stateNoAutoJump) X(m_stateDartSlide) \
    X(m_stateHitHead) X(m_stateFlipGravity) X(m_gravityMod) X(m_stateForce) X(m_stateForceVector) \
    X(m_affectedByForces) X(m_touchedForceIds) X(m_lastMovedTime) X(m_playerSpeedAC) X(m_fixRobotJump) \
    X(m_holdingButtons) X(m_inputsLocked) X(m_isOutOfBounds) X(m_fallStartY) X(m_ignoreDamage) \
    X(m_enable22Changes) X(m_enableImpulseFix)

namespace kt {

struct PState {
#define X(f) std::remove_cvref_t<decltype(PlayerObject::f)> f{};
    KT_PLAYER_FIELDS(X)
#undef X
    cocos2d::CCPoint pos;
    float rot = 0.f;
    bool valid = false;
};

inline void savePlayer(PlayerObject* p, PState& st) {
    st.valid = p != nullptr;
    if (!p) return;
#define X(f) st.f = p->f;
    KT_PLAYER_FIELDS(X)
#undef X
    st.pos = p->getPosition();
    st.rot = p->getRotation();
}

inline void loadPlayer(PlayerObject* p, PState const& st) {
    if (!p || !st.valid) return;
#define X(f) p->f = st.f;
    KT_PLAYER_FIELDS(X)
#undef X
    p->m_isDead = false;
    p->setPosition(st.pos);
    p->setRotation(st.rot);
}

// dst becomes an exact physics copy of src (used for the pathfinder dummy)
inline void copyPlayer(PlayerObject* dst, PlayerObject* src) {
    if (!dst || !src) return;
#define X(f) dst->f = src->f;
    KT_PLAYER_FIELDS(X)
#undef X
    dst->m_isDead = false;
    dst->setPosition(src->getPosition());
    dst->setRotation(src->getRotation());
}

} // namespace kt
