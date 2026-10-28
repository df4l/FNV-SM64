#pragma once

// Settings from <Plugins>\sm64\sm64.ini, read once at PostLoad. Missing keys keep these defaults.
struct Config
{
	bool startActive = true;	// [General] bStartActive, master toggle state at game start
	int toggleKey = 0x77;		// [Debug] iToggleKey, Win32 virtual-key code (default F8; F9 is quickload)
	int dumpStateKey = 0x79;	// [Debug] iDumpStateKey, Win32 virtual-key code (default F10)
	bool playerFollow = true;	// [Subsystems] bPlayerFollow, snap the player ref onto Mario every frame
	bool disableControls = true;	// [Subsystems] bDisableControls, turn off player movement while following Mario
	bool render = true;			// [Subsystems] bRender, show Mario's mesh
	bool hidePlayer = true;		// [Subsystems] bHidePlayer, hide the player's 3rd-person body while Mario is spawned
	bool input = true;			// [Subsystems] bInput, drive Mario with the game's controls (0 = no input, as before M4)
	bool renderFlipWinding = false;	// [Render] bFlipWinding, reverse the triangle order (Mario drawn inside out)
	bool renderTexture = true;		// [Render] bTexture, draw the textured overlay (face, eyes, buttons, logo)
	bool textureSwapRB = false;		// [Render] bTextureSwapRB, swap red/blue when copying the atlas
	float decalOffset = 0.1f;		// [Render] fDecalOffset, game units the overlay is pushed out along the normal
	bool lighting = true;			// [Render] bLighting, tint Mario by the scene's sun light (ambient + diffuse)
	float lightDiffuse = 0.5f;		// [Render] fLightDiffuse, weight of the sun's diffuse colour in the tint
	float lightBrightness = 1.0f;	// [Render] fLightBrightness, overall multiplier
	float lightMin = 0.08f;			// [Render] fLightMin, floor per channel so Mario never goes fully black
	bool interpolate = true;		// [Render] bInterpolate, blend the last two 30 Hz ticks every frame (mesh + player follow)
	float cameraMaxZoom = 500.0f;	// [Camera] fMaxZoomDist, 3rd-person zoom-out limit while Mario is spawned (0 = game value)
	bool thirdPersonOnLoad = true;	// [Camera] bThirdPersonOnLoad, first spawn after a load/new game: 3rd person, zoomed fully out
	bool forceThirdPerson = true;	// [Camera] bForceThirdPerson, switch back to 3rd person whenever the view goes to 1st while Mario is spawned (M6.1)
	float cameraMinZoom = 150.0f;	// [Camera] fMinZoomDist, zoom distance set when the view is forced back to 3rd person
	bool terrain = true;			// [Collision] bTerrain, stand on the exterior terrain (M5.1); 0 = test floor everywhere
	bool statics = true;			// [Collision] bStatics, stand on / bump into the fixed Havok collision of nearby refs (M5.2)
	float staticRadius = 4096.0f;	// [Collision] fStaticRadius, statics are loaded this far around the player (game units)
	unsigned maxStaticTriangles = 150000;	// [Collision] iMaxStaticTriangles, cap on static triangles
	bool staticFlipWinding = false;	// [Collision] bStaticFlipWinding, reverse the stored winding of Havok meshes
	bool doors = true;				// [Collision] bDoors, doors collide in the pose they have when the collision is built (M5.4d)
	bool movingBodies = true;		// [Collision] bMovingBodies, animated doors, lifts and moving platforms follow their Havok body (M8.1)
	unsigned maxMovingBodies = 128;	// [Collision] iMaxMovingBodies, nearest moving bodies (keyframed + dynamic) kept per build
	bool dynamicBodies = true;		// [Collision] bDynamicBodies, loose objects (crates, barrels, furniture) collide and follow their body (M8.2)
	float minDynamicSize = 40.0f;	// [Collision] fMinDynamicSize, a loose object's largest dimension must reach this (game units)
	unsigned maxMovingTriangles = 4096;	// [Collision] iMaxMovingTriangles, cap per moving body
	bool roomPieces = true;			// [Collision] bRoomPieces, interior kit pieces (floor + walls + ceiling) get floors up, ceilings down (M7.1c)
	bool streaming = true;			// [Collision] bStreaming, rebuild terrain + statics around Mario as he moves (M5.3)
	float reloadDistance = 2048.0f;	// [Collision] fReloadDistance, Mario this far from the last build centre = rebuild
	bool blockGameControls = true;	// [Input] bBlockGameControls, ignore aim/ready/VATS/hotkeys/grab while Mario is active
	float stickDeadzone = 0.24f;	// [Input] fStickDeadzone, gamepad left-stick radial deadzone (0..1; game uses 7849/32767)
	bool showHud = true;			// [Input] bShowHud, keep the HUD visible while our control bits are set
	bool audio = true;				// [Audio] bAudio, Mario's voice and sound effects through the game's sound device (M9)
	float audioVolume = 1.0f;		// [Audio] fVolume, 0..1 on top of the game's Master and Effects sliders
	bool healthSync = true;		// [Gameplay] bHealthSync, Mario's health follows the player's, and his own damage (falls) hurts the player (M10.1)
	float lowHealth = 0.25f;	// [Gameplay] fLowHealth, player health fraction at or below which Mario is low (pants when idle)
	bool water = true;			// [Gameplay] bWater, Mario swims in the game's water (M10.2)
	bool waterNeedsContact = true;	// [Gameplay] bWaterNeedsContact, water level only while the player is in water (SA); 0 = whenever the game has a level
	bool npcColliders = true;	// [Gameplay] bNpcColliders, living NPCs and creatures near Mario push him back (M10.3, SA ped colliders)
	unsigned maxNpcColliders = 32;	// [Gameplay] iMaxNpcColliders, nearest actors given a collider
	float npcRange = 1500.0f;	// [Gameplay] fNpcRange, actors farther than this from Mario get no collider (game units)
	float npcRadius = 20.0f;	// [Gameplay] fNpcRadius, collider radius when the base form has no bounds (game units, times the actor's scale)
	float npcHeight = 120.0f;	// [Gameplay] fNpcHeight, collider height when the base form has no bounds (game units, times its scale)
	bool npcBounds = true;		// [Gameplay] bNpcBounds, size each collider from its base form's object bounds (OBND) (M10.4b)
	float npcBoundsScale = 1.0f;	// [Gameplay] fNpcBoundsScale, multiplier on the bounds-derived radius
	bool npcAttacks = true;		// [Gameplay] bNpcAttacks, Mario's punches/kicks/stomps damage collider NPCs (M10.4, SA)
	float attackReach = 65.0f;	// [Gameplay] fAttackReach, on the ground: horizontal reach beyond the collider's edge (game units)
	float attackReachAir = 50.0f;	// [Gameplay] fAttackReachAir, in the air: reach beyond the collider's edge, sideways and vertically
	unsigned attackCooldown = 4;	// [Gameplay] iAttackCooldown, minimum ticks (30 Hz) between two damaging strikes on the same actor
	float punchRecoil = 0.25f;	// [Gameplay] fPunchRecoil, scale of Mario's bounce-back after a punch/kick contact (1 = SM64/SA)
	int damageMode = 1;			// [Gameplay] iDamageMode, 1 = vanilla unarmed damage + target DT/DR, 0 = fAttackDamage (SA, ignores armour)
	float damageMult = 1.0f;	// [Gameplay] fDamageMult, applied to every hit
	float attackDamage = 20.0f;	// [Gameplay] fAttackDamage, base damage of a hit in iDamageMode 0 (SA 20)
	float kickMult = 1.5f;		// [Gameplay] fKickMult, jump kick / slide kick / butt and belly slides, which also knock down (SA 30/20)
	float groundPoundMult = 3.5f;	// [Gameplay] fGroundPoundMult, falling ground pound (SA 70/20)
	float metalCapMult = 11.0f;	// [Gameplay] fMetalCapMult, any hit with the metal cap, which also knocks down (SA 220/20)
	int knockForce = 40;		// [Gameplay] iKnockForce, PushActorAway force for knockdowns (0 = never knock down)
};

extern Config g_config;

void Config_Load();
