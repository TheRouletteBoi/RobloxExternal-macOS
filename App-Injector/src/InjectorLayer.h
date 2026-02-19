#pragma once

#include "Walnut/Layer.h"

#ifdef WL_HEADLESS
#include "HeadlessConsole.h"
#else
#include "Walnut/UI/Console.h"
#endif

#include <memory>
#include <atomic>
#include <thread>

#include "process/process.hpp"
#include "memory/memory.hpp"
#include "macho/macho.hpp"
#include "scanner/scanner.hpp"
#include "roblox.hpp"
#include "games.hpp"
#include "../../App-Common/esp_controller.hpp"

class InjectorLayer : public Walnut::Layer
{
public:
	InjectorLayer();
	virtual ~InjectorLayer();

	virtual void OnAttach() override;
	virtual void OnDetach() override;
	virtual void OnUpdate(float ts) override;
#ifndef WL_HEADLESS
	virtual void OnUIRender() override;
#endif

	void Shutdown();
	void ReloadDylib();

private:
	bool InitializeInjection();
	bool InitializeESP();
	bool InitializeGame();
	bool InitializeDumper();
	void DumpStudioOffsets();
	void DetectGameProfile();

	void StartCharacterRefreshThread();
	void StartAntiAFKThread();
	void StartESPThread();

	void UpdateESPAndAim(float deltaTime);
	void UpdateProfileScreenSize();
	bool IsAimKeyHeld() const;

#ifdef WL_HEADLESS
	void HandleHotkeys();
	void ShowHotkeys();
#else
	void UI_StatusPanel();
	void UI_GameInfo();
	void UI_ESPControls();
	void UI_AimbotControls();
	void UI_PredictionControls();
	void UI_TargetSelectionControls();
	void UI_DebugPanel();
#endif

private:
	std::unique_ptr<dumper::DumperContext> m_dumper;
	std::unique_ptr<ESPController> m_espController;
	std::unique_ptr<roblox::GameContext> m_dataModel;
	std::unique_ptr<games::GameProfileFactory> m_profileFactory;
	std::unique_ptr<games::GenericProfile> m_genericProfile;
	games::GameProfile* m_activeProfile = nullptr;
	std::unique_ptr<games::AimSettings> m_aimSettings;

	task_t m_task = 0;
	pid_t m_pid = 0;

	std::atomic<bool> m_running{true};
	std::atomic<bool> m_espThreadRunning{false};
	std::atomic<bool> m_shutdown{false};
	std::thread m_espThread;
	std::thread m_characterRefreshThread;
	std::thread m_antiAFKThread;

	bool m_isConnected = false;
	bool m_gameFound = false;
	std::string m_statusMessage = "Initializing...";
	std::string m_currentGameName = "Unknown";
	int64_t m_placeId = 0;

	bool m_showDebugPanel = false;
	bool m_showGameInfo = true;
	bool m_showESPControls = true;
	bool m_showAimbotControls = true;
	bool m_showPredictionControls = false;
	bool m_showTargetSelection = true;
	int m_targetCount = 0;
	float m_currentFPS = 0.0f;

	bool m_espEnabled = true;
	bool m_aimbotEnabledGui = true;
	float m_maxDistanceGui = 500.0f;
	float m_maxDeltaDistanceGui = 30.0f;
	float m_smoothingGui = 5.0f;
	int m_aimStyleGui = 0; // 0=Silent, 1=Legit, 2=Snap
	int m_targetSelectionGui = 0; // 0=Crosshair, 1=Distance, 2=Health, 3=Mouse
	int m_aimPartGui = 1; // 0=Head, 1=Torso, 2=Custom
	int m_aimKeyGui = 0; // 0=LeftMouse, 1=RightMouse, 2=k
	bool m_predictionEnabledGui = false;
	float m_bulletSpeedGui = 3000.0f;
	float m_gravityGui = 196.2f;
	bool m_predictGravityGui = true;

	float m_enemyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red
	float m_teammateColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f }; // Green
	float m_targetColor[4] = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta
};
