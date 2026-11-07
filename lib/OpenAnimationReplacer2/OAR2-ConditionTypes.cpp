#include "OAR2-ConditionTypes.h"

// OAR 2.x counterpart of OpenAnimationReplacer-ConditionTypes.cpp / OpenAnimationReplacerAPI-Conditions.cpp
namespace OAR2
{
	namespace
	{
		IConditionsInterface2* api = nullptr;

		using RequestFunction = IConditionsInterface2* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);
	}

	IConditionsInterface2* GetAPI()
	{
		if (api) {
			return api;
		}
		const auto module = GetModuleHandleA("OpenAnimationReplacer.dll");
		const auto request = module ? reinterpret_cast<RequestFunction>(GetProcAddress(module, "RequestPluginAPI_Conditions")) : nullptr;
		if (request) {
			const auto plugin = SKSE::PluginDeclaration::GetSingleton();
			api = request(InterfaceVersion::V2, plugin->GetName().data(), plugin->GetVersion());
		}
		return api;
	}

	namespace Conditions
	{
		CustomCondition::CustomCondition()
		{
			const auto factory = api->GetWrappedConditionFactory();
			_wrappedCondition = std::unique_ptr<ICondition>(factory());
			_wrappedCondition->PreInitialize();
		}

		bool CustomCondition::Evaluate(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator* a_clipGenerator) const
		{
			if (IsDisabled()) {
				return true;
			}
			// do not call wrapped function's EvaluateImpl, but our own instead
			return IsNegated() ? !EvaluateImpl(a_refr, a_clipGenerator) : EvaluateImpl(a_refr, a_clipGenerator);
		}

		IConditionComponent* CustomCondition::AddBaseComponent(ConditionComponentType a_componentType, const char* a_name, const char* a_description /* = ""*/)
		{
			return AddComponent(api->GetConditionComponentFactory(a_componentType), a_name, a_description);
		}
	}
}
