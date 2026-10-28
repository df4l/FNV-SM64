#include "config.h"
#include "sm64_loader.h"

Config g_config;

void Config_Load()
{
	const std::wstring path = SM64_DataDir() + L"sm64.ini";
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		_MESSAGE("sm64.ini not found, using defaults");

	g_config.startActive = GetPrivateProfileIntW(L"General", L"bStartActive", g_config.startActive, path.c_str()) != 0;
	g_config.toggleKey = GetPrivateProfileIntW(L"Debug", L"iToggleKey", g_config.toggleKey, path.c_str());
	g_config.dumpStateKey = GetPrivateProfileIntW(L"Debug", L"iDumpStateKey", g_config.dumpStateKey, path.c_str());
	g_config.playerFollow = GetPrivateProfileIntW(L"Subsystems", L"bPlayerFollow", g_config.playerFollow, path.c_str()) != 0;
	g_config.disableControls = GetPrivateProfileIntW(L"Subsystems", L"bDisableControls", g_config.disableControls, path.c_str()) != 0;
	g_config.render = GetPrivateProfileIntW(L"Subsystems", L"bRender", g_config.render, path.c_str()) != 0;
	g_config.hidePlayer = GetPrivateProfileIntW(L"Subsystems", L"bHidePlayer", g_config.hidePlayer, path.c_str()) != 0;
	g_config.input = GetPrivateProfileIntW(L"Subsystems", L"bInput", g_config.input, path.c_str()) != 0;
	g_config.renderFlipWinding = GetPrivateProfileIntW(L"Render", L"bFlipWinding", g_config.renderFlipWinding, path.c_str()) != 0;

	g_config.renderTexture = GetPrivateProfileIntW(L"Render", L"bTexture", g_config.renderTexture, path.c_str()) != 0;
	g_config.textureSwapRB = GetPrivateProfileIntW(L"Render", L"bTextureSwapRB", g_config.textureSwapRB, path.c_str()) != 0;
	wchar_t buffer[32];
	GetPrivateProfileStringW(L"Render", L"fDecalOffset", L"0.1", buffer, 32, path.c_str());
	g_config.decalOffset = (float)_wtof(buffer);
	g_config.lighting = GetPrivateProfileIntW(L"Render", L"bLighting", g_config.lighting, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Render", L"fLightDiffuse", L"0.5", buffer, 32, path.c_str());
	g_config.lightDiffuse = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Render", L"fLightBrightness", L"1.0", buffer, 32, path.c_str());
	g_config.lightBrightness = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Render", L"fLightMin", L"0.08", buffer, 32, path.c_str());
	g_config.lightMin = (float)_wtof(buffer);
	g_config.interpolate = GetPrivateProfileIntW(L"Render", L"bInterpolate", g_config.interpolate, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Camera", L"fMaxZoomDist", L"500", buffer, 32, path.c_str());
	g_config.cameraMaxZoom = (float)_wtof(buffer);
	g_config.thirdPersonOnLoad = GetPrivateProfileIntW(L"Camera", L"bThirdPersonOnLoad", g_config.thirdPersonOnLoad, path.c_str()) != 0;
	g_config.forceThirdPerson = GetPrivateProfileIntW(L"Camera", L"bForceThirdPerson", g_config.forceThirdPerson, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Camera", L"fMinZoomDist", L"150", buffer, 32, path.c_str());
	g_config.cameraMinZoom = (float)_wtof(buffer);
	g_config.terrain = GetPrivateProfileIntW(L"Collision", L"bTerrain", g_config.terrain, path.c_str()) != 0;
	g_config.statics = GetPrivateProfileIntW(L"Collision", L"bStatics", g_config.statics, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Collision", L"fStaticRadius", L"4096", buffer, 32, path.c_str());
	g_config.staticRadius = (float)_wtof(buffer);
	g_config.maxStaticTriangles = GetPrivateProfileIntW(L"Collision", L"iMaxStaticTriangles", g_config.maxStaticTriangles, path.c_str());
	g_config.staticFlipWinding = GetPrivateProfileIntW(L"Collision", L"bStaticFlipWinding", g_config.staticFlipWinding, path.c_str()) != 0;
	g_config.doors = GetPrivateProfileIntW(L"Collision", L"bDoors", g_config.doors, path.c_str()) != 0;
	g_config.movingBodies = GetPrivateProfileIntW(L"Collision", L"bMovingBodies", g_config.movingBodies, path.c_str()) != 0;
	g_config.maxMovingBodies = GetPrivateProfileIntW(L"Collision", L"iMaxMovingBodies", g_config.maxMovingBodies, path.c_str());
	g_config.maxMovingTriangles = GetPrivateProfileIntW(L"Collision", L"iMaxMovingTriangles", g_config.maxMovingTriangles, path.c_str());
	g_config.dynamicBodies = GetPrivateProfileIntW(L"Collision", L"bDynamicBodies", g_config.dynamicBodies, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Collision", L"fMinDynamicSize", L"40", buffer, 32, path.c_str());
	g_config.minDynamicSize = (float)_wtof(buffer);
	g_config.roomPieces = GetPrivateProfileIntW(L"Collision", L"bRoomPieces", g_config.roomPieces, path.c_str()) != 0;
	g_config.streaming = GetPrivateProfileIntW(L"Collision", L"bStreaming", g_config.streaming, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Collision", L"fReloadDistance", L"2048", buffer, 32, path.c_str());
	g_config.reloadDistance = (float)_wtof(buffer);
	g_config.blockGameControls =GetPrivateProfileIntW(L"Input", L"bBlockGameControls", g_config.blockGameControls, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Input", L"fStickDeadzone", L"0.24", buffer, 32, path.c_str());
	g_config.stickDeadzone = (float)_wtof(buffer);
	g_config.showHud = GetPrivateProfileIntW(L"Input", L"bShowHud", g_config.showHud, path.c_str()) != 0;
	g_config.audio = GetPrivateProfileIntW(L"Audio", L"bAudio", g_config.audio, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Audio", L"fVolume", L"1.0", buffer, 32, path.c_str());
	g_config.audioVolume = (float)_wtof(buffer);
	g_config.healthSync = GetPrivateProfileIntW(L"Gameplay", L"bHealthSync", g_config.healthSync, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Gameplay", L"fLowHealth", L"0.25", buffer, 32, path.c_str());
	g_config.lowHealth = (float)_wtof(buffer);
	g_config.water = GetPrivateProfileIntW(L"Gameplay", L"bWater", g_config.water, path.c_str()) != 0;
	g_config.waterNeedsContact = GetPrivateProfileIntW(L"Gameplay", L"bWaterNeedsContact", g_config.waterNeedsContact, path.c_str()) != 0;
	g_config.npcColliders = GetPrivateProfileIntW(L"Gameplay", L"bNpcColliders", g_config.npcColliders, path.c_str()) != 0;
	g_config.maxNpcColliders = GetPrivateProfileIntW(L"Gameplay", L"iMaxNpcColliders", g_config.maxNpcColliders, path.c_str());
	GetPrivateProfileStringW(L"Gameplay", L"fNpcRange", L"1500", buffer, 32, path.c_str());
	g_config.npcRange = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fNpcRadius", L"20", buffer, 32, path.c_str());
	g_config.npcRadius = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fNpcHeight", L"120", buffer, 32, path.c_str());
	g_config.npcHeight = (float)_wtof(buffer);
	g_config.npcBounds = GetPrivateProfileIntW(L"Gameplay", L"bNpcBounds", g_config.npcBounds, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Gameplay", L"fNpcBoundsScale", L"1.0", buffer, 32, path.c_str());
	g_config.npcBoundsScale = (float)_wtof(buffer);
	g_config.npcAttacks = GetPrivateProfileIntW(L"Gameplay", L"bNpcAttacks", g_config.npcAttacks, path.c_str()) != 0;
	GetPrivateProfileStringW(L"Gameplay", L"fAttackReach", L"65", buffer, 32, path.c_str());
	g_config.attackReach = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fAttackReachAir", L"50", buffer, 32, path.c_str());
	g_config.attackReachAir = (float)_wtof(buffer);
	g_config.attackCooldown = GetPrivateProfileIntW(L"Gameplay", L"iAttackCooldown", g_config.attackCooldown, path.c_str());
	GetPrivateProfileStringW(L"Gameplay", L"fPunchRecoil", L"0.25", buffer, 32, path.c_str());
	g_config.punchRecoil = (float)_wtof(buffer);
	g_config.damageMode = GetPrivateProfileIntW(L"Gameplay", L"iDamageMode", g_config.damageMode, path.c_str());
	GetPrivateProfileStringW(L"Gameplay", L"fDamageMult", L"1.0", buffer, 32, path.c_str());
	g_config.damageMult = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fAttackDamage", L"20", buffer, 32, path.c_str());
	g_config.attackDamage = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fKickMult", L"1.5", buffer, 32, path.c_str());
	g_config.kickMult = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fGroundPoundMult", L"3.5", buffer, 32, path.c_str());
	g_config.groundPoundMult = (float)_wtof(buffer);
	GetPrivateProfileStringW(L"Gameplay", L"fMetalCapMult", L"11", buffer, 32, path.c_str());
	g_config.metalCapMult = (float)_wtof(buffer);
	g_config.knockForce = GetPrivateProfileIntW(L"Gameplay", L"iKnockForce", g_config.knockForce, path.c_str());

	_MESSAGE("config: bStartActive=%d iToggleKey=0x%02X iDumpStateKey=0x%02X bPlayerFollow=%d bDisableControls=%d "
		"bRender=%d bHidePlayer=%d bInput=%d bFlipWinding=%d bTexture=%d bTextureSwapRB=%d fDecalOffset=%.3f bInterpolate=%d "
		"bLighting=%d fLightDiffuse=%.2f fLightBrightness=%.2f fLightMin=%.2f fMaxZoomDist=%.0f bThirdPersonOnLoad=%d bTerrain=%d bBlockGameControls=%d fStickDeadzone=%.2f bShowHud=%d", g_config.startActive,
		g_config.toggleKey, g_config.dumpStateKey, g_config.playerFollow, g_config.disableControls, g_config.render,
		g_config.hidePlayer, g_config.input, g_config.renderFlipWinding, g_config.renderTexture, g_config.textureSwapRB, g_config.decalOffset,
		g_config.interpolate, g_config.lighting, g_config.lightDiffuse, g_config.lightBrightness, g_config.lightMin, g_config.cameraMaxZoom,
		g_config.thirdPersonOnLoad, g_config.terrain, g_config.blockGameControls, g_config.stickDeadzone, g_config.showHud);
	_MESSAGE("config: bForceThirdPerson=%d fMinZoomDist=%.0f bRoomPieces=%d", g_config.forceThirdPerson,
		g_config.cameraMinZoom, g_config.roomPieces);
	_MESSAGE("config: bStatics=%d fStaticRadius=%.0f iMaxStaticTriangles=%u bStaticFlipWinding=%d bDoors=%d bStreaming=%d "
		"fReloadDistance=%.0f", g_config.statics, g_config.staticRadius, g_config.maxStaticTriangles,
		g_config.staticFlipWinding, g_config.doors, g_config.streaming, g_config.reloadDistance);
	_MESSAGE("config: bMovingBodies=%d iMaxMovingBodies=%u iMaxMovingTriangles=%u bDynamicBodies=%d fMinDynamicSize=%.0f",
		g_config.movingBodies, g_config.maxMovingBodies, g_config.maxMovingTriangles, g_config.dynamicBodies, g_config.minDynamicSize);
	_MESSAGE("config: bAudio=%d fVolume=%.2f bHealthSync=%d fLowHealth=%.2f bWater=%d bWaterNeedsContact=%d", g_config.audio,
		g_config.audioVolume, g_config.healthSync, g_config.lowHealth, g_config.water, g_config.waterNeedsContact);
	_MESSAGE("config: bNpcColliders=%d iMaxNpcColliders=%u fNpcRange=%.0f fNpcRadius=%.1f fNpcHeight=%.1f bNpcBounds=%d "
		"fNpcBoundsScale=%.2f", g_config.npcColliders, g_config.maxNpcColliders, g_config.npcRange, g_config.npcRadius,
		g_config.npcHeight, g_config.npcBounds, g_config.npcBoundsScale);
	_MESSAGE("config: bNpcAttacks=%d fAttackReach=%.1f fAttackReachAir=%.1f iAttackCooldown=%u fPunchRecoil=%.2f iDamageMode=%d fDamageMult=%.2f "
		"fAttackDamage=%.1f fKickMult=%.2f fGroundPoundMult=%.2f fMetalCapMult=%.2f iKnockForce=%d", g_config.npcAttacks,
		g_config.attackReach, g_config.attackReachAir, g_config.attackCooldown, g_config.punchRecoil, g_config.damageMode, g_config.damageMult,
		g_config.attackDamage, g_config.kickMult, g_config.groundPoundMult, g_config.metalCapMult, g_config.knockForce);
}
