#include "casting/TrueDirectionalMovement.h"

#include "TrueDirectionalMovementAPI.h"

namespace TrueDirectionalMovement
{
	namespace
	{
		TDM_API::IVTDM1* api{ nullptr };
		bool             holding{ false };  // TDM's directional movement is off for us
		bool             refusedLogged{ false };
	}

	void Init()
	{
		api = static_cast<TDM_API::IVTDM1*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V1));
		if (api) {
			logs::info("True Directional Movement found: hotbar casts aiming at the crosshair turn its directional movement off");
		}
	}

	void FaceCrosshair(bool a_face)
	{
		if (!api || a_face == holding) {
			return;
		}
		const auto handle = SKSE::GetPluginHandle();
		if (!a_face) {
			api->ReleaseDisableDirectionalMovement(handle);
			holding = false;
			return;
		}
		// another mod may hold it, or TDM needs it right now (e.g. its own target lock): tried again every frame
		const auto result = api->RequestDisableDirectionalMovement(handle);
		holding = result == TDM_API::APIResult::OK || result == TDM_API::APIResult::AlreadyGiven;
		if (holding) {
			refusedLogged = false;
		} else if (!refusedLogged) {
			refusedLogged = true;
			logs::info("True Directional Movement kept its directional movement for a hotbar cast (result {})", static_cast<int>(result));
		}
	}
}
