#pragma once

#include <workspace/core/entity/node.hxx>
#include <workspace/core/entity/registry.hxx>
#include <workspace/core/world/transform.hxx>
#include <workspace/core/entity/corona.hxx>
#include <workspace/core/entity/draw.hxx>
#include <workspace/core/entity/rigid.hxx>
#include <workspace/core/entity/rig.hxx>
#include <workspace/core/entity/gfx.hxx>
#include <workspace/core/entity/trigger.hxx>
#include <workspace/core/entity/pfx.hxx>
#include <workspace/core/entity/actions.hxx>
#include <workspace/core/game/plugs.hxx>
#include <workspace/core/world/surface.hxx>

#include <cstring>

// The game's entity types: 0x4D4180 (called from the game's start-up
// 0x4024E0) registers them into g_vuentityregistry after the engine's - its
// first call, 0x62A010, adds the ten UI containers / elements the rows open
// with. 147 registerType calls in retail's order (captured by hydro-tools
// census/regcalls.py, printed by codegen/registry.py game). Each create fn is
// a retail address: a hook until that type is recreated.
inline const entity_type_row_t g_game_entity_types[] = {
    { "VuUIContainerEntity", "UI/Containers", "UI Folder", 0x62F800u },
    { "VuUIScreenEntity", "UI/Containers", "Screen", 0x6299A0u },
    { "VuUIImageEntity", "UI/Elements", "Image", 0x62F590u },
    { "VuUITextEntity", "UI/Elements", "Text", 0x62F2B0u },
    { "VuUIScrollingTextEntity", "UI/Elements", "ScrollingText", 0x62EC00u },
    { "VuUIDebugTextEntity", "UI/Elements", "DebugText", 0x62EA80u },
    { "VuUIEventEntity", "UI/Events", "Event", 0x62E6D0u },
    { "VuUIPageLayoutEntity", "UI/Elements", "PageLayout", 0x62CA50u },
    { "VuUIProgressBarEntity", "UI/Elements", "ProgressBar", 0x62B550u },
    { "VuUITouchButtonEntity", "UI/Elements", "TouchButton", 0x62A8A0u },
    { "VuAddBoostEntity", "Actions", "AddBoost", 0x4055C0u },
    { "VuSignInEntity", "Actions", "SignIn", 0x405810u },
    { "VuUnlockAchievementEntity", "Actions", "UnlockAchievement", 0x406080u },
    { "VuStartLightsOutAction", "Actions", "StartLightsOut", 0x4054D0u },
    { "VuSetTrackBranchProbabilities", "Actions", "SetTrackBranchProbabilities", 0x406380u },
    { "VuSayVoiceOverEntity", "Actions", "SayVoiceOver", 0x407E40u },
    { "VuSayVoiceOverEllipsoidEntity", "Actions", "SayVoiceOverEllipsoid", 0x4081D0u },
    { "VuSayVoiceOverBoxEntity", "Actions", "SayVoiceOverBox", 0x408610u },
    { "VuSetFrontEndCameraEntity", "Actions", "SetFrontEndCamera", 0x408A80u },
    { "VuCheatEntity", "Actions", "Cheat", 0x404C90u },
    { "VuSentientUpsellPresented", "Actions", "SentientUpsellPresented", 0x4067F0u },
    { "VuSentientUpsellResponded", "Actions", "SentientUpsellResponded", 0x4069F0u },
    { "VuOnTouchSpecialEntity", "Events", "OnTouchSpecial", 0x475EC0u },
    { "VuUIPadCheatComboEvent", "Events", "OnPadCheatCombo", 0x476260u },
    { "VuSysNotifyEntity", "Events", "OnSysNotify", 0x423050u },
    { "VuOnAnyButtonOrKeyEntity", "Events", "OnAnyButtonOrKey", 0x4233B0u },
    { "VuIsEventTypeEntity", "Logic", "IsEventType", 0x42C7E0u },
    { "VuIsSkuEntity", "Logic", "IsSku", 0x42CCF0u },
    { "VuHumanFilterEntity", "Logic", "HumanFilter", 0x42D160u },
    { "VuOncePerLapFilterEntity", "Logic", "OncePerLapFilter", 0x42D4C0u },
    { "VuIsTegraEntity", "Logic", "IsTegra", 0x42D820u },
    { "VuLicenseEntity", "Logic", "License", 0x42DBF0u },
    { "VuIsPauseMenuActive", "Logic", "IsPauseMenuActive", 0x42DE80u },
    { "VuIsCircuitTrack", "Logic", "IsCircuitTrack", 0x42E050u },
    { "VuIsAchievementUnlockedEntity", "Logic", "IsAchievementUnlocked", 0x42E250u },
    { "VuControlMethodEntity", "Logic", "ControlMethod", 0x42E6C0u },
    { "VuAwardFlowEntity", "Logic", "AwardFlow", 0x42ED30u },
    { "VuBoatEntity", "Objects", "Boat", 0x41B690u },
    { "VuRampEntity", "Objects", "Ramp", 0x43E340u },
    { "VuAiWaypointEntity", "Objects", "AiWaypoint", 0x431680u },
    { "VuFrontEndCameraEntity", "Objects", "FrontEndCamera", 0x43A210u },
    { "VuDevJumpPointEntity", "Objects", "DevJumpPoint", 0x4352C0u },
    { "VuGridPositionEntity", "Objects", "GridPosition", 0x43A5F0u },
    { "VuBreakablePropEntity", "Objects", "BreakableProp", 0x433EE0u },
    { "VuBreakableGamePropEntity", "Objects", "BreakableGameProp", 0x433C70u },
    { "VuDynamicGamePropEntity", "Objects", "DynamicGameProp", 0x436740u },
    { "VuBreakableDynamicGamePropEntity", "Objects", "BreakableDynamicGameProp", 0x433040u },
    { "VuPowerUpBoostEntity", "Objects", "PowerUpBoost", 0x43D750u },
    { "VuPowerUpTriggerEntity", "Objects", "PowerUpTrigger", 0x43E0E0u },
    { "VuPowerUpHiddenPackageEntity", "Objects", "HiddenPackage", 0x43DC00u },
    { "VuAiNuisanceDriverStartEntity", "Objects", "AiNuisanceDriverStart", 0x42F930u },
    { "VuFloatingMineEntity", "Objects", "FloatingMine", 0x4385F0u },
    { "VuForceAffectorEntity", "Objects", "ForceAffector", 0x4391C0u },
    { "VuAIUIDriverStartEntity", "Objects", "AIUIDriverStart", 0x430520u },
    { "VuDockedBoatEntity", "Objects", "DockedBoat", 0x435800u },
    { "VuPosterEntity", "Objects", "Poster", 0x43A7F0u },
    { "VuSlalomGateEntity", "Objects", "SlalomGate", 0x43F250u },
    { "VuCheckpointBuoyEntity", "Objects", "CheckpointBuoy", 0x434EE0u },
    { "VuScreenFadeEllipsoidEntity", "Objects", "ScreenFadeEllipsoid", 0x43E870u },
    { "VuScreenFadeBoxEntity", "Objects", "ScreenFadeBox", 0x43EC60u },
    { "VuSetScreenUIAction", "UI/Actions", "SetScreen", 0x471440u },
    { "VuPushScreenUIAction", "UI/Actions", "PushScreen", 0x471700u },
    { "VuPopScreenUIAction", "UI/Actions", "PopScreen", 0x471A20u },
    { "VuReloadScreenUIAction", "UI/Actions", "ReloadScreen", 0x471B10u },
    { "VuStartActionGameUIAction", "UI/Actions", "StartActionGame", 0x4708B0u },
    { "VuEndActionGameUIAction", "UI/Actions", "EndActionGame", 0x470BE0u },
    { "VuResumeActionGameUIAction", "UI/Actions", "ResumeActionGame", 0x470CD0u },
    { "VuRestartActionGameUIAction", "UI/Actions", "RestartActionGame", 0x470DC0u },
    { "VuUserNextUIAction", "UI/Actions", "UserNext", 0x470EB0u },
    { "VuDoesChampDataExistUIAction", "UI/Actions", "DoesChampDataExist", 0x471220u },
    { "VuClearChampDataUIAction", "UI/Actions", "ClearChampData", 0x4712C0u },
    { "VuShutdownAppUIAction", "UI/Actions", "ShutdownApp", 0x4713B0u },
    { "VuShowWebPageUIAction", "UI/Actions", "ShowWebPage", 0x471BD0u },
    { "VuSetInfoStringUIAction", "UI/Actions", "SetInfoString", 0x471F20u },
    { "VuSetMenuPresenceUIAction", "UI/Actions", "SetMenuPresence", 0x471FA0u },
    { "VuSetBoatConfigPlayerEntity", "UI/Actions", "SetBoatConfigPlayer", 0x44C980u },
    { "VuClearGameResultsUIAction", "UI/Actions", "ClearGameResults", 0x4721D0u },
    { "VuDoesChampionshipDataExistUIAction", "UI/Actions", "DoesChampionshipDataExist", 0x472540u },
    { "VuClearChampionshipDataUIAction", "UI/Actions", "ClearChampionshipData", 0x4725C0u },
    { "VuSetGameModeUIAction", "UI/Actions", "SetGameMode", 0x472790u },
    { "VuMessageBoxEntity", "UI", "MessageBox", 0x463750u },
    { "VuMessageBoxScreenEntity", "UI/Containers", "MessageBoxScreen", 0x4643B0u },
    { "VuSellScreen", "UI/Containers", "SellScreen", 0x46BD70u },
    { "VuGameButtonEntity", "UI/Elements", "GameButton", 0x45B190u },
    { "VuUIButtonEntity", "UI/Elements", "Button", 0x4728B0u },
    { "VuBarracudaUIButtonEntity", "UI/Elements", "BarracudaButton", 0x441040u },
    { "VuRaceCompletionUITextEntity", "UI/Elements", "RaceCompletionText", 0x458300u },
    { "VuSlalomCompletionUITextEntity", "UI/Elements", "SlalomCompletionText", 0x458840u },
    { "VuTimeTrialCompletionUITextEntity", "UI/Elements", "TimeTrialCompletionText", 0x458E60u },
    { "VuChampionshipCompletionUITextEntity", "UI/Elements", "ChampionshipCompletionText", 0x4593A0u },
    { "VuBarracudaEventUITextEntity", "UI/Elements", "BarracudaEventText", 0x443E00u },
    { "VuBarracudaSlalomPenaltyUITextEntity", "UI/Elements", "BarracudaSlalomPenaltyText", 0x4440B0u },
    { "VuBarracudaTeamScoreUITextEntity", "UI/Elements", "BarracudaTeamScoreText", 0x4442A0u },
    { "VuBarracudaTrackUITextEntity", "UI/Elements", "BarracudaTrackText", 0x444540u },
    { "VuBarracudaTuningUITextEntity", "UI/Elements", "BarracudaTuningText", 0x444730u },
    { "VuBarracudaUnlockUITextEntity", "UI/Elements", "BarracudaUnlockText", 0x444BF0u },
    { "VuInvalidNetGameUITextEntity", "UI/Elements", "InvalidNetGameText", 0x445030u },
    { "VuShortUnlockUITextEntity", "UI/Elements", "ShortUnlockText", 0x445450u },
    { "VuDifficultyTextEntity", "UI/Elements", "DifficultyText", 0x45A2E0u },
    { "VuDifficultyImageEntity", "UI/Elements", "DifficultyImage", 0x45AB80u },
    { "VuBarracudaUICreditTextEntity", "UI/Elements", "BarracudaCreditText", 0x443B60u },
    { "VuBarracudaUIAwardImageEntity", "UI/Elements", "BarracudaAwardImage", 0x440B80u },
    { "VuBarracudaUITrophyImageEntity", "UI/Elements", "BarracudaTrophyImage", 0x446AE0u },
    { "VuBarracudaUIUnlockInfoTextEntity", "UI/Elements", "BarracudaUnlockInfoText", 0x446FE0u },
    { "VuBarracudaUIUnlockCatTextEntity", "UI/Elements", "BarracudaUnlockCatText", 0x446E80u },
    { "VuBarracudaUITrophyTextEntity", "UI/Elements", "BarracudaTrophyText", 0x446CB0u },
    { "VuBarracudaTrackUIImageEntity", "UI/Elements", "BarracudaTrackImage", 0x4401F0u },
    { "VuPackFileVersionsUITextEntity", "UI/Elements", "PackFileVersionsText", 0x4452A0u },
    { "VuCurrentAchievementUITextEntity", "UI/Elements", "CurrentAchievementText", 0x4455D0u },
    { "VuAchievementNameUITextEntity", "UI/Elements", "AchievementName", 0x445770u },
    { "VuAchievementValueUITextEntity", "UI/Elements", "AchievementValue", 0x4459D0u },
    { "VuGamerTagUITextEntity", "UI/Elements", "GamerTag", 0x445D40u },
    { "VuCreditsUITextEntity", "UI/Elements", "Credits", 0x445E60u },
    { "VuInfoUITextEntity", "UI/Elements", "Info", 0x4460A0u },
    { "VuLeaderboardRowCountUITextEntity", "UI/Elements", "LeaderboardRowCount", 0x4468D0u },
    { "VuLoadingProgressBarEntity", "UI/Elements", "LoadingProgressBar", 0x462C80u },
    { "VuRaceResultsTableEntity", "UI/Objects", "RaceResultsTable", 0x468610u },
    { "VuChampionshipEntity", "UI/Objects", "Championship", 0x44FEE0u },
    { "VuSetDefaultSettingsEntity", "UI/Settings", "SetDefaultSettings", 0x46BE30u },
    { "VuSaveSettingsEntity", "UI/Settings", "SaveSettings", 0x46C2B0u },
    { "VuTiltSteeringSettingsEntity", "UI/Settings", "TiltSteering", 0x46C4A0u },
    { "VuSteeringSensitivitySettingsEntity", "UI/Settings", "SteeringSensitivity", 0x46CA50u },
    { "VuVibrationSettingsEntity", "UI/Settings", "Vibration", 0x46CCE0u },
    { "VuEffectVolumeSettingsEntity", "UI/Settings", "EffectVolume", 0x46D080u },
    { "VuMusicVolumeSettingsEntity", "UI/Settings", "MusicVolume", 0x46D1B0u },
    { "VuDisplayScaleSettingsEntity", "UI/Settings", "DisplayScale", 0x46D2C0u },
    { "VuUIUnitsSettingEntity", "UI/Settings", "Units", 0x46D350u },
    { "VuUISubtitlesSettingEntity", "UI/Settings", "Subtitles", 0x46D710u },
    { "VuUIFidelitySettingEntity", "UI/Settings", "Fidelity", 0x46D9E0u },
    { "VuUIFlipIntervalSettingEntity", "UI/Settings", "FlipInterval", 0x46DDA0u },
    { "VuInputRemappingEntity", "UI/Input", "InputRemapping", 0x45FBB0u },
    { "VuThrottleRemappingEntity", "UI/Input", "ThrottleRemapping", 0x462910u },
    { "VuHUDEntity", "UI/HUD", "HUD", 0x423940u },
    { "VuHUDElementEntity", "UI/HUD", "TextElement", 0x423BF0u },
    { "VuHUDHiddenPackageToastEntity", "UI/HUD", "HiddenPackageToast", 0x4242E0u },
    { "VuHUDRecoverButtonEntity", "UI/HUD", "RecoverButton", 0x425A60u },
    { "VuHUDPauseButtonEntity", "UI/HUD", "PauseButton", 0x426120u },
    { "VuHUDSpeedometerEntity", "UI/HUD", "Speedometer", 0x42AA00u },
    { "VuHUDMiniMapEntity", "UI/HUD", "MiniMap", 0x426C20u },
    { "VuHUDOnScreenButtonEntity", "UI/HUD", "OnScreenButton", 0x42A3C0u },
    { "VuHUDOnScreenAxisEntity", "UI/HUD", "OnScreenAxis", 0x42A640u },
    { "VuBoatConfigEntity", "UI/Objects", "BoatConfig", 0x44C7B0u },
    { "VuGameConfigEntity", "UI/Objects", "GameConfig", 0x45CBE0u },
    { "VuUILeaderboardEntity", "UI/Objects", "Leaderboard", 0x477DD0u },
    { "VuUIMiniLeaderboardEntity", "UI/Objects", "MiniLeaderboard", 0x47CAA0u },
    { "VuUIGhostLeaderboardEntity", "UI/Objects", "GhostLeaderboard", 0x4764D0u },
    { "VuBoatStatEntity", "UI/Objects", "BoatStat", 0x44F4C0u },
};

// The whole registry as retail's start-up leaves it: 0x5685B0, 0x556AC0 over
// the VuTemplateAsset names, then 0x4D4180.
inline entity_registry_t build_entity_registry( const std::vector< std::string >& template_names )
{
    entity_registry_t registry;
    registry.register_rows( g_engine_entity_types, sizeof( g_engine_entity_types ) / sizeof( g_engine_entity_types[ 0 ] ) );
    registry.register_templates( template_names );
    registry.register_rows( g_game_entity_types, sizeof( g_game_entity_types ) / sizeof( g_game_entity_types[ 0 ] ) );
    return registry;
}
// What each type's create fn leaves, captured by running it under emulation
// (hydro-tools census/createprobe.py, printed by codegen/entityfacts.py): the
// vtable, the VuEntity base ctor's flags (+0x14) - for every type whose create
// ran clean with no faked calls, whose ctor made no children of its own, and
// whose slot 11 is VuEntity::load 0x5009D0 (both asserted by the codegen). A
// type not listed is an UNKNOWN entity. Sorted by class.
inline const entity_ctor_t g_entity_ctors[] = {
    { "VuAIUIDriverStartEntity", 0x739BF0u, 0u },
    { "VuAchievementNameUITextEntity", 0x73BD5Cu, 0u },
    { "VuAchievementValueUITextEntity", 0x73BD1Cu, 0u },
    { "VuAddBoostEntity", 0x73762Cu, 0u },
    { "VuAddIntegerConstantEntity", 0x75415Cu, 0u },
    { "VuAddIntegersEntity", 0x754128u, 0u },
    { "VuAiNuisanceDriverStartEntity", 0x739B94u, 0u },
    { "VuAiWaypointEntity", 0x739D44u, 0u },
    { "VuAmbientLightEntity", 0x756594u, 0u },
    { "VuAnimatedPropEntity", 0x75623Cu, 0u },
    { "VuAttachEntity", 0x7571A8u, 0u },
    { "VuAudioAmbientReverbEntity", 0x756B70u, 0u },
    { "VuAudioBankEntity", 0x75281Cu, 0u },
    { "VuAudioDuckingEntity", 0x75696Cu, 0u },
    { "VuAudioEmitterEntity", 0x756E6Cu, 0u },
    { "VuAudioEventEntity", 0x756F30u, 0u },
    { "VuAudioProjectEntity", 0x7527E8u, 0u },
    { "VuAudioReverbEntity", 0x756D58u, 0u },
    { "VuAwardFlowEntity", 0x7398B0u, 0u },
    { "VuBankedTurnWaveEntity", 0x7531C4u, 0u },
    { "VuBarracudaEventUITextEntity", 0x73BFDCu, 0u },
    { "VuBarracudaSlalomPenaltyUITextEntity", 0x73BF9Cu, 0u },
    { "VuBarracudaTrackUIImageEntity", 0x73B4FCu, 0u },
    { "VuBarracudaTrackUITextEntity", 0x73BF1Cu, 0u },
    { "VuBarracudaTuningUITextEntity", 0x73BEDCu, 0u },
    { "VuBarracudaUIAwardImageEntity", 0x73B5DCu, 0u },
    { "VuBarracudaUIButtonEntity", 0x73B7B0u, 0u },
    { "VuBarracudaUICreditTextEntity", 0x73B86Cu, 0u },
    { "VuBarracudaUITrophyImageEntity", 0x73C0BCu, 0u },
    { "VuBarracudaUITrophyTextEntity", 0x73C18Cu, 0u },
    { "VuBarracudaUIUnlockCatTextEntity", 0x73C25Cu, 0u },
    { "VuBarracudaUIUnlockInfoTextEntity", 0x73C32Cu, 0u },
    { "VuBarracudaUnlockUITextEntity", 0x73BE9Cu, 0u },
    { "VuBoatConfigEntity", 0x73C69Cu, 0u },
    { "VuBoatStatEntity", 0x73C808u, 0u },
    { "VuBooleanEntity", 0x75537Cu, 0u },
    { "VuBreakableDynamicGamePropEntity", 0x739F14u, 0u },
    { "VuBreakableGamePropEntity", 0x73A004u, 0u },
    { "VuBreakablePropEntity", 0x73A14Cu, 0u },
    { "VuBuildFilterEntity", 0x754A64u, 0u },
    { "VuBumpWaveEntity", 0x7532C4u, 0u },
    { "VuChampionshipCompletionUITextEntity", 0x73CC34u, 0u },
    { "VuChampionshipEntity", 0x73C998u, 0u },
    { "VuCheatEntity", 0x737104u, 0u },
    { "VuCheckpointBuoyEntity", 0x73A274u, 0u },
    { "VuCircularOceanWaveEntity", 0x753A9Cu, 0u },
    { "VuClearChampDataUIAction", 0x73E9A0u, 0u },
    { "VuClearChampionshipDataUIAction", 0x73E718u, 0u },
    { "VuClearGameResultsUIAction", 0x73E788u, 0u },
    { "VuCompareBooleansEntity", 0x754CA0u, 0u },
    { "VuCompareIntegersEntity", 0x754C6Cu, 0u },
    { "VuCompareStringsEntity", 0x754C38u, 0u },
    { "VuControlEntity", 0x752A28u, 1u },
    { "VuControlKeyframeEntity", 0x7528ACu, 0u },
    { "VuControlMethodEntity", 0x7398E4u, 0u },
    { "VuCoronaEntity", 0x755BD8u, 0u },
    { "VuCoronaOccluderEntity", 0x7559ECu, 0u },
    { "VuCounterEntity", 0x755224u, 0u },
    { "VuCreditsUITextEntity", 0x73BC9Cu, 0u },
    { "VuCurrentAchievementUITextEntity", 0x73BD9Cu, 0u },
    { "VuDelayEntity", 0x754B9Cu, 0u },
    { "VuDevJumpPointEntity", 0x73A350u, 0u },
    { "VuDifficultyImageEntity", 0x73CE3Cu, 0u },
    { "VuDifficultyTextEntity", 0x73CE74u, 0u },
    { "VuDirectionalCoronaEntity", 0x7558D4u, 0u },
    { "VuDirectionalFlowWaveEntity", 0x7536D0u, 0u },
    { "VuDirectionalLightEntity", 0x7564DCu, 0u },
    { "VuDirectionalWaveEntity", 0x753CACu, 0u },
    { "VuDisplayScaleSettingsEntity", 0x73DFF4u, 0u },
    { "VuDockedBoatEntity", 0x73A45Cu, 0u },
    { "VuDoesChampDataExistUIAction", 0x73E9D8u, 0u },
    { "VuDoesChampionshipDataExistUIAction", 0x73E750u, 0u },
    { "VuDynamicGamePropEntity", 0x73A594u, 0u },
    { "VuDynamicLightEntity", 0x7563B0u, 0u },
    { "VuDynamicPropEntity", 0x74DCFCu, 0u },
    { "VuEffectVolumeSettingsEntity", 0x73E06Cu, 0u },
    { "VuEllipticalGfxSettingsEntity", 0x752F4Cu, 0u },
    { "VuEndActionGameUIAction", 0x73EAB8u, 0u },
    { "VuExplosionEntity", 0x755488u, 0u },
    { "VuFloatingMineEntity", 0x73A6CCu, 0u },
    { "VuFoliageEntity", 0x7556CCu, 0u },
    { "VuForceAffectorEntity", 0x73A898u, 0u },
    { "VuFrontEndCameraEntity", 0x73A9E4u, 0u },
    { "VuGameConfigEntity", 0x73D1DCu, 0u },
    { "VuGamerTagUITextEntity", 0x73BCDCu, 0u },
    { "VuGenericEventEntity", 0x75674Cu, 0u },
    { "VuGlobalGfxSettingsEntity", 0x752FD4u, 0u },
    { "VuGridPositionEntity", 0x73AA3Cu, 0u },
    { "VuGroupEntity", 0x756878u, 1u },
    { "VuHUDElementEntity", 0x738C64u, 0u },
    { "VuHUDEntity", 0x738CA4u, 1u },
    { "VuHUDHiddenPackageToastEntity", 0x738BDCu, 0u },
    { "VuHUDMiniMapEntity", 0x738E10u, 0u },
    { "VuHUDOnScreenAxisEntity", 0x739020u, 0u },
    { "VuHUDOnScreenButtonEntity", 0x739058u, 0u },
    { "VuHUDPauseButtonEntity", 0x738AC4u, 0u },
    { "VuHUDRecoverButtonEntity", 0x738B24u, 0u },
    { "VuHUDSpeedometerEntity", 0x739250u, 0u },
    { "VuHumanFilterEntity", 0x739A50u, 0u },
    { "VuIfEntity", 0x754CD4u, 0u },
    { "VuInfiniteOceanWaveEntity", 0x753B34u, 0u },
    { "VuInfoUITextEntity", 0x73BC5Cu, 0u },
    { "VuInputRemappingEntity", 0x73D4F4u, 0u },
    { "VuIntegerEntity", 0x75420Cu, 0u },
    { "VuIsAchievementUnlockedEntity", 0x739918u, 0u },
    { "VuIsCircuitTrack", 0x73994Cu, 0u },
    { "VuIsEventTypeEntity", 0x739AD0u, 0u },
    { "VuIsPauseMenuActive", 0x739980u, 0u },
    { "VuIsSkuEntity", 0x739A84u, 0u },
    { "VuIsTegraEntity", 0x7399E8u, 0u },
    { "VuKeyframeEntity", 0x752B3Cu, 0u },
    { "VuKeyframeMotionEntity", 0x752C44u, 1u },
    { "VuLensWaterEmitterEntity", 0x75557Cu, 0u },
    { "VuLicenseEntity", 0x7399B4u, 0u },
    { "VuLoadingProgressBarEntity", 0x73D724u, 0u },
    { "VuMessageBoxEntity", 0x73D7E0u, 0u },
    { "VuMessageBoxScreenEntity", 0x73D8E4u, 1u },
    { "VuMultiDelayEntity", 0x754B68u, 0u },
    { "VuMusicVolumeSettingsEntity", 0x73E030u, 0u },
    { "VuOnAnyButtonOrKeyEntity", 0x7385E0u, 0u },
    { "VuOnKeyboardEventEntity", 0x7567F4u, 0u },
    { "VuOnProjectInitializedEntity", 0x756828u, 0u },
    { "VuOncePerLapFilterEntity", 0x739A1Cu, 0u },
    { "VuOneShotFilterEntity", 0x754C04u, 0u },
    { "VuOneShotPfxEntity", 0x755C98u, 0u },
    { "VuPackFileVersionsUITextEntity", 0x73BE1Cu, 0u },
    { "VuPersistentBooleanEntity", 0x755144u, 0u },
    { "VuPersistentIntegerEntity", 0x755110u, 0u },
    { "VuPersistentStringEntity", 0x7550DCu, 0u },
    { "VuPfxProjectEntity", 0x752850u, 0u },
    { "VuPlatformFilterEntity", 0x754A98u, 0u },
    { "VuPlayAnimationEntity", 0x757340u, 0u },
    { "VuPointWaveEntity", 0x75344Cu, 0u },
    { "VuPopScreenUIAction", 0x73E8C0u, 0u },
    { "VuPosterEntity", 0x73ABD0u, 0u },
    { "VuPowerUpBoostEntity", 0x73AED4u, 0u },
    { "VuPowerUpHiddenPackageEntity", 0x73AF5Cu, 0u },
    { "VuPowerUpTriggerEntity", 0x73AFE4u, 0u },
    { "VuPropEntity", 0x74DE78u, 0u },
    { "VuPushScreenUIAction", 0x73E8F8u, 0u },
    { "VuRaceCompletionUITextEntity", 0x73CCF4u, 0u },
    { "VuRaceResultsTableEntity", 0x73DA38u, 0u },
    { "VuRampEntity", 0x73B05Cu, 0u },
    { "VuRandomTriggerEntity", 0x754ACCu, 0u },
    { "VuRectangularGfxSettingsEntity", 0x752E74u, 0u },
    { "VuRectangularOceanWaveEntity", 0x753AE8u, 0u },
    { "VuReloadScreenUIAction", 0x73E888u, 0u },
    { "VuRestartActionGameUIAction", 0x73EA48u, 0u },
    { "VuResumeActionGameUIAction", 0x73EA80u, 0u },
    { "VuRotateMotionEntity", 0x752CACu, 0u },
    { "VuSaveSettingsEntity", 0x73E1ACu, 0u },
    { "VuSayVoiceOverBoxEntity", 0x7378E8u, 0u },
    { "VuSayVoiceOverEllipsoidEntity", 0x737920u, 0u },
    { "VuSayVoiceOverEntity", 0x737958u, 0u },
    { "VuSceneEntity", 0x7568C8u, 1u },
    { "VuScreenFadeBoxEntity", 0x73B234u, 0u },
    { "VuScreenFadeEllipsoidEntity", 0x73B26Cu, 0u },
    { "VuSellScreen", 0x73DAA8u, 1u },
    { "VuSentientUpsellPresented", 0x737524u, 0u },
    { "VuSentientUpsellResponded", 0x7374ECu, 0u },
    { "VuSequencerEntity", 0x754BD0u, 0u },
    { "VuSetBoatConfigPlayerEntity", 0x73C654u, 0u },
    { "VuSetBooleanEntity", 0x754EE4u, 0u },
    { "VuSetDefaultSettingsEntity", 0x73E1E0u, 0u },
    { "VuSetFrontEndCameraEntity", 0x737AB8u, 0u },
    { "VuSetGameModeUIAction", 0x73E6E0u, 0u },
    { "VuSetInfoStringUIAction", 0x73E818u, 0u },
    { "VuSetIntegerEntity", 0x754EB0u, 0u },
    { "VuSetMenuPresenceUIAction", 0x73E7E0u, 0u },
    { "VuSetScreenUIAction", 0x73E930u, 0u },
    { "VuSetStringEntity", 0x754E7Cu, 0u },
    { "VuSetTrackBranchProbabilities", 0x73755Cu, 0u },
    { "VuShortUnlockUITextEntity", 0x73BDDCu, 0u },
    { "VuShowWebPageUIAction", 0x73E850u, 0u },
    { "VuShutdownAppUIAction", 0x73E968u, 0u },
    { "VuSignInEntity", 0x7375E4u, 0u },
    { "VuSkyBoxEntity", 0x756054u, 0u },
    { "VuSlalomCompletionUITextEntity", 0x73CCB4u, 0u },
    { "VuSlalomGateEntity", 0x73B3E8u, 0u },
    { "VuStartActionGameUIAction", 0x73EB08u, 0u },
    { "VuStartLightsOutAction", 0x737664u, 0u },
    { "VuStaticPfxEntity", 0x755E80u, 0u },
    { "VuStaticWakeWaveEntity", 0x7535BCu, 0u },
    { "VuSteeringSensitivitySettingsEntity", 0x73E10Cu, 0u },
    { "VuStringEntity", 0x7552D0u, 0u },
    { "VuSubstituteAssetEntity", 0x7570C4u, 0u },
    { "VuSubtractIntegersEntity", 0x7540F4u, 0u },
    { "VuSwapSkyboxEntity", 0x756FE0u, 0u },
    { "VuSwitchEntity", 0x754A30u, 0u },
    { "VuSysNotifyEntity", 0x738614u, 0u },
    { "VuThrottleRemappingEntity", 0x73D45Cu, 0u },
    { "VuTiltSteeringSettingsEntity", 0x73E15Cu, 0u },
    { "VuTimeTrialCompletionUITextEntity", 0x73CC74u, 0u },
    { "VuTransformEntity", 0x752A8Cu, 0u },
    { "VuTriggerArrayEntity", 0x754B34u, 0u },
    { "VuTriggerBoxEntity", 0x752D9Cu, 0u },
    { "VuTriggerForwarderEntity", 0x754B00u, 0u },
    { "VuTriggerPlaneEntity", 0x752D34u, 0u },
    { "VuUIContainerEntity", 0x6F74F0u, 1u },
    { "VuUIDebugTextEntity", 0x6F737Cu, 0u },
    { "VuUIEventEntity", 0x6F7308u, 0u },
    { "VuUIFidelitySettingEntity", 0x73DED4u, 0u },
    { "VuUIFlipIntervalSettingEntity", 0x73DE6Cu, 0u },
    { "VuUIGhostLeaderboardEntity", 0x73F110u, 0u },
    { "VuUIImageEntity", 0x6F7484u, 0u },
    { "VuUILeaderboardEntity", 0x73F2C8u, 0u },
    { "VuUIMiniLeaderboardEntity", 0x73F408u, 0u },
    { "VuUIPadCheatComboEvent", 0x73EFB8u, 0u },
    { "VuUIPageLayoutEntity", 0x6F72C4u, 0u },
    { "VuUIScreenEntity", 0x6F7140u, 1u },
    { "VuUISubtitlesSettingEntity", 0x73DF3Cu, 0u },
    { "VuUITextEntity", 0x6F7434u, 0u },
    { "VuUITouchButtonEntity", 0x6F71ACu, 0u },
    { "VuUIUnitsSettingEntity", 0x73DFA4u, 0u },
    { "VuUserNextUIAction", 0x73EA10u, 0u },
    { "VuVibrationSettingsEntity", 0x73E0BCu, 0u },
    { "VuWakeWaveEntity", 0x7534A8u, 0u },
    { "VuWaterRampEntity", 0x75374Cu, 0u },
    { "VuWaterSurfaceEntity", 0x753F1Cu, 0u },
    { "VuWaterTextureSettingsEntity", 0x7530CCu, 0u },
    { "VuWhirlpoolWaveEntity", 0x753864u, 0u },
};

inline const entity_ctor_t* entity_ctor_for( const std::string& cls )
{
    const entity_ctor_t* first = g_entity_ctors;
    const entity_ctor_t* last = g_entity_ctors + sizeof( g_entity_ctors ) / sizeof( g_entity_ctors[ 0 ] );
    const entity_ctor_t* found = std::lower_bound( first, last, cls,
        []( const entity_ctor_t& row, const std::string& key ) { return std::strcmp( row.m_class, key.c_str( ) ) < 0; } );
    return found != last && cls == found->m_class ? found : nullptr;
}
// The components each captured type's ctor attaches, in its +0x48 order, with
// each component's getType key (census/createprobe.py; codegen/entityfacts.py
// components). Grouped by class, classes sorted.
inline const entity_component_row_t g_entity_component_rows[] = {
    { "VuAIUIDriverStartEntity", "VuTransformComponent", 0x74D324u },
    { "VuAIUIDriverStartEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAIUIDriverStartEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAchievementNameUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuAchievementNameUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAchievementNameUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuAchievementNameUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuAchievementValueUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuAchievementValueUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAchievementValueUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuAchievementValueUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuAddBoostEntity", "VuTransformComponent", 0x74D324u },
    { "VuAddBoostEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAddIntegerConstantEntity", "VuTransformComponent", 0x74D324u },
    { "VuAddIntegerConstantEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAddIntegersEntity", "VuTransformComponent", 0x74D324u },
    { "VuAddIntegersEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAiNuisanceDriverStartEntity", "VuTransformComponent", 0x74D324u },
    { "VuAiNuisanceDriverStartEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAiNuisanceDriverStartEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAiWaypointEntity", "VuTransformComponent", 0x74D324u },
    { "VuAiWaypointEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAiWaypointEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAmbientLightEntity", "VuTransformComponent", 0x74D324u },
    { "VuAmbientLightEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAnimatedPropEntity", "VuTransformComponent", 0x74D324u },
    { "VuAnimatedPropEntity", "Vu3dDrawAnimatedModelComponent", 0x758240u },
    { "VuAnimatedPropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAnimatedPropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAnimatedPropEntity", "VuAnimatedAttachComponent", 0x75829Cu },
    { "VuAnimatedPropEntity", "VuMotionComponent", 0x74E984u },
    { "VuAnimatedPropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuAttachEntity", "VuTransformComponent", 0x74D324u },
    { "VuAttachEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioAmbientReverbEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioAmbientReverbEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioBankEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioDuckingEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioDuckingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioEmitterEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioEmitterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioEmitterEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAudioEmitterEntity", "VuMotionComponent", 0x74E984u },
    { "VuAudioEventEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioEventEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioProjectEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioReverbEntity", "VuTransformComponent", 0x74D324u },
    { "VuAudioReverbEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuAudioReverbEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuAwardFlowEntity", "VuTransformComponent", 0x74D324u },
    { "VuAwardFlowEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBankedTurnWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuBankedTurnWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuBarracudaEventUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaEventUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaEventUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaEventUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaSlalomPenaltyUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaSlalomPenaltyUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaSlalomPenaltyUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaSlalomPenaltyUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaTrackUIImageEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaTrackUIImageEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaTrackUIImageEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaTrackUIImageEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaTrackUIImageEntity", "VuControlComponent", 0x757EA0u },
    { "VuBarracudaTrackUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaTrackUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaTrackUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaTrackUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaTuningUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaTuningUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaTuningUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaTuningUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUIAwardImageEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUIAwardImageEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUIAwardImageEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUIAwardImageEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUIAwardImageEntity", "VuControlComponent", 0x757EA0u },
    { "VuBarracudaUIButtonEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUIButtonEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUIButtonEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUIButtonEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUICreditTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUICreditTextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUICreditTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUICreditTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUITrophyImageEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUITrophyImageEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUITrophyImageEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUITrophyImageEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUITrophyImageEntity", "VuControlComponent", 0x757EA0u },
    { "VuBarracudaUITrophyTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUITrophyTextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUITrophyTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUITrophyTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUIUnlockCatTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUIUnlockCatTextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUIUnlockCatTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUIUnlockCatTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUIUnlockInfoTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUIUnlockInfoTextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUIUnlockInfoTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUIUnlockInfoTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBarracudaUnlockUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuBarracudaUnlockUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBarracudaUnlockUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBarracudaUnlockUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBoatConfigEntity", "VuTransformComponent", 0x74D324u },
    { "VuBoatConfigEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBoatConfigEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBoatConfigEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBoatStatEntity", "VuTransformComponent", 0x74D324u },
    { "VuBoatStatEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBoatStatEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuBoatStatEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuBooleanEntity", "VuTransformComponent", 0x74D324u },
    { "VuBooleanEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBreakableDynamicGamePropEntity", "VuTransformComponent", 0x74D324u },
    { "VuBreakableDynamicGamePropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuBreakableDynamicGamePropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBreakableDynamicGamePropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuBreakableDynamicGamePropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuBreakableDynamicGamePropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuBreakableDynamicGamePropEntity", "Vu3dDrawBreakableModelComponent", 0x74D9D0u },
    { "VuBreakableGamePropEntity", "VuTransformComponent", 0x74D324u },
    { "VuBreakableGamePropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuBreakableGamePropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBreakableGamePropEntity", "VuMotionComponent", 0x74E984u },
    { "VuBreakableGamePropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuBreakableGamePropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuBreakableGamePropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuBreakableGamePropEntity", "Vu3dDrawBreakableModelComponent", 0x74D9D0u },
    { "VuBreakablePropEntity", "VuTransformComponent", 0x74D324u },
    { "VuBreakablePropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuBreakablePropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBreakablePropEntity", "VuMotionComponent", 0x74E984u },
    { "VuBreakablePropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuBreakablePropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuBreakablePropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuBreakablePropEntity", "Vu3dDrawBreakableModelComponent", 0x74D9D0u },
    { "VuBuildFilterEntity", "VuTransformComponent", 0x74D324u },
    { "VuBuildFilterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuBumpWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuBumpWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuChampionshipCompletionUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuChampionshipCompletionUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuChampionshipCompletionUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuChampionshipCompletionUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuChampionshipEntity", "VuTransformComponent", 0x74D324u },
    { "VuChampionshipEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuChampionshipEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuChampionshipEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuCheatEntity", "VuTransformComponent", 0x74D324u },
    { "VuCheatEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCheckpointBuoyEntity", "VuTransformComponent", 0x74D324u },
    { "VuCheckpointBuoyEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuCheckpointBuoyEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCheckpointBuoyEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuCheckpointBuoyEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuCheckpointBuoyEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuCircularOceanWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuCircularOceanWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCircularOceanWaveEntity", "VuControlComponent", 0x757EA0u },
    { "VuCircularOceanWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuClearChampDataUIAction", "VuTransformComponent", 0x74D324u },
    { "VuClearChampDataUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuClearChampionshipDataUIAction", "VuTransformComponent", 0x74D324u },
    { "VuClearChampionshipDataUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuClearGameResultsUIAction", "VuTransformComponent", 0x74D324u },
    { "VuClearGameResultsUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuCompareBooleansEntity", "VuTransformComponent", 0x74D324u },
    { "VuCompareBooleansEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCompareIntegersEntity", "VuTransformComponent", 0x74D324u },
    { "VuCompareIntegersEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCompareStringsEntity", "VuTransformComponent", 0x74D324u },
    { "VuCompareStringsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuControlEntity", "VuTransformComponent", 0x74D324u },
    { "VuControlEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuControlKeyframeEntity", "VuTransformComponent", 0x74D324u },
    { "VuControlMethodEntity", "VuTransformComponent", 0x74D324u },
    { "VuControlMethodEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCoronaEntity", "VuTransformComponent", 0x74D324u },
    { "VuCoronaEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuCoronaEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuCoronaEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCoronaEntity", "VuMotionComponent", 0x74E984u },
    { "VuCoronaOccluderEntity", "VuTransformComponent", 0x74D324u },
    { "VuCoronaOccluderEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuCoronaOccluderEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuCounterEntity", "VuTransformComponent", 0x74D324u },
    { "VuCounterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCreditsUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuCreditsUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCreditsUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuCreditsUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuCurrentAchievementUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuCurrentAchievementUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuCurrentAchievementUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuCurrentAchievementUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuDelayEntity", "VuTransformComponent", 0x74D324u },
    { "VuDelayEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDevJumpPointEntity", "VuTransformComponent", 0x74D324u },
    { "VuDevJumpPointEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDifficultyImageEntity", "VuTransformComponent", 0x74D324u },
    { "VuDifficultyImageEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuDifficultyImageEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuDifficultyTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuDifficultyTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuDifficultyTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuDirectionalCoronaEntity", "VuTransformComponent", 0x74D324u },
    { "VuDirectionalCoronaEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDirectionalCoronaEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuDirectionalCoronaEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDirectionalCoronaEntity", "VuMotionComponent", 0x74E984u },
    { "VuDirectionalFlowWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuDirectionalFlowWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDirectionalLightEntity", "VuTransformComponent", 0x74D324u },
    { "VuDirectionalLightEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDirectionalLightEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDirectionalLightEntity", "VuMotionComponent", 0x74E984u },
    { "VuDirectionalWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuDirectionalWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDirectionalWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDirectionalWaveEntity", "VuControlComponent", 0x757EA0u },
    { "VuDisplayScaleSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuDisplayScaleSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDisplayScaleSettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuDisplayScaleSettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuDockedBoatEntity", "VuTransformComponent", 0x74D324u },
    { "VuDoesChampDataExistUIAction", "VuTransformComponent", 0x74D324u },
    { "VuDoesChampDataExistUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuDoesChampionshipDataExistUIAction", "VuTransformComponent", 0x74D324u },
    { "VuDoesChampionshipDataExistUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuDynamicGamePropEntity", "VuTransformComponent", 0x74D324u },
    { "VuDynamicGamePropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDynamicGamePropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDynamicGamePropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuDynamicGamePropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuDynamicGamePropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuDynamicLightEntity", "VuTransformComponent", 0x74D324u },
    { "VuDynamicLightEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDynamicLightEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDynamicLightEntity", "VuMotionComponent", 0x74E984u },
    { "VuDynamicPropEntity", "VuTransformComponent", 0x74D324u },
    { "VuDynamicPropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuDynamicPropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuDynamicPropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuDynamicPropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuDynamicPropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuEffectVolumeSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuEffectVolumeSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuEffectVolumeSettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuEffectVolumeSettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuEllipticalGfxSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuEllipticalGfxSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuEllipticalGfxSettingsEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuEndActionGameUIAction", "VuTransformComponent", 0x74D324u },
    { "VuEndActionGameUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuExplosionEntity", "VuTransformComponent", 0x74D324u },
    { "VuExplosionEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuExplosionEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuFloatingMineEntity", "VuTransformComponent", 0x74D324u },
    { "VuFloatingMineEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuFloatingMineEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuFloatingMineEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuFloatingMineEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuFloatingMineEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuFoliageEntity", "VuTransformComponent", 0x74D324u },
    { "VuFoliageEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuFoliageEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuForceAffectorEntity", "VuTransformComponent", 0x74D324u },
    { "VuForceAffectorEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuForceAffectorEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuFrontEndCameraEntity", "VuTransformComponent", 0x74D324u },
    { "VuFrontEndCameraEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuGameConfigEntity", "VuTransformComponent", 0x74D324u },
    { "VuGameConfigEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuGameConfigEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuGameConfigEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuGameConfigEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuGamerTagUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuGamerTagUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuGamerTagUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuGamerTagUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuGenericEventEntity", "VuTransformComponent", 0x74D324u },
    { "VuGenericEventEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuGlobalGfxSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuGlobalGfxSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuGridPositionEntity", "VuTransformComponent", 0x74D324u },
    { "VuGridPositionEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuGroupEntity", "VuTransformComponent", 0x74D324u },
    { "VuGroupEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuHUDElementEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDElementEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuHUDElementEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDElementEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDHiddenPackageToastEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDHiddenPackageToastEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuHUDHiddenPackageToastEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDHiddenPackageToastEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDMiniMapEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDMiniMapEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDMiniMapEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDOnScreenAxisEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDOnScreenAxisEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDOnScreenAxisEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDOnScreenButtonEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDOnScreenButtonEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDOnScreenButtonEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDPauseButtonEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDPauseButtonEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDPauseButtonEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDRecoverButtonEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDRecoverButtonEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDRecoverButtonEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHUDSpeedometerEntity", "VuTransformComponent", 0x74D324u },
    { "VuHUDSpeedometerEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuHUDSpeedometerEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuHumanFilterEntity", "VuTransformComponent", 0x74D324u },
    { "VuHumanFilterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuIfEntity", "VuTransformComponent", 0x74D324u },
    { "VuIfEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuInfiniteOceanWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuInfiniteOceanWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuInfiniteOceanWaveEntity", "VuControlComponent", 0x757EA0u },
    { "VuInfoUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuInfoUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuInfoUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuInfoUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuInputRemappingEntity", "VuTransformComponent", 0x74D324u },
    { "VuInputRemappingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuInputRemappingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuInputRemappingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuIntegerEntity", "VuTransformComponent", 0x74D324u },
    { "VuIntegerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuIsAchievementUnlockedEntity", "VuTransformComponent", 0x74D324u },
    { "VuIsAchievementUnlockedEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuIsCircuitTrack", "VuTransformComponent", 0x74D324u },
    { "VuIsCircuitTrack", "VuScriptComponent", 0x74C3F0u },
    { "VuIsEventTypeEntity", "VuTransformComponent", 0x74D324u },
    { "VuIsEventTypeEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuIsPauseMenuActive", "VuTransformComponent", 0x74D324u },
    { "VuIsPauseMenuActive", "VuScriptComponent", 0x74C3F0u },
    { "VuIsSkuEntity", "VuTransformComponent", 0x74D324u },
    { "VuIsSkuEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuIsTegraEntity", "VuTransformComponent", 0x74D324u },
    { "VuIsTegraEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuKeyframeEntity", "VuTransformComponent", 0x74D324u },
    { "VuKeyframeEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuKeyframeMotionEntity", "VuTransformComponent", 0x74D324u },
    { "VuKeyframeMotionEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuKeyframeMotionEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuLensWaterEmitterEntity", "VuTransformComponent", 0x74D324u },
    { "VuLensWaterEmitterEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuLicenseEntity", "VuTransformComponent", 0x74D324u },
    { "VuLicenseEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuLoadingProgressBarEntity", "VuTransformComponent", 0x74D324u },
    { "VuLoadingProgressBarEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuLoadingProgressBarEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuLoadingProgressBarEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuMessageBoxEntity", "VuTransformComponent", 0x74D324u },
    { "VuMessageBoxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuMessageBoxScreenEntity", "VuTransformComponent", 0x74D324u },
    { "VuMessageBoxScreenEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuMessageBoxScreenEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuMultiDelayEntity", "VuTransformComponent", 0x74D324u },
    { "VuMultiDelayEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuMusicVolumeSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuMusicVolumeSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuMusicVolumeSettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuMusicVolumeSettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuOnAnyButtonOrKeyEntity", "VuTransformComponent", 0x74D324u },
    { "VuOnAnyButtonOrKeyEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOnKeyboardEventEntity", "VuTransformComponent", 0x74D324u },
    { "VuOnKeyboardEventEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOnProjectInitializedEntity", "VuTransformComponent", 0x74D324u },
    { "VuOnProjectInitializedEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOncePerLapFilterEntity", "VuTransformComponent", 0x74D324u },
    { "VuOncePerLapFilterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOneShotFilterEntity", "VuTransformComponent", 0x74D324u },
    { "VuOneShotFilterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOneShotPfxEntity", "VuTransformComponent", 0x74D324u },
    { "VuOneShotPfxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuOneShotPfxEntity", "VuMotionComponent", 0x74E984u },
    { "VuPackFileVersionsUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuPackFileVersionsUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPackFileVersionsUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuPackFileVersionsUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuPersistentBooleanEntity", "VuTransformComponent", 0x74D324u },
    { "VuPersistentBooleanEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPersistentIntegerEntity", "VuTransformComponent", 0x74D324u },
    { "VuPersistentIntegerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPersistentStringEntity", "VuTransformComponent", 0x74D324u },
    { "VuPersistentStringEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPfxProjectEntity", "VuTransformComponent", 0x74D324u },
    { "VuPlatformFilterEntity", "VuTransformComponent", 0x74D324u },
    { "VuPlatformFilterEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPlayAnimationEntity", "VuTransformComponent", 0x74D324u },
    { "VuPlayAnimationEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPointWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuPointWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPointWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPointWaveEntity", "VuMotionComponent", 0x74E984u },
    { "VuPopScreenUIAction", "VuTransformComponent", 0x74D324u },
    { "VuPopScreenUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuPosterEntity", "VuTransformComponent", 0x74D324u },
    { "VuPosterEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPosterEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuPowerUpBoostEntity", "VuTransformComponent", 0x74D324u },
    { "VuPowerUpBoostEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPowerUpBoostEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPowerUpBoostEntity", "VuMotionComponent", 0x74E984u },
    { "VuPowerUpHiddenPackageEntity", "VuTransformComponent", 0x74D324u },
    { "VuPowerUpHiddenPackageEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPowerUpHiddenPackageEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPowerUpHiddenPackageEntity", "VuMotionComponent", 0x74E984u },
    { "VuPowerUpTriggerEntity", "VuTransformComponent", 0x74D324u },
    { "VuPowerUpTriggerEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPowerUpTriggerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPowerUpTriggerEntity", "VuMotionComponent", 0x74E984u },
    { "VuPropEntity", "VuTransformComponent", 0x74D324u },
    { "VuPropEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuPropEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuPropEntity", "VuMotionComponent", 0x74E984u },
    { "VuPropEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuPropEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuPropEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuPushScreenUIAction", "VuTransformComponent", 0x74D324u },
    { "VuPushScreenUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuRaceCompletionUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuRaceCompletionUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRaceCompletionUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuRaceCompletionUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuRaceResultsTableEntity", "VuTransformComponent", 0x74D324u },
    { "VuRaceResultsTableEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuRaceResultsTableEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuRaceResultsTableEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRampEntity", "VuTransformComponent", 0x74D324u },
    { "VuRampEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuRampEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRampEntity", "VuMotionComponent", 0x74E984u },
    { "VuRampEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuRampEntity", "Vu3dDrawStaticModelComponent", 0x7519ACu },
    { "VuRampEntity", "VuRigidBodyComponent", 0x74DAE4u },
    { "VuRandomTriggerEntity", "VuTransformComponent", 0x74D324u },
    { "VuRandomTriggerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRectangularGfxSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuRectangularGfxSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRectangularGfxSettingsEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuRectangularOceanWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuRectangularOceanWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuRectangularOceanWaveEntity", "VuControlComponent", 0x757EA0u },
    { "VuRectangularOceanWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuReloadScreenUIAction", "VuTransformComponent", 0x74D324u },
    { "VuReloadScreenUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuRestartActionGameUIAction", "VuTransformComponent", 0x74D324u },
    { "VuRestartActionGameUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuResumeActionGameUIAction", "VuTransformComponent", 0x74D324u },
    { "VuResumeActionGameUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuRotateMotionEntity", "VuTransformComponent", 0x74D324u },
    { "VuRotateMotionEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSaveSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuSaveSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSayVoiceOverBoxEntity", "VuTransformComponent", 0x74D324u },
    { "VuSayVoiceOverBoxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSayVoiceOverBoxEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSayVoiceOverEllipsoidEntity", "VuTransformComponent", 0x74D324u },
    { "VuSayVoiceOverEllipsoidEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSayVoiceOverEllipsoidEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSayVoiceOverEntity", "VuTransformComponent", 0x74D324u },
    { "VuSayVoiceOverEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSceneEntity", "VuTransformComponent", 0x74D324u },
    { "VuScreenFadeBoxEntity", "VuTransformComponent", 0x74D324u },
    { "VuScreenFadeBoxEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuScreenFadeEllipsoidEntity", "VuTransformComponent", 0x74D324u },
    { "VuScreenFadeEllipsoidEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSellScreen", "VuTransformComponent", 0x74D324u },
    { "VuSentientUpsellPresented", "VuTransformComponent", 0x74D324u },
    { "VuSentientUpsellPresented", "VuScriptComponent", 0x74C3F0u },
    { "VuSentientUpsellResponded", "VuTransformComponent", 0x74D324u },
    { "VuSentientUpsellResponded", "VuScriptComponent", 0x74C3F0u },
    { "VuSequencerEntity", "VuTransformComponent", 0x74D324u },
    { "VuSequencerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetBoatConfigPlayerEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetBoatConfigPlayerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetBooleanEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetBooleanEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetDefaultSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetDefaultSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetFrontEndCameraEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetFrontEndCameraEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetGameModeUIAction", "VuTransformComponent", 0x74D324u },
    { "VuSetGameModeUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuSetInfoStringUIAction", "VuTransformComponent", 0x74D324u },
    { "VuSetInfoStringUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuSetIntegerEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetIntegerEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetMenuPresenceUIAction", "VuTransformComponent", 0x74D324u },
    { "VuSetMenuPresenceUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuSetScreenUIAction", "VuTransformComponent", 0x74D324u },
    { "VuSetScreenUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuSetStringEntity", "VuTransformComponent", 0x74D324u },
    { "VuSetStringEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSetTrackBranchProbabilities", "VuTransformComponent", 0x74D324u },
    { "VuSetTrackBranchProbabilities", "VuScriptComponent", 0x74C3F0u },
    { "VuShortUnlockUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuShortUnlockUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuShortUnlockUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuShortUnlockUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuShowWebPageUIAction", "VuTransformComponent", 0x74D324u },
    { "VuShowWebPageUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuShutdownAppUIAction", "VuTransformComponent", 0x74D324u },
    { "VuShutdownAppUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuSignInEntity", "VuTransformComponent", 0x74D324u },
    { "VuSignInEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSkyBoxEntity", "VuTransformComponent", 0x74D324u },
    { "VuSkyBoxEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuSkyBoxEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSlalomCompletionUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuSlalomCompletionUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSlalomCompletionUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuSlalomCompletionUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuSlalomGateEntity", "VuTransformComponent", 0x74D324u },
    { "VuSlalomGateEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSlalomGateEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSlalomGateEntity", "VuMotionComponent", 0x74E984u },
    { "VuStartActionGameUIAction", "VuTransformComponent", 0x74D324u },
    { "VuStartActionGameUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuStartLightsOutAction", "VuTransformComponent", 0x74D324u },
    { "VuStartLightsOutAction", "VuScriptComponent", 0x74C3F0u },
    { "VuStaticPfxEntity", "VuTransformComponent", 0x74D324u },
    { "VuStaticPfxEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuStaticPfxEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuStaticPfxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuStaticPfxEntity", "VuMotionComponent", 0x74E984u },
    { "VuStaticWakeWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuStaticWakeWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuSteeringSensitivitySettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuSteeringSensitivitySettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSteeringSensitivitySettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuSteeringSensitivitySettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuStringEntity", "VuTransformComponent", 0x74D324u },
    { "VuStringEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSubstituteAssetEntity", "VuTransformComponent", 0x74D324u },
    { "VuSubstituteAssetEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSubtractIntegersEntity", "VuTransformComponent", 0x74D324u },
    { "VuSubtractIntegersEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSwapSkyboxEntity", "VuTransformComponent", 0x74D324u },
    { "VuSwapSkyboxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSwitchEntity", "VuTransformComponent", 0x74D324u },
    { "VuSwitchEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuSysNotifyEntity", "VuTransformComponent", 0x74D324u },
    { "VuSysNotifyEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuThrottleRemappingEntity", "VuTransformComponent", 0x74D324u },
    { "VuThrottleRemappingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuThrottleRemappingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuThrottleRemappingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuTiltSteeringSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuTiltSteeringSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTiltSteeringSettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuTiltSteeringSettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuTimeTrialCompletionUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuTimeTrialCompletionUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTimeTrialCompletionUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuTimeTrialCompletionUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuTransformEntity", "VuTransformComponent", 0x74D324u },
    { "VuTransformEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTransformEntity", "VuMotionComponent", 0x74E984u },
    { "VuTransformEntity", "VuOffsetAttachComponent", 0x75182Cu },
    { "VuTriggerArrayEntity", "VuTransformComponent", 0x74D324u },
    { "VuTriggerArrayEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTriggerBoxEntity", "VuTransformComponent", 0x74D324u },
    { "VuTriggerBoxEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuTriggerBoxEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTriggerForwarderEntity", "VuTransformComponent", 0x74D324u },
    { "VuTriggerForwarderEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuTriggerPlaneEntity", "VuTransformComponent", 0x74D324u },
    { "VuTriggerPlaneEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuTriggerPlaneEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIContainerEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIContainerEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIDebugTextEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIDebugTextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIDebugTextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIDebugTextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIEventEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIEventEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIFidelitySettingEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIFidelitySettingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIFidelitySettingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIFidelitySettingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIFlipIntervalSettingEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIFlipIntervalSettingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIFlipIntervalSettingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIFlipIntervalSettingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIGhostLeaderboardEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIGhostLeaderboardEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIGhostLeaderboardEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIGhostLeaderboardEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIImageEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIImageEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIImageEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIImageEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIImageEntity", "VuControlComponent", 0x757EA0u },
    { "VuUILeaderboardEntity", "VuTransformComponent", 0x74D324u },
    { "VuUILeaderboardEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUILeaderboardEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUILeaderboardEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIMiniLeaderboardEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIMiniLeaderboardEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIMiniLeaderboardEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIMiniLeaderboardEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIPadCheatComboEvent", "VuTransformComponent", 0x74D324u },
    { "VuUIPadCheatComboEvent", "VuScriptComponent", 0x74C3F0u },
    { "VuUIPageLayoutEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIPageLayoutEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIPageLayoutEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIPageLayoutEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUIScreenEntity", "VuTransformComponent", 0x74D324u },
    { "VuUISubtitlesSettingEntity", "VuTransformComponent", 0x74D324u },
    { "VuUISubtitlesSettingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUISubtitlesSettingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUISubtitlesSettingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUITextEntity", "VuTransformComponent", 0x74D324u },
    { "VuUITextEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUITextEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUITextEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUITouchButtonEntity", "VuTransformComponent", 0x74D324u },
    { "VuUITouchButtonEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUITouchButtonEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIUnitsSettingEntity", "VuTransformComponent", 0x74D324u },
    { "VuUIUnitsSettingEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuUIUnitsSettingEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuUIUnitsSettingEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuUserNextUIAction", "VuTransformComponent", 0x74D324u },
    { "VuUserNextUIAction", "VuScriptComponent", 0x74C3F0u },
    { "VuVibrationSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuVibrationSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuVibrationSettingsEntity", "Vu2dLayoutComponent", 0x738A78u },
    { "VuVibrationSettingsEntity", "VuUIDrawComponent", 0x738A44u },
    { "VuWakeWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuWakeWaveEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuWakeWaveEntity", "VuMotionComponent", 0x74E984u },
    { "VuWaterRampEntity", "VuTransformComponent", 0x74D324u },
    { "VuWaterRampEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuWaterRampEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuWaterRampEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuWaterRampEntity", "VuMotionComponent", 0x74E984u },
    { "VuWaterSurfaceEntity", "VuTransformComponent", 0x74D324u },
    { "VuWaterSurfaceEntity", "Vu3dDrawComponent", 0x74D244u },
    { "VuWaterSurfaceEntity", "Vu3dLayoutComponent", 0x74C550u },
    { "VuWaterSurfaceEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuWaterSurfaceEntity", "VuMotionComponent", 0x74E984u },
    { "VuWaterTextureSettingsEntity", "VuTransformComponent", 0x74D324u },
    { "VuWaterTextureSettingsEntity", "VuScriptComponent", 0x74C3F0u },
    { "VuWhirlpoolWaveEntity", "VuTransformComponent", 0x74D324u },
    { "VuWhirlpoolWaveEntity", "Vu3dLayoutComponent", 0x74C550u },
};

// The captured script-graph rows (game/plugs.hxx) as engine entity/script.hxx takes them.
inline const script_facts_t g_script_facts = { g_entity_type_chains, sizeof( g_entity_type_chains ) / sizeof( g_entity_type_chains[ 0 ] ),
    g_script_plug_rows, sizeof( g_script_plug_rows ) / sizeof( g_script_plug_rows[ 0 ] ), g_script_ref_rows,
    sizeof( g_script_ref_rows ) / sizeof( g_script_ref_rows[ 0 ] ) };

inline std::vector< entity_component_t > entity_components_for( const std::string& cls )
{
    std::vector< entity_component_t > out;
    for ( const entity_component_row_t& row : g_entity_component_rows )
        if ( cls == row.m_class )
            out.push_back( { row.m_key, row.m_vtable, nullptr } );
    return out;
}

constexpr std::uint32_t k_transform_component_vtable = 0x74D324u;

// A VuTransformComponent and its three property nodes' defaults (+0x14 of each
// node): the ctor's values until a template mark makes the loaded ones the
// defaults (every property class's vt+0x1C copies the target into it).
struct transform_state_t
{
    transform_component_t c;
    float position[ 3 ];
    float rotation[ 3 ];
    float scale[ 3 ];
};

inline transform_state_t* entity_transform_state( entity_t& e )
{
    for ( entity_component_t& c : e.m_components )
        if ( c.m_vtable == k_transform_component_vtable && c.m_state )
            return static_cast< transform_state_t* >( c.m_state.get( ) );
    return nullptr;
}

inline transform_component_t* entity_transform( entity_t& e )
{
    transform_state_t* s = entity_transform_state( e );
    return s ? &s->c : nullptr;
}

inline void entity_mark_world_unknown( entity_t& e )
{
    e.m_world_unknown = true;
    for ( auto& child : e.m_children )
        entity_mark_world_unknown( *child );
}

// 0x534C00, the recalc's last step: every child of the owner gets world =
// its local x this world (0x4078C0) and its world euler (CRT atan2 / asin -
// not recreated, so unknown under a parent), the child's watcher (+0xC0, only
// when notified) is a hook, and the walk recurses into it. A child whose
// transform is not modelled (an UNKNOWN entity) cannot be updated: its world,
// and its subtree's, is unknown.
inline void transform_walk_children( entity_t& owner, const float* world )
{
    for ( auto& child : owner.m_children )
    {
        transform_component_t* t = entity_transform( *child );
        if ( !t )
        {
            entity_mark_world_unknown( *child );
            continue;
        }
        transform_world( *t, world );
        transform_walk_children( *child, t->world );
    }
}

// The world's component load for the components recreated so far - only
// VuTransformComponent (constructed by its ctor 0x534180 when the owner is,
// so its state exists from creation): its Position / Rotation / Scale loads
// (each reset to its node's default first), then its post-load (vt+0xC
// 0x534340, vt+0x10 0x5343F0 - the same body): the recalc against the owner's
// parent's transform world as it stands now, then 0x534C00's child walk.
// Every other component is a hook.
constexpr std::uint32_t k_static_model_component_vtable = 0x7519ACu;
constexpr std::uint32_t k_animated_model_component_vtable = 0x758240u;

// A model-draw component's targets and each node's default.
struct model_draw_state_t
{
    model_draw_t value;
    model_draw_t defaults;
};

// VuRigidBodyComponent (engine entity/rigid.hxx): its targets and each node's default.
constexpr std::uint32_t k_rigid_body_component_vtable = 0x74DAE4u;

struct rigid_component_state_t
{
    rigid_component_t value;
    rigid_component_t defaults;
};

inline void sky_box_component_load( entity_t& owner, const value_t& properties );

inline void entity_load_component( entity_t& owner, entity_component_t& component, const value_t& properties, bool )
{
    if ( component.m_vtable == 0x74D244u && owner.m_type == "VuSkyBoxEntity" )
    {
        sky_box_component_load( owner, properties );
        return;
    }
    if ( component.m_vtable == k_rigid_body_component_vtable )
    {
        rigid_component_state_t* s = static_cast< rigid_component_state_t* >( component.m_state.get( ) );
        rigid_component_load( s->value, properties, s->defaults );
        return;   // postLoad 0x545010: no body yet - nothing (entity/rigid.hxx)
    }
    if ( component.m_vtable == k_static_model_component_vtable || component.m_vtable == k_animated_model_component_vtable )
    {
        model_draw_state_t* s = static_cast< model_draw_state_t* >( component.m_state.get( ) );
        model_draw_load( s->value, properties, s->defaults );
        // The post-load (vt+0xC / vt+0x10) is a hook.
        return;
    }
    if ( component.m_vtable != k_transform_component_vtable )
        return;
    transform_state_t* s = static_cast< transform_state_t* >( component.m_state.get( ) );
    transform_property( properties, "Position", s->c.local + 12, s->position, false );
    transform_property( properties, "Rotation", s->c.rotation, s->rotation, true );
    transform_property( properties, "Scale", s->c.scale, s->scale, false );
    transform_component_t* parent = owner.m_parent ? entity_transform( *owner.m_parent ) : nullptr;
    transform_recalc( s->c, parent ? parent->world : nullptr );
    transform_walk_children( owner, s->c.world );
}

// The template mark's component step 0x501FE0: each property node's vt+0x1C -
// its default becomes the target's current value; the component's vt+0x20 is
// a hook.
inline void entity_mark_component( entity_t&, entity_component_t& component )
{
    if ( component.m_vtable == k_rigid_body_component_vtable )
    {
        rigid_component_state_t* s = static_cast< rigid_component_state_t* >( component.m_state.get( ) );
        s->defaults = s->value;
        return;
    }
    if ( component.m_vtable == k_static_model_component_vtable || component.m_vtable == k_animated_model_component_vtable )
    {
        model_draw_state_t* s = static_cast< model_draw_state_t* >( component.m_state.get( ) );
        s->defaults = s->value;
        return;
    }
    if ( component.m_vtable != k_transform_component_vtable )
        return;
    transform_state_t* s = static_cast< transform_state_t* >( component.m_state.get( ) );
    std::memcpy( s->position, s->c.local + 12, sizeof( s->position ) );
    std::memcpy( s->rotation, s->c.rotation, sizeof( s->rotation ) );
    std::memcpy( s->scale, s->c.scale, sizeof( s->scale ) );
}

// The entity properties recreated so far - VuCoronaEntity's: the targets and
// each node's default (the ctor's values; a template mark makes the loaded
// values the defaults).
struct corona_state_t
{
    corona_properties_t value;
    corona_properties_t defaults;
};

// VuFoliageEntity's properties, by its ctor 0x5E69A0 (world/foliage.hxx foliage_entity_t).
struct foliage_state_t
{
    std::string texture;                                        // "Texture Asset"
    std::uint8_t fog = 0;                                       // "Fog Enabled"
    std::uint32_t color = 0x00FFFFFFu;                          // "Color"
    float uv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };                 // "U0" "V0" "U1" "V1"
    float draw_distance = 3.4028235e38f;                        // "Draw Distance"
};

// VuAmbientLightEntity / VuDirectionalLightEntity (engine entity/rig.hxx).
struct ambient_light_state_t
{
    ambient_light_t value;
    ambient_light_t defaults;
};

struct directional_light_state_t
{
    directional_light_t value;
    directional_light_t defaults;
};

// VuGlobalGfxSettingsEntity (engine entity/gfx.hxx).
struct gfx_settings_state_t
{
    gfx_settings_t value;
    gfx_settings_t defaults;
};

// VuRectangularGfxSettingsEntity (ctor 0x5D1FB0) / VuEllipticalGfxSettingsEntity (ctor 0x5D2580): the base
// VuGfxSettingsEntity set (engine entity/gfx.hxx - the same base ctor 0x5A6520) then their VuPercentageProperty
// Inner Radius X % / Y % (+0xD0 / +0xD4, 0.5 each) - the elliptical one only Inner Radius % (+0xD0).
struct gfx_zone_state_t
{
    gfx_settings_t value;
    gfx_settings_t defaults;
    float inner[ 2 ] = { 0.5f, 0.5f };
    float inner_defaults[ 2 ] = { 0.5f, 0.5f };
};

// VuTriggerPlaneEntity / VuTriggerBoxEntity (engine entity/trigger.hxx).
struct trigger_state_t
{
    trigger_properties_t value;
    trigger_properties_t defaults;
};

// VuStaticPfxEntity (engine entity/pfx.hxx).
struct static_pfx_state_t
{
    static_pfx_t value;
    static_pfx_t defaults;
};

inline bool entity_is_trigger( const std::string& cls )
{
    return cls == "VuTriggerPlaneEntity" || cls == "VuTriggerBoxEntity";
}

// The logic / action / motion / settings entities (engine entity/actions.hxx):
// one table row per class - its construct, its loads and the template mark.
template < class T >
struct props_state_t
{
    T value;
    T defaults;
};

struct action_kind_t
{
    const char* m_class;
    std::shared_ptr< void > ( *m_make )( );
    void ( *m_load )( void* state, const value_t& properties );
    void ( *m_mark )( void* state );
};

template < class T, void ( *Construct )( T& ), void ( *Load )( T&, const value_t&, const T& ) >
struct action_kind_of
{
    static std::shared_ptr< void > make( )
    {
        auto s = std::make_shared< props_state_t< T > >( );
        Construct( s->value );
        Construct( s->defaults );
        return s;
    }
    static void load( void* state, const value_t& properties )
    {
        props_state_t< T >* s = static_cast< props_state_t< T >* >( state );
        Load( s->value, properties, s->defaults );
    }
    static void mark( void* state )
    {
        props_state_t< T >* s = static_cast< props_state_t< T >* >( state );
        s->defaults = s->value;
    }
};

#define HYDRO_ACTION_KIND( cls, T, C, L ) \
    { cls, &action_kind_of< T, C, L >::make, &action_kind_of< T, C, L >::load, &action_kind_of< T, C, L >::mark }

inline const action_kind_t* entity_action_kind( const std::string& cls )
{
    static const action_kind_t kinds[] = {
        HYDRO_ACTION_KIND( "VuDelayEntity", delay_properties_t, delay_construct, delay_load ),
        HYDRO_ACTION_KIND( "VuPowerUpBoostEntity", powerup_properties_t, powerup_construct, powerup_load ),
        HYDRO_ACTION_KIND( "VuPowerUpTriggerEntity", powerup_properties_t, powerup_construct, powerup_load ),
        HYDRO_ACTION_KIND( "VuBooleanEntity", boolean_properties_t, boolean_construct, boolean_load ),
        HYDRO_ACTION_KIND( "VuGenericEventEntity", generic_event_properties_t, generic_event_construct, generic_event_load ),
        HYDRO_ACTION_KIND( "VuRotateMotionEntity", rotate_motion_properties_t, rotate_motion_construct, rotate_motion_load ),
        HYDRO_ACTION_KIND( "VuAttachEntity", attach_properties_t, attach_construct, attach_load ),
        HYDRO_ACTION_KIND( "VuPlayAnimationEntity", play_animation_properties_t, play_animation_construct, play_animation_load ),
        HYDRO_ACTION_KIND( "VuSkyBoxEntity", sky_box_properties_t, sky_box_construct, sky_box_load ),
        HYDRO_ACTION_KIND( "VuWaterTextureSettingsEntity", water_texture_properties_t, water_texture_construct, water_texture_load ),
        HYDRO_ACTION_KIND( "VuSetFrontEndCameraEntity", front_end_camera_properties_t, front_end_camera_construct, front_end_camera_load ),
        HYDRO_ACTION_KIND( "VuPointWaveEntity", point_wave_properties_t, point_wave_construct, point_wave_load ),
    };
    for ( const action_kind_t& k : kinds )
        if ( cls == k.m_class )
            return &k;
    return nullptr;
}

#undef HYDRO_ACTION_KIND

// The loaded properties of an entity of action class T, or null.
template < class T >
inline const T* entity_action( const entity_t& e )
{
    return e.m_state && entity_action_kind( e.m_type ) ? &static_cast< const props_state_t< T >* >( e.m_state.get( ) )->value : nullptr;
}

// The sky's Vu3dDrawComponent: only its "Reflect" is read here (the shadow pass has no sky).
inline void sky_box_component_load( entity_t& owner, const value_t& properties )
{
    if ( const sky_box_properties_t* p = entity_action< sky_box_properties_t >( owner ) )
    {
        sky_box_properties_t* mine = const_cast< sky_box_properties_t* >( p );
        property_bool( properties, "Reflect", mine->reflect, mine->reflect );
    }
}

// VuPropEntity's own property the race reads: "Initially Visible" (ctor 0x5464C0: bool vt 0x737450, target +0x78,
// default 1 - the word 0x0101 stored at +0x78 before the node is made).
struct prop_entity_state_t
{
    std::uint8_t visible = 1, visible_default = 1;
};

inline std::shared_ptr< void > entity_state_for( const std::string& cls )
{
    if ( cls == "VuPropEntity" )
        return std::make_shared< prop_entity_state_t >( );
    if ( const action_kind_t* k = entity_action_kind( cls ) )
        return k->m_make( );
    if ( cls == "VuFoliageEntity" )
        return std::make_shared< foliage_state_t >( );
    if ( cls == "VuCoronaEntity" )
    {
        auto s = std::make_shared< corona_state_t >( );
        corona_construct( s->value );
        corona_construct( s->defaults );
        return s;
    }
    if ( cls == "VuAmbientLightEntity" )
    {
        auto s = std::make_shared< ambient_light_state_t >( );
        ambient_light_construct( s->value );
        ambient_light_construct( s->defaults );
        return s;
    }
    if ( cls == "VuDirectionalLightEntity" )
    {
        auto s = std::make_shared< directional_light_state_t >( );
        directional_light_construct( s->value );
        directional_light_construct( s->defaults );
        return s;
    }
    if ( cls == "VuGlobalGfxSettingsEntity" )
    {
        auto s = std::make_shared< gfx_settings_state_t >( );
        gfx_settings_construct( s->value );
        gfx_settings_construct( s->defaults );
        return s;
    }
    if ( cls == "VuRectangularGfxSettingsEntity" || cls == "VuEllipticalGfxSettingsEntity" )
    {
        auto s = std::make_shared< gfx_zone_state_t >( );
        gfx_settings_construct( s->value );
        gfx_settings_construct( s->defaults );
        return s;
    }
    if ( entity_is_trigger( cls ) )
    {
        auto s = std::make_shared< trigger_state_t >( );
        trigger_construct( s->value );
        trigger_construct( s->defaults );
        return s;
    }
    if ( cls == "VuStaticPfxEntity" )
    {
        auto s = std::make_shared< static_pfx_state_t >( );
        static_pfx_construct( s->value );
        static_pfx_construct( s->defaults );
        return s;
    }
    // The water classes (world/surface.hxx).
    if ( auto water = water_entity_state_for( cls ) )
        return water;
    return nullptr;
}

// VuEntity::load's "Properties" step (0x5009D0 and the override load 0x5015D0
// alike): each property node's vt+8 load, reset to its default first.
inline void entity_load_properties( entity_t& entity, const value_t& properties )
{
    if ( const action_kind_t* k = entity_action_kind( entity.m_type ) )
    {
        if ( entity.m_state )
            k->m_load( entity.m_state.get( ), properties );
        return;
    }
    if ( entity.m_type == "VuPropEntity" && entity.m_state )
    {
        prop_entity_state_t* s = static_cast< prop_entity_state_t* >( entity.m_state.get( ) );
        property_bool( properties, "Initially Visible", s->visible, s->visible_default );
    }
    else if ( entity.m_type == "VuFoliageEntity" && entity.m_state )
    {
        // Each load falls back to what stands: the ctor's value, or the template's (the loaded values are the defaults).
        foliage_state_t* s = static_cast< foliage_state_t* >( entity.m_state.get( ) );
        property_string( properties, "Texture Asset", s->texture, std::string( s->texture ) );
        property_bool( properties, "Fog Enabled", s->fog, s->fog );
        property_color( properties, "Color", s->color, s->color );
        property_float( properties, "U0", s->uv[ 0 ], s->uv[ 0 ], false );
        property_float( properties, "V0", s->uv[ 1 ], s->uv[ 1 ], false );
        property_float( properties, "U1", s->uv[ 2 ], s->uv[ 2 ], false );
        property_float( properties, "V1", s->uv[ 3 ], s->uv[ 3 ], false );
        property_float( properties, "Draw Distance", s->draw_distance, s->draw_distance, false );
    }
    else if ( entity.m_type == "VuCoronaEntity" && entity.m_state )
    {
        corona_state_t* s = static_cast< corona_state_t* >( entity.m_state.get( ) );
        corona_load( s->value, properties, s->defaults );
    }
    else if ( entity.m_type == "VuAmbientLightEntity" && entity.m_state )
    {
        ambient_light_state_t* s = static_cast< ambient_light_state_t* >( entity.m_state.get( ) );
        ambient_light_load( s->value, properties, s->defaults );
    }
    else if ( entity.m_type == "VuDirectionalLightEntity" && entity.m_state )
    {
        directional_light_state_t* s = static_cast< directional_light_state_t* >( entity.m_state.get( ) );
        directional_light_load( s->value, properties, s->defaults );
    }
    else if ( entity.m_type == "VuGlobalGfxSettingsEntity" && entity.m_state )
    {
        gfx_settings_state_t* s = static_cast< gfx_settings_state_t* >( entity.m_state.get( ) );
        gfx_settings_load( s->value, properties, s->defaults );
    }
    else if ( ( entity.m_type == "VuRectangularGfxSettingsEntity" || entity.m_type == "VuEllipticalGfxSettingsEntity" ) && entity.m_state )
    {
        gfx_zone_state_t* s = static_cast< gfx_zone_state_t* >( entity.m_state.get( ) );
        gfx_settings_load( s->value, properties, s->defaults );
        if ( entity.m_type == "VuRectangularGfxSettingsEntity" )
        {
            property_percentage( properties, "Inner Radius X %", s->inner[ 0 ], s->inner_defaults[ 0 ] );
            property_percentage( properties, "Inner Radius Y %", s->inner[ 1 ], s->inner_defaults[ 1 ] );
        }
        else
            property_percentage( properties, "Inner Radius %", s->inner[ 0 ], s->inner_defaults[ 0 ] );
    }
    else if ( entity_is_trigger( entity.m_type ) && entity.m_state )
    {
        trigger_state_t* s = static_cast< trigger_state_t* >( entity.m_state.get( ) );
        trigger_load( s->value, properties, s->defaults );
    }
    else if ( entity.m_type == "VuStaticPfxEntity" && entity.m_state )
    {
        static_pfx_state_t* s = static_cast< static_pfx_state_t* >( entity.m_state.get( ) );
        static_pfx_load( s->value, properties, s->defaults );
    }
    else if ( water_kind_of( entity.m_type ) != water_kind_none && entity.m_state )
        water_entity_load( *static_cast< water_entity_state_t* >( entity.m_state.get( ) ), properties );
}

// The template mark's property step (each node's vt+0x1C): default = value.
inline void entity_mark_properties( entity_t& entity )
{
    if ( const action_kind_t* k = entity_action_kind( entity.m_type ) )
    {
        if ( entity.m_state )
            k->m_mark( entity.m_state.get( ) );
        return;
    }
    if ( entity.m_type == "VuPropEntity" && entity.m_state )
    {
        prop_entity_state_t* s = static_cast< prop_entity_state_t* >( entity.m_state.get( ) );
        s->visible_default = s->visible;
    }
    else if ( entity.m_type == "VuCoronaEntity" && entity.m_state )
    {
        corona_state_t* s = static_cast< corona_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( entity.m_type == "VuAmbientLightEntity" && entity.m_state )
    {
        ambient_light_state_t* s = static_cast< ambient_light_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( entity.m_type == "VuDirectionalLightEntity" && entity.m_state )
    {
        directional_light_state_t* s = static_cast< directional_light_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( entity.m_type == "VuGlobalGfxSettingsEntity" && entity.m_state )
    {
        gfx_settings_state_t* s = static_cast< gfx_settings_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( ( entity.m_type == "VuRectangularGfxSettingsEntity" || entity.m_type == "VuEllipticalGfxSettingsEntity" ) && entity.m_state )
    {
        gfx_zone_state_t* s = static_cast< gfx_zone_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
        s->inner_defaults[ 0 ] = s->inner[ 0 ];
        s->inner_defaults[ 1 ] = s->inner[ 1 ];
    }
    else if ( entity_is_trigger( entity.m_type ) && entity.m_state )
    {
        trigger_state_t* s = static_cast< trigger_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( entity.m_type == "VuStaticPfxEntity" && entity.m_state )
    {
        static_pfx_state_t* s = static_cast< static_pfx_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
    }
    else if ( water_kind_of( entity.m_type ) != water_kind_none && entity.m_state )
    {
        water_entity_state_t* s = static_cast< water_entity_state_t* >( entity.m_state.get( ) );
        s->defaults = s->value;
        s->water_map_default = s->water_map;   // VuAssetProperty<VuWaterMapAsset> slot 7 0x406A70
    }
}

// entity_components_for with each recreated component's state constructed:
// the transform's ctor 0x534180, whose 0x534680 registers Position, Rotation
// and Scale with their targets' values as the defaults.
inline std::vector< entity_component_t > entity_components_constructed( const std::string& cls )
{
    std::vector< entity_component_t > out = entity_components_for( cls );
    for ( entity_component_t& c : out )
        if ( c.m_vtable == k_transform_component_vtable )
        {
            auto state = std::make_shared< transform_state_t >( );
            transform_construct( state->c );
            std::memcpy( state->position, state->c.local + 12, sizeof( state->position ) );
            std::memcpy( state->rotation, state->c.rotation, sizeof( state->rotation ) );
            std::memcpy( state->scale, state->c.scale, sizeof( state->scale ) );
            c.m_state = state;
        }
        else if ( c.m_vtable == k_static_model_component_vtable || c.m_vtable == k_animated_model_component_vtable )
        {
            auto state = std::make_shared< model_draw_state_t >( );
            model_draw_construct( state->value, c.m_vtable == k_animated_model_component_vtable );
            state->defaults = state->value;
            c.m_state = state;
        }
        else if ( c.m_vtable == k_rigid_body_component_vtable )
        {
            auto state = std::make_shared< rigid_component_state_t >( );
            rigid_component_construct( state->value );
            state->defaults = state->value;
            c.m_state = state;
        }
    return out;
}

// entity_components_constructed with the VuScriptComponent's state too: the
// plugs and refs its owner's ctor declares (game/plugs.hxx), no link yet. A
// separate step because the UI's loader keeps its own state on that component
// (its "Enable" node - core/ui entity.hxx).
inline std::vector< entity_component_t > entity_components_scripted( const std::string& cls )
{
    std::vector< entity_component_t > out = entity_components_constructed( cls );
    for ( entity_component_t& c : out )
        if ( c.m_vtable == k_script_component_vtable )
            c.m_state = script_construct( cls, g_script_facts );
    return out;
}

// The components' post-loads recreated so far: VuScriptComponent's (vt+0xC
// 0x503D70, vt+0x10 0x504320 - engine entity/script.hxx). Every other is a
// hook (the transform's runs inside entity_load_component).
inline void entity_post_load_component( entity_t& owner, entity_component_t& component, const value_t& data, bool override )
{
    if ( component.m_vtable == k_script_component_vtable && component.m_state )
        script_load( owner, data, override, g_script_facts );
}