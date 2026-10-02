#include "casting/ShoutBlend.h"

#include <excpt.h>

namespace ShoutBlend
{
	namespace
	{
		// Havok 2010 (64 bit) layouts, checked against shout_behavior.hkx (packfiles load in place):
		constexpr std::size_t kNodeName = 0x38;            // hkbNode::name (hkStringPtr, lowest bit = ownership flag)
		constexpr std::size_t kBlenderChildren = 0x60;     // hkbBlenderGenerator::children (hkArray<hkbBlenderGeneratorChild*>)
		constexpr std::size_t kChildBoneWeights = 0x38;    // hkbBlenderGeneratorChild::boneWeights (hkbBoneWeightArray*)
		constexpr std::size_t kBoneWeightsArray = 0x30;    // hkbBoneWeightArray::boneWeights (hkArray<float>)
		constexpr std::size_t kReferencedBehavior = 0x50;  // hkbBehaviorReferenceGenerator::behavior (runtime)

		constexpr const char* kBlenderName = "MT_ShoutLocomotionBlend";

		// skeleton bone indices (meshes\actors\character\character assets\skeleton.hkx)
		constexpr std::array<int, 16> kArmBones{ 27, 28, 29, 30, 31, 32,  // clavicles, upper arms, forearms
			44, 45,                                                       // pauldrons
			52, 53, 54, 55, 56, 57, 58, 59 };                             // forearm / upper arm twists
		constexpr std::array<int, 3> kSpineBones{ 24, 25, 26 };
		constexpr int                kMinBones = 60;
		constexpr int                kMaxBones = 256;

		struct RawArray
		{
			void*         data;
			std::int32_t  size;
			std::int32_t  capacityAndFlags;
		};

		template <class T>
		T& At(void* a_object, std::size_t a_offset)
		{
			return *reinterpret_cast<T*>(static_cast<std::byte*>(a_object) + a_offset);
		}

		struct Vtables
		{
			std::uintptr_t blender;
			std::uintptr_t graph;
			std::uintptr_t reference;
		};

		constexpr int kMaxBlenders = 4;
		constexpr int kMaxGraphs = 32;

		struct Search
		{
			Vtables        vtables;
			void*          blenders[kMaxBlenders];
			int            blenderCount;
			void*          graphs[kMaxGraphs];
			int            graphCount;
		};

		bool IsBlenderName(void* a_node)
		{
			const auto raw = At<std::uintptr_t>(a_node, kNodeName) & ~std::uintptr_t{ 1 };
			return raw && std::strcmp(reinterpret_cast<const char*>(raw), kBlenderName) == 0;
		}

		// the player's active nodes, including referenced behaviors (shout_behavior is referenced from the master graph)
		void SearchGraph(Search& a_search, RE::hkbBehaviorGraph* a_graph, int a_depth)
		{
			if (!a_graph || a_depth > 6 || a_search.graphCount >= kMaxGraphs) {
				return;
			}
			for (int i = 0; i < a_search.graphCount; ++i) {
				if (a_search.graphs[i] == a_graph) {
					return;
				}
			}
			a_search.graphs[a_search.graphCount++] = a_graph;

			const auto nodes = a_graph->activeNodes;
			if (!nodes || nodes->size() <= 0 || nodes->size() > 20000) {
				return;
			}
			for (std::int32_t i = 0; i < nodes->size(); ++i) {
				const auto& info = nodes->data()[i];
				if (info.behavior && info.behavior != a_graph) {
					SearchGraph(a_search, info.behavior, a_depth + 1);
				}
				const auto node = info.nodeClone;
				if (!node) {
					continue;
				}
				const auto vtable = *reinterpret_cast<std::uintptr_t*>(node);
				if (vtable == a_search.vtables.blender) {
					if (a_search.blenderCount < kMaxBlenders && IsBlenderName(node)) {
						bool known = false;
						for (int b = 0; b < a_search.blenderCount; ++b) {
							known |= a_search.blenders[b] == node;
						}
						if (!known) {
							a_search.blenders[a_search.blenderCount++] = node;
						}
					}
				} else if (vtable == a_search.vtables.graph) {
					SearchGraph(a_search, reinterpret_cast<RE::hkbBehaviorGraph*>(node), a_depth + 1);
				} else if (vtable == a_search.vtables.reference) {
					SearchGraph(a_search, At<RE::hkbBehaviorGraph*>(node, kReferencedBehavior), a_depth + 1);
				}
			}
		}

		// SEH: the graph is read raw (and on other threads at the same time); a bad read must not take the game down
		bool SearchSafe(Search& a_search, RE::hkbBehaviorGraph* a_root)
		{
			__try {
				SearchGraph(a_search, a_root, 0);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		struct Weights
		{
			float*       data;
			std::int32_t size;
		};

		// the blender's two bone weight arrays: [0] the shout child, [1] the locomotion child
		bool ReadWeights(void* a_blender, Weights (&a_out)[2])
		{
			const auto& children = At<RawArray>(a_blender, kBlenderChildren);
			if (children.size != 2 || !children.data) {
				return false;
			}
			Weights found[2]{};
			for (int i = 0; i < 2; ++i) {
				const auto child = static_cast<void**>(children.data)[i];
				const auto boneWeights = child ? At<void*>(child, kChildBoneWeights) : nullptr;
				if (!boneWeights) {
					return false;
				}
				const auto& array = At<RawArray>(boneWeights, kBoneWeightsArray);
				if (!array.data || array.size < kMinBones || array.size > kMaxBones) {
					return false;
				}
				found[i] = { static_cast<float*>(array.data), array.size };
			}
			// the shout child takes nothing from the root, the locomotion child everything
			const bool firstIsShout = found[0].data[0] == 0.0f && found[1].data[0] == 1.0f;
			const bool secondIsShout = found[1].data[0] == 0.0f && found[0].data[0] == 1.0f;
			if (!firstIsShout && !secondIsShout) {
				return false;
			}
			a_out[0] = firstIsShout ? found[0] : found[1];
			a_out[1] = firstIsShout ? found[1] : found[0];
			return true;
		}

		bool ReadWeightsSafe(void* a_blender, Weights (&a_out)[2])
		{
			__try {
				return ReadWeights(a_blender, a_out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool WriteSafe(float* a_dst, const float* a_src, std::int32_t a_count)
		{
			__try {
				std::memcpy(a_dst, a_src, sizeof(float) * a_count);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		struct Patched
		{
			float*             data;
			std::vector<float> original;
		};
		std::vector<Patched>      patched;
		std::unordered_set<void*> handled;  // blenders seen during the running cast (patched or rejected)
		bool                      warned{ false };
		bool                      loggedPatch{ false };

		bool IsPatched(const float* a_data)
		{
			return std::ranges::any_of(patched, [&](const Patched& a_entry) { return a_entry.data == a_data; });
		}

		void Patch(void* a_blender)
		{
			Weights weights[2]{};
			if (!ReadWeightsSafe(a_blender, weights)) {
				if (!warned) {
					warned = true;
					logs::warn("Moving casts: MT_ShoutLocomotionBlend has an unexpected layout, left unchanged");
				}
				return;
			}
			if (IsPatched(weights[0].data) || IsPatched(weights[1].data)) {
				return;  // shared with a blender handled already
			}
			std::vector<float> shout(weights[0].data, weights[0].data + weights[0].size);
			std::vector<float> locomotion(weights[1].data, weights[1].data + weights[1].size);
			// only the vanilla split (half / half), anything else is a behavior mod's own blend
			const bool vanilla = std::ranges::all_of(kArmBones, [&](int a_bone) { return shout[a_bone] == 0.5f && locomotion[a_bone] == 0.5f; }) &&
			                     std::ranges::all_of(kSpineBones, [&](int a_bone) { return shout[a_bone] == 0.5f && locomotion[a_bone] == 0.5f; });
			if (!vanilla) {
				if (!warned) {
					warned = true;
					logs::warn("Moving casts: MT_ShoutLocomotionBlend weights aren't the vanilla ones, left unchanged");
				}
				return;
			}
			auto newShout = shout;
			auto newLocomotion = locomotion;
			for (const int bone : kArmBones) {
				newShout[bone] = 1.0f;
				newLocomotion[bone] = 0.0f;
			}
			for (const int bone : kSpineBones) {
				newShout[bone] = 0.0f;
				newLocomotion[bone] = 1.0f;
			}
			if (!WriteSafe(weights[0].data, newShout.data(), weights[0].size) ||
				!WriteSafe(weights[1].data, newLocomotion.data(), weights[1].size)) {
				WriteSafe(weights[0].data, shout.data(), weights[0].size);
				return;
			}
			patched.push_back({ weights[0].data, std::move(shout) });
			patched.push_back({ weights[1].data, std::move(locomotion) });
			if (!loggedPatch) {
				loggedPatch = true;
				logs::info("Moving casts: arms taken from the casting animation while the hotbar cast runs");
			}
		}
	}

	void Update(bool a_active)
	{
		if (!a_active) {
			Restore();
			return;
		}
		if (!patched.empty()) {
			return;  // done for this cast
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
		if (!player || !player->GetAnimationGraphManager(manager) || !manager) {
			return;
		}
		static const Vtables vtables{
			RE::VTABLE_hkbBlenderGenerator[0].address(),
			RE::VTABLE_hkbBehaviorGraph[0].address(),
			RE::VTABLE_hkbBehaviorReferenceGenerator[0].address(),
		};
		for (const auto& graph : manager->graphs) {
			if (!graph || !graph->behaviorGraph) {
				continue;
			}
			Search search{ .vtables = vtables };
			if (!SearchSafe(search, graph->behaviorGraph)) {
				continue;
			}
			for (int i = 0; i < search.blenderCount; ++i) {
				if (handled.insert(search.blenders[i]).second) {
					Patch(search.blenders[i]);
				}
			}
		}
	}

	void Restore()
	{
		for (const auto& entry : patched) {
			WriteSafe(entry.data, entry.original.data(), static_cast<std::int32_t>(entry.original.size()));
		}
		patched.clear();
		handled.clear();
	}
}
