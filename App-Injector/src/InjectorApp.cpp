#ifdef WL_HEADLESS
#include "Walnut/Application.h"
#else
#include "Walnut/ApplicationGUI.h"
#endif
#include "Walnut/EntryPoint.h"

#include "InjectorLayer.h"

#include <csignal>

InjectorLayer* g_InjectorLayerCopyRaw{};

Walnut::Application* Walnut::CreateApplication(int argc, char** argv)
{
	Walnut::ApplicationSpecification spec;
	spec.Name = "RobloxExternal-macOS (Injector)";
#ifndef WL_HEADLESS
	spec.IconPath = "res/Walnut-Icon.png";
	spec.CustomTitlebar = true;
	spec.CenterWindow = true;
	spec.Width = 800;
	spec.Height = 600;
#endif

	Walnut::Application* app = new Walnut::Application(spec);
	std::shared_ptr<InjectorLayer> injectorLayer = std::make_shared<InjectorLayer>();
	app->PushLayer(injectorLayer);

#ifndef WL_HEADLESS
	app->SetMenubarCallback([app, injectorLayer]()
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit"))
				app->Close();

			if (ImGui::MenuItem("Reload Dylib"))
				injectorLayer->ReloadDylib();

			ImGui::EndMenu();
		}
	});
#endif

	// TODO(Roulette): What the fucking hack. Redo this in a better and safer way.
	g_InjectorLayerCopyRaw = injectorLayer.get();
	auto signal_handler = [](int sig)
	{
		std::println("\nSignal {} received, cleaning up...", sig);
		g_InjectorLayerCopyRaw->Shutdown();
		std::exit(0);
	};
	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);

	return app;
}