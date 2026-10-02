#include "casting/PlayerControl.h"

#include "core/Config.h"

namespace PlayerControl
{
	namespace
	{
		constexpr float kAimDistance = 10000.0f;  // game units the crosshair ray reaches
		constexpr float kTwoPi = 6.2831853f;

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		float NormalizeAngle(float a_angle)
		{
			a_angle = std::fmod(a_angle, kTwoPi);
			return a_angle < 0.0f ? a_angle + kTwoPi : a_angle;
		}

		RE::ThirdPersonState* ThirdPerson()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->IsInThirdPerson()) {
				return nullptr;
			}
			return static_cast<RE::ThirdPersonState*>(camera->currentState.get());
		}

		// What the crosshair points at: the first thing the camera ray hits, or a point far away
		std::optional<RE::NiPoint3> CrosshairPoint()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			const auto player = Player();
			if (!camera || !camera->cameraRoot || !player) {
				return std::nullopt;
			}
			// the camera looks along the player's pitch; in third person its yaw is the player's plus the free rotation
			const auto         third = ThirdPerson();
			const float        yaw = player->GetAngleZ() + (third ? third->freeRotation.x : 0.0f);
			const float        pitch = player->GetAngleX();
			const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 from = camera->cameraRoot->world.translate;
			RE::NiPoint3       point = from + forward * kAimDistance;

			const auto cell = player->GetParentCell();
			const auto world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return point;
			}
			const float     scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(point.x * scale, point.y * scale, point.z * scale, 0.0f);
			RE::CFilter filter{};
			player->GetCollisionFilterInfo(filter);  // the player's own group: the ray passes through the character
			pick.rayInput.filterInfo.filter = (filter.filter & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			if (world->PickObject(pick) && pick.rayOutput.HasHit()) {
				const float distance = kAimDistance * pick.rayOutput.hitFraction;
				// third person: the ray starts behind the character, hits before it are the camera's own surroundings
				const float minimum = third ? from.GetDistance(player->GetPosition()) * 0.5f : 0.0f;
				if (distance > minimum) {
					point = from + forward * distance;
				}
			}
			return point;
		}
	}

	void StopSprinting()
	{
		const auto player = Player();
		if (!player || !player->AsActorState()->IsSprinting()) {
			return;
		}
		const auto manager = RE::BGSDefaultObjectManager::GetSingleton();
		const auto action = manager ? manager->GetObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionSprintStop) : nullptr;
		const auto data = action ? RE::TESActionData::Create() : nullptr;
		if (!data) {
			return;
		}
		data->source = RE::NiPointer<RE::TESObjectREFR>(player);
		data->action = action;
		data->Process();
		data->~TESActionData();  // game heap object: destroy, then free with the game's allocator
		RE::free(data);
	}

	bool AimsAtCrosshair(const RE::MagicItem* a_item)
	{
		if (!a_item || !Config::Get().aimAtCrosshair) {
			return false;
		}
		const auto delivery = a_item->GetDelivery();
		return delivery == RE::MagicSystem::Delivery::kAimed || delivery == RE::MagicSystem::Delivery::kTargetLocation;
	}

	void FaceCamera(float a_maxStep)
	{
		const auto third = ThirdPerson();
		const auto player = Player();
		if (!third || !player) {
			return;
		}
		const float offset = third->freeRotation.x;
		if (std::abs(offset) < 0.0001f) {
			return;
		}
		const float step = a_maxStep < 0.0f ? offset : std::clamp(offset, -a_maxStep, a_maxStep);
		player->SetHeading(NormalizeAngle(player->GetAngleZ() + step));
		third->freeRotation.x = offset - step;
	}

	ScopedCrosshairAim::ScopedCrosshairAim(RE::MagicCaster* a_caster)
	{
		const auto player = Player();
		const auto point = CrosshairPoint();
		if (!player || !point) {
			return;
		}
		RE::NiPoint3 origin = player->GetPosition();
		if (const auto node = a_caster ? a_caster->GetMagicNode() : nullptr) {
			origin = node->world.translate;
		} else {
			origin.z += (player->GetBoundMax().z - player->GetBoundMin().z) * 0.7f;
		}
		const auto  d = *point - origin;
		const float horizontal = std::sqrt(d.x * d.x + d.y * d.y);
		if (horizontal < 1.0f) {
			return;
		}
		_saved = player->data.angle;
		player->data.angle.x = -std::atan2(d.z, horizontal);
		player->data.angle.z = NormalizeAngle(std::atan2(d.x, d.y));
	}

	ScopedCrosshairAim::~ScopedCrosshairAim()
	{
		if (const auto player = Player(); player && _saved) {
			player->data.angle.x = _saved->x;
			player->data.angle.z = _saved->z;
		}
	}
}
