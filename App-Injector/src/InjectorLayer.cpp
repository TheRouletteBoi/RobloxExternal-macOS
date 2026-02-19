#include "InjectorLayer.h"

#include "Walnut/Application.h"
#ifndef WL_HEADLESS
#include "Walnut/UI/UI.h"
#include "misc/cpp/imgui_stdlib.h"
#endif

#include <iostream>
#include <chrono>
#include <cmath>

namespace config {
	constexpr const char* APP_NAME = "RobloxPlayer";
	constexpr const char* SHM_PATH = "/tmp/esp_shared_memory";
	constexpr const char* DYLIB_NAME = "libApp-ESPManager.dylib";
}

InjectorLayer::InjectorLayer()
{
}

InjectorLayer::~InjectorLayer()
{
	Shutdown();
}

void InjectorLayer::Shutdown()
{
	if (m_shutdown)
		return;

	m_running = false;
	m_espThreadRunning = false;

	if (m_espThread.joinable())
		m_espThread.join();
	if (m_characterRefreshThread.joinable())
		m_characterRefreshThread.join();
	if (m_antiAFKThread.joinable())
		m_antiAFKThread.join();

	if (m_espController) {
		m_espController->clear_esp();
		m_espController->disable_esp();
	}

	m_shutdown.store(true, std::memory_order_relaxed);
}

void InjectorLayer::OnAttach()
{
	m_profileFactory = std::make_unique<games::GameProfileFactory>();
	m_profileFactory->register_profile<games::PhantomForcesProfile>();
	m_profileFactory->register_profile<games::MurderMystery2Profile>();
	m_profileFactory->register_profile<games::MurderersVsSheriffsProfile>();
	m_profileFactory->register_profile<games::MurderersVsSheriffsDuelsProfile>();
	m_profileFactory->register_profile<games::RivalsProfile>();

	m_genericProfile = std::make_unique<games::GenericProfile>();
	m_aimSettings = std::make_unique<games::AimSettings>();

	if (!InitializeInjection()) {
		m_statusMessage = "Failed to inject into RobloxPlayer\nMake sure the app is running and you have SIP Disabled.";
		std::println("{}", m_statusMessage);
		return;
	}

	if (!InitializeDumper()) {
		m_statusMessage = "Failed to initialize Dumper";
		std::println("{}", m_statusMessage);
		return;
	}

	if (!InitializeESP()) {
		m_statusMessage = "Failed to initialize ESP controller";
		std::println("{}", m_statusMessage);
		return;
	}

	if (!InitializeGame()) {
		m_statusMessage = "Waiting for game...";
		std::println("{}", m_statusMessage);
		return;
	}

	DumpStudioOffsets();
	StartCharacterRefreshThread();
	StartAntiAFKThread();
#ifdef WL_HEADLESS
	ShowHotkeys();
#endif
	StartESPThread();

	m_statusMessage = "Connected and running";
	std::println("{}", m_statusMessage);
	m_isConnected = true;
}

void InjectorLayer::OnDetach()
{
	m_running = false;
}

void InjectorLayer::OnUpdate(float ts)
{
#ifdef WL_HEADLESS
	HandleHotkeys();
#endif
}

bool InjectorLayer::InitializeInjection()
{
	auto injection_result = process::inject_dylib(
		config::APP_NAME,
		config::DYLIB_NAME,
		process::InjectionMode::AUTO,
		false
	);

	if (!injection_result.success) {
		return false;
	}

	m_task = injection_result.task;
	m_pid = injection_result.pid;
	return true;
}

bool InjectorLayer::InitializeDumper()
{
	try {
		m_dumper = std::make_unique<dumper::DumperContext>(m_task);
		return true;
	}
	catch (const std::exception& e) {
		return false;
	}
}

bool InjectorLayer::InitializeESP()
{
	try {
		m_espController = std::make_unique<ESPController>(config::SHM_PATH);

		if (!m_espController->wait_for_dylib(5000)) {
			return false;
		}

		m_espController->enable_esp();
		return true;
	}
	catch (const std::exception& e) {
		return false;
	}
}

bool InjectorLayer::InitializeGame()
{
	try {
		m_dataModel = std::make_unique<roblox::GameContext>(m_task);

		if (!m_dataModel) {
			std::println("Failed to find game");
			return false;
		}

		std::println("Game: {:#X}", m_dataModel->game().address());

		if (!m_dataModel->is_valid()) {
			return false;
		}
		return true;
	}
	catch (const std::exception& e) {
		return false;
	}
}

void InjectorLayer::DumpStudioOffsets()
{
	if (!m_dataModel)
		return;

	std::println("Character: {:#X}", m_dataModel->my_hrp().address());

	// TODO(Roulette): move this into a function in dumper named update_offsets()
	dumper::DumperContext::LiveInstances live;
	live.game = m_dataModel->game().address();
	live.workspace = m_dataModel->workspace().address();
	live.players = m_dataModel->players().address();
	live.camera = m_dataModel->camera().address();
	live.local_player = m_dataModel->local_player().address();

	m_dataModel->refresh_character();
	if (m_dataModel->my_character()) {
		live.character = m_dataModel->my_character().address();

		auto humanoid = m_dataModel->my_humanoid();
		if (humanoid) live.humanoid = humanoid.address();

		auto hrp = m_dataModel->my_hrp();
		if (hrp) live.hrp = hrp.address();
	}
	m_dumper->find_studio_offsets(live);
	m_dumper->print_found_offsets();
}

void InjectorLayer::ReloadDylib()
{
	auto result = process::inject_dylib(config::APP_NAME, config::DYLIB_NAME, process::InjectionMode::FORCE_RESTART);
	if (!result.success) {
		std::println("Failed to restart with dylib");
	}
}

void InjectorLayer::DetectGameProfile()
{
	if (!m_dataModel)
		return;

	m_activeProfile = m_profileFactory->detect_game(*m_dataModel);

	if (m_activeProfile) {
		m_currentGameName = m_activeProfile->name();
		m_activeProfile->initialize(*m_dataModel);
	} else {
		m_currentGameName = "Generic (No specific profile)";
		m_activeProfile = m_genericProfile.get();
		m_genericProfile->initialize(*m_dataModel);
	}

	m_aimSettings->aim_key = m_activeProfile->default_aim_key();
	std::println("Aim key: {}",
			m_aimSettings->aim_key == games::AimKey::LeftMouse ? "Left Mouse" :
			m_aimSettings->aim_key == games::AimKey::RightMouse ? "Right Mouse" : "E Key");

	UpdateProfileScreenSize();
}

void InjectorLayer::StartCharacterRefreshThread()
{
	m_characterRefreshThread = std::thread([this]() {
		int64_t lastPlaceId = 0;

		while (m_running) {
			if (m_dataModel) {
				int64_t currentPlaceId = m_dataModel->place_id();
				std::println("Place ID: {}", currentPlaceId);

				if (currentPlaceId != 0 && currentPlaceId != lastPlaceId) {
					std::println("Place ID changed: {} -> {}", lastPlaceId, currentPlaceId);
					lastPlaceId = currentPlaceId;

					m_placeId = currentPlaceId;
					m_gameFound = true;
					//std::println("Game found!");
					//m_game->print_info();

					std::println("Detecting game profile for PlaceId: {}", currentPlaceId);
					DetectGameProfile();

					m_statusMessage = "Connected and running";
					m_isConnected = true;
				}

				if (lastPlaceId != 0 && currentPlaceId == 0) {
					std::println("Place ID lost, waiting for rejoin...");
					m_statusMessage = "Waiting for game...";
					m_isConnected = false;
					lastPlaceId = 0;
					m_gameFound = false;
				}
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});
}

void InjectorLayer::StartAntiAFKThread()
{
	m_antiAFKThread = std::thread([this]() {
		while (m_running) {
			if (m_dataModel) {
				auto local = m_dataModel->local_player();
				if (local) {
					local.set_last_input_timestamp(9999999.0);
				}
			}
			std::this_thread::sleep_for(std::chrono::seconds(5));
		}
	});
}

void InjectorLayer::StartESPThread()
{
	m_espThreadRunning = true;
	m_espThread = std::thread([this]() {
		auto lastTime = std::chrono::steady_clock::now();

		while (m_espThreadRunning) {
			auto now = std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(now - lastTime).count();
			lastTime = now;

			m_currentFPS = 1.0f / deltaTime;

			if (m_activeProfile) {
				m_activeProfile->update(*m_dataModel);
			}

			UpdateProfileScreenSize();

			UpdateESPAndAim(deltaTime);

			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	});
}

void InjectorLayer::UpdateProfileScreenSize()
{
	if (!m_espController || !m_activeProfile)
		return;

	float w = m_espController->window_width();
	float h = m_espController->window_height();

	m_activeProfile->set_screen_size(w, h);

	if (auto* generic = dynamic_cast<games::GenericProfile*>(m_activeProfile)) {
		generic->config.max_delta_dist = m_aimSettings->max_delta_dist;
		generic->config.max_distance = m_aimSettings->max_distance;
	}
	if (auto* pf = dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
		pf->config.max_delta_dist = m_aimSettings->max_delta_dist;
		pf->config.max_distance = m_aimSettings->max_distance;
	}
}

bool InjectorLayer::IsAimKeyHeld() const
{
	if (!m_espController)
		return false;

	switch (m_aimSettings->aim_key) {
		case games::AimKey::LeftMouse:
			return m_espController->is_left_mouse_down();
		case games::AimKey::RightMouse:
			return m_espController->is_right_mouse_down();
		case games::AimKey::KeyK:
			return m_espController->is_key_down('k') || m_espController->is_key_down('K');
	}
	return false;
}

void InjectorLayer::UpdateESPAndAim(float deltaTime)
{
	if (!m_espController || !m_dataModel || !m_activeProfile)
		return;

#ifndef WL_HEADLESS
	m_aimSettings->enabled = m_aimbotEnabledGui;
	m_aimSettings->max_delta_dist = m_maxDeltaDistanceGui;
	m_aimSettings->max_distance = m_maxDistanceGui;
	m_aimSettings->smoothing = m_smoothingGui;
	m_aimSettings->prediction_enabled = m_predictionEnabledGui;
	m_aimSettings->bullet_speed = m_bulletSpeedGui;
	m_aimSettings->gravity = m_gravityGui;
	m_aimSettings->predict_gravity = m_predictGravityGui;

	m_aimSettings->style = static_cast<games::AimStyle>(m_aimStyleGui);
	m_aimSettings->selection = static_cast<games::TargetSelection>(m_targetSelectionGui);
	m_aimSettings->aim_part = static_cast<games::AimPart>(m_aimPartGui);
	m_aimSettings->aim_key = static_cast<games::AimKey>(m_aimKeyGui);
#endif

	auto camera = m_dataModel->camera();
	if (!camera) {
		m_espController->clear_esp();
		return;
	}

	roblox::CFrame camera_cf = camera.cframe();
	float fov = camera.field_of_view();
	float screen_w = m_espController->window_width();
	float screen_h = m_espController->window_height();

	m_activeProfile->set_mouse_position(m_espController->mouse_x(), m_espController->mouse_y());

	auto targets = m_activeProfile->find_targets(*m_dataModel, camera_cf, fov);

	if (targets.empty()) {
		m_espController->clear_esp();
		return;
	}

	m_espController->begin_frame();

	float screen_center_x = screen_w / 2.0f;
	float screen_center_y = screen_h / 2.0f;

	// Convert delta distance (studs) to screen pixels
	float fov_radians = fov * (3.14159f / 180.0f);
	float tan_half_fov = std::tan(fov_radians * 0.5f);

	float normalized = std::clamp(m_aimSettings->max_delta_dist / m_aimSettings->max_studs, 0.0f, 1.0f);

	float pixel_radius = normalized * (screen_h * 0.45f);

	ESPColor blue{0.3f, 0.6f, 1.0f, 0.5f};
	m_espController->add_circle(screen_center_x, screen_center_y, pixel_radius, blue, 2.0f, false);

	games::Target* best_target = nullptr;
	float best_score = std::numeric_limits<float>::max();

	for (auto& t : targets) {
		if (!t.is_valid)
			continue;

		if (m_aimSettings->selection != games::TargetSelection::ClosestToMouse) {
			if (t.delta_distance > m_aimSettings->max_delta_dist)
				continue;
		}

		float score = 0;
		switch (m_aimSettings->selection) {
			case games::TargetSelection::ClosestToCrosshair:
				score = t.delta_distance;
				break;
			case games::TargetSelection::ClosestDistance:
				score = t.distance_3d;
				break;
			case games::TargetSelection::LowestHealth:
				score = t.health;
				break;
			case games::TargetSelection::ClosestToMouse:
				score = t.screen_distance;
				break;
		}

		if (score < best_score) {
			best_score = score;
			best_target = &t;
		}
	}

	bool aim_held = IsAimKeyHeld();

	if (aim_held && best_target && m_aimSettings->enabled) {
		if (m_aimSettings->selection == games::TargetSelection::ClosestToMouse) {
			m_activeProfile->apply_aim_mouse(*best_target, *m_dataModel, camera_cf, *m_aimSettings, *m_espController);
		} else {
			m_activeProfile->apply_aim(*best_target, *m_dataModel, camera_cf, *m_aimSettings);
		}

		static int aim_log_counter = 0;
		if (++aim_log_counter >= 30) {
			aim_log_counter = 0;
			std::println("[AIM] {} | dist={:.1f} delta={:.2f} style={} mode={}",
						best_target->name,
						best_target->distance_3d,
						best_target->delta_distance,
						static_cast<int>(m_aimSettings->style),
						m_aimSettings->selection == games::TargetSelection::ClosestToMouse ? "MOUSE" : "CAMERA");
		}
	}

	for (const auto& target : targets) {
		if (!target.is_valid)
			continue;

		bool is_aim_target = (best_target && &target == best_target && aim_held);
		ESPColor color = m_activeProfile->get_target_color(target, is_aim_target);
		float border = m_activeProfile->get_target_border_width(target, is_aim_target);

		std::string label = target.name;
		label += std::format(" [{:.0f}m]", target.distance_3d);

		float box_width = std::max(20.0f, 2000.0f / target.distance_3d);
		float box_height = box_width * 2.0f;

		float x = target.screen_position.x;
		float y = target.screen_position.y;

		m_espController->add_box(x - box_width / 2, y - box_height / 2,
					 box_width, box_height,
					 color, label, border);
	}

	m_espController->end_frame();
}

#ifdef WL_HEADLESS
void InjectorLayer::ShowHotkeys()
{
	std::println("=== Controls ===");
	std::println("  Aim Key    - {} (game-dependent)",
		m_aimSettings->aim_key == games::AimKey::LeftMouse ? "Left Mouse" :
		m_aimSettings->aim_key == games::AimKey::RightMouse ? "Right Mouse" : "E Key");
	std::println("  \\           - Quit");
	std::println("  |           - Reload");
	std::println("");
	std::println("=== Aim Style ===");
	std::println("  * - Silent (direct camera write)");
	std::println("  ( - Legit (smooth interpolation)");
	std::println("  ) - Snap (instant lock)");
	std::println("");
	std::println("=== Target Selection ===");
	std::println("  % - Closest to Crosshair");
	std::println("  ^ - Closest Distance");
	std::println("  & - Lowest Health");
	std::println("  $ - Closest to Mouse");
	std::println("");
	std::println("=== Aim Part ===");
	std::println("  @ - Head");
	std::println("  # - Torso");
	std::println("");
	std::println("=== Prediction ===");
	std::println("  P     - Toggle prediction on/off");
	std::println("  G     - Toggle gravity prediction");
	std::println("  , / . - Decrease/Increase bullet speed");
	std::println("");
	std::println("=== Adjustments ===");
	std::println("  [ / ] - Decrease/Increase smoothing");
	std::println("  - / = - Decrease/Increase max delta distance");
	std::println("  ;     - Toggle aim on/off");
	if (dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
		std::println("  T     - Switch target team (PF only)");
	}
	std::println("");
}

void InjectorLayer::HandleHotkeys()
{
	if (!m_espController || !m_dataModel || !m_activeProfile)
		return;

	if (m_espController->was_key_pressed('|')) {
		ReloadDylib();
	}

	if (m_espController->was_key_pressed('t') || m_espController->was_key_pressed('T')) {
		if (auto* pf = dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
			pf->switch_teams();
		}
	}

	// Aim style toggle: * = Silent, ( = Legit, ) = Snap
	if (m_espController->was_key_pressed('*')) {
		m_aimSettings->style = games::AimStyle::Silent;
		std::println("[CONFIG] Aim style: Silent");
	}
	if (m_espController->was_key_pressed('(')) {
		m_aimSettings->style = games::AimStyle::Legit;
		std::println("[CONFIG] Aim style: Legit (smoothing={})", m_aimSettings->smoothing);
	}
	if (m_espController->was_key_pressed(')')) {
		m_aimSettings->style = games::AimStyle::Snap;
		std::println("[CONFIG] Aim style: Snap");
	}

	// Target selection: % = Crosshair, ^ = Distance, & = Health
	if (m_espController->was_key_pressed('%')) {
		m_aimSettings->selection = games::TargetSelection::ClosestToCrosshair;
		std::println("[CONFIG] Selection: Closest to Crosshair");
	}
	if (m_espController->was_key_pressed('^')) {
		m_aimSettings->selection = games::TargetSelection::ClosestDistance;
		std::println("[CONFIG] Selection: Closest Distance");
	}
	if (m_espController->was_key_pressed('&')) {
		m_aimSettings->selection = games::TargetSelection::LowestHealth;
		std::println("[CONFIG] Selection: Lowest Health");
	}
	if (m_espController->was_key_pressed('$')) {
		m_aimSettings->selection = games::TargetSelection::ClosestToMouse;
		std::println("[CONFIG] Selection: Closest to Mouse");
	}

	// Aim part: @ = Head, # = Torso
	if (m_espController->was_key_pressed('@')) {
		m_aimSettings->aim_part = games::AimPart::Head;
		m_activeProfile->set_aim_part("Head");
		std::println("[CONFIG] Aim part: Head");
	}
	if (m_espController->was_key_pressed('#')) {
		m_aimSettings->aim_part = games::AimPart::Torso;
		m_activeProfile->set_aim_part("HumanoidRootPart");
		std::println("[CONFIG] Aim part: Torso");
	}

	// Smoothing adjustment: [ and ] keys
	if (m_espController->was_key_pressed('[')) {
		m_aimSettings->smoothing = std::max(1.0f, m_aimSettings->smoothing - 1.0f);
		std::println("[CONFIG] Smoothing: {}", m_aimSettings->smoothing);
	}
	if (m_espController->was_key_pressed(']')) {
		m_aimSettings->smoothing = std::min(20.0f, m_aimSettings->smoothing + 1.0f);
		std::println("[CONFIG] Smoothing: {}", m_aimSettings->smoothing);
	}

	if (m_espController->was_key_pressed('p') || m_espController->was_key_pressed('P')) {
		m_aimSettings->prediction_enabled = !m_aimSettings->prediction_enabled;
		std::println("[CONFIG] Prediction: {}", m_aimSettings->prediction_enabled ? "ENABLED" : "DISABLED");
	}

	if (m_espController->was_key_pressed('g') || m_espController->was_key_pressed('G')) {
		m_aimSettings->predict_gravity = !m_aimSettings->predict_gravity;
		std::println("[CONFIG] Gravity prediction: {}", m_aimSettings->predict_gravity ? "ENABLED" : "DISABLED");
	}

	// Bullet speed adjustment
	if (m_espController->was_key_pressed(',')) {
		m_aimSettings->bullet_speed = std::max(100.0f, m_aimSettings->bullet_speed - 100.0f);
		std::println("[CONFIG] Bullet speed: {} studs/sec", m_aimSettings->bullet_speed);
	}
	if (m_espController->was_key_pressed('.')) {
		m_aimSettings->bullet_speed = std::min(10000.0f, m_aimSettings->bullet_speed + 100.0f);
		std::println("[CONFIG] Bullet speed: {} studs/sec", m_aimSettings->bullet_speed);
	}

	// Max delta distance: - and = keys
	if (m_espController->was_key_pressed('-')) {
		m_aimSettings->max_delta_dist = std::max(m_aimSettings->min_studs, m_aimSettings->max_delta_dist - 1.0f);
		std::println("[CONFIG] Max delta: {} studs", m_aimSettings->max_delta_dist);
	}
	if (m_espController->was_key_pressed('=')) {
		m_aimSettings->max_delta_dist = std::min(m_aimSettings->max_studs, m_aimSettings->max_delta_dist + 1.0f);
		std::println("[CONFIG] Max delta: {} studs", m_aimSettings->max_delta_dist);
	}

	// Toggle aim on/off: ;
	if (m_espController->was_key_pressed(';')) {
		m_aimSettings->enabled = !m_aimSettings->enabled;
		std::println("[CONFIG] Aim: {}", m_aimSettings->enabled ? "ENABLED" : "DISABLED");
	}
}
#else

void InjectorLayer::OnUIRender()
{
	UI_StatusPanel();

	if (m_isConnected) {
		if (m_showGameInfo)
			UI_GameInfo();
		if (m_showESPControls)
			UI_ESPControls();
		if (m_showAimbotControls)
			UI_AimbotControls();
		if (m_showPredictionControls)
			UI_PredictionControls();
		if (m_showTargetSelection)
			UI_TargetSelectionControls();
		if (m_showDebugPanel)
			UI_DebugPanel();
	}
}

void InjectorLayer::UI_StatusPanel()
{
	ImGui::Begin("Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	if (m_isConnected) {
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● Connected");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "● Disconnected");
	}

	ImGui::Text("Status: %s", m_statusMessage.c_str());

	if (m_isConnected) {
		ImGui::Text("PID: %d", m_pid);
		ImGui::Text("FPS: %.1f", m_currentFPS);
		ImGui::Text("Targets: %d", m_targetCount);
	}

	ImGui::Separator();

	ImGui::Checkbox("Game Info", &m_showGameInfo);
	ImGui::Checkbox("ESP Controls", &m_showESPControls);
	ImGui::Checkbox("Aimbot", &m_showAimbotControls);
	ImGui::Checkbox("Prediction", &m_showPredictionControls);
	ImGui::Checkbox("Target Selection", &m_showTargetSelection);
	ImGui::Checkbox("Debug", &m_showDebugPanel);

	ImGui::End();
}

void InjectorLayer::UI_GameInfo()
{
	ImGui::Begin("Game Information");

	ImGui::Text("Game: %s", m_currentGameName.c_str());
	ImGui::Text("Place ID: %llu", m_placeId);

	if (m_dataModel) {
		auto jobId = m_dataModel->job_id();
		if (jobId) {
			ImGui::Text("Job ID: %s", jobId->c_str());
		}
	}

	ImGui::End();
}

void InjectorLayer::UI_ESPControls()
{
	ImGui::Begin("ESP Settings");

	ImGui::Checkbox("Enable ESP", &m_espEnabled);

	ImGui::Separator();

	ImGui::SliderFloat("Max Distance", &m_maxDistanceGui, 50.0f, 1000.0f, "%.0f studs");
	ImGui::SliderFloat("Max Delta Distance", &m_maxDeltaDistanceGui, 5.0f, 100.0f, "%.1f studs");

	ImGui::Separator();

	ImGui::ColorEdit4("Enemy Color", m_enemyColor);
	ImGui::ColorEdit4("Teammate Color", m_teammateColor);
	ImGui::ColorEdit4("Target Color", m_targetColor);

	ImGui::End();
}

void InjectorLayer::UI_AimbotControls()
{
	ImGui::Begin("Aimbot Settings");

	ImGui::Checkbox("Enable Aimbot", &m_aimbotEnabledGui);

	ImGui::Separator();
	ImGui::Text("Aim Style");
	ImGui::RadioButton("Silent (Direct)", &m_aimStyleGui, 0);
	ImGui::RadioButton("Legit (Smooth)", &m_aimStyleGui, 1);
	ImGui::RadioButton("Snap (Instant)", &m_aimStyleGui, 2);

	if (m_aimStyleGui == 1) { // Legit
		ImGui::SliderFloat("Smoothing", &m_smoothingGui, 1.0f, 20.0f, "%.1f");
	}

	ImGui::Separator();
	ImGui::Text("Aim Part");
	ImGui::RadioButton("Head", &m_aimPartGui, 0);
	ImGui::RadioButton("Torso", &m_aimPartGui, 1);
	ImGui::RadioButton("Custom", &m_aimPartGui, 2);

	ImGui::Separator();
	ImGui::Text("Aim Key");
	ImGui::RadioButton("Left Mouse", &m_aimKeyGui, 0);
	ImGui::RadioButton("Right Mouse", &m_aimKeyGui, 1);
	ImGui::RadioButton("E Key", &m_aimKeyGui, 2);

	ImGui::End();
}

void InjectorLayer::UI_PredictionControls()
{
	ImGui::Begin("Prediction Settings");

	ImGui::Checkbox("Enable Prediction", &m_predictionEnabledGui);

	ImGui::Separator();

	ImGui::SliderFloat("Bullet Speed", &m_bulletSpeedGui, 500.0f, 5000.0f, "%.0f studs/s");
	ImGui::Checkbox("Predict Gravity", &m_predictGravityGui);

	if (m_predictGravityGui) {
		ImGui::SliderFloat("Gravity", &m_gravityGui, 50.0f, 500.0f, "%.1f");
	}

	ImGui::End();
}

void InjectorLayer::UI_TargetSelectionControls()
{
	ImGui::Begin("Target Selection");

	ImGui::RadioButton("Closest to Crosshair", &m_targetSelectionGui, 0);
	ImGui::RadioButton("Closest Distance", &m_targetSelectionGui, 1);
	ImGui::RadioButton("Lowest Health", &m_targetSelectionGui, 2);
	ImGui::RadioButton("Closest to Mouse", &m_targetSelectionGui, 3);
	if (auto* pf = dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
		if (ImGui::Button("Team Switch"))
			pf->switch_teams();
	}

	ImGui::End();
}

void InjectorLayer::UI_DebugPanel()
{
	ImGui::Begin("Debug Information");

	if (m_espController) {
		ImGui::Text("Window: %.0fx%.0f",
			m_espController->window_width(),
			m_espController->window_height());
		ImGui::Text("Mouse: (%.0f, %.0f)",
			m_espController->mouse_x(),
			m_espController->mouse_y());
	}

	if (m_dataModel) {
		ImGui::Text("Game Valid: %s", m_dataModel->is_valid() ? "Yes" : "No");

		auto camera = m_dataModel->camera();
		if (camera) {
			ImGui::Text("Camera FOV: %.1f", camera.field_of_view());
		}
	}

	ImGui::End();
}

#endif
