#include "casting/GraphWatch.h"

namespace GraphWatch
{
	namespace
	{
		constexpr float kTail = 1.0f;    // seconds of watching after the cast animation ended
		constexpr int   kMaxGraphs = 4;  // 3rd and 1st person

		using Clock = std::chrono::steady_clock;

		std::atomic<bool>                                watching{ false };  // a cast animation runs (+ kTail)
		std::atomic<bool>                                leftShout{ false };
		std::atomic<Clock::rep>                          start{ 0 };
		std::array<std::atomic<const void*>, kMaxGraphs> sources{};  // the player's graphs, for the graph number in the log
		float                                            tail{ 0.0f };

		float Elapsed()
		{
			return std::chrono::duration<float>(Clock::duration(Clock::now().time_since_epoch().count() - start.load())).count();
		}

		bool Is(std::string_view a_tag, const char* a_name)
		{
			return _stricmp(a_tag.data(), a_name) == 0;
		}

		bool StartsWith(std::string_view a_tag, std::string_view a_prefix)
		{
			return a_tag.size() >= a_prefix.size() && _strnicmp(a_tag.data(), a_prefix.data(), a_prefix.size()) == 0;
		}

		// footsteps and sounds come every few frames while moving
		bool Noise(std::string_view a_tag)
		{
			return StartsWith(a_tag, "Foot"sv) || StartsWith(a_tag, "SoundPlay"sv) || StartsWith(a_tag, "SoundStop"sv);
		}

		int GraphNumber(const void* a_source)
		{
			for (int i = 0; i < kMaxGraphs; ++i) {
				if (sources[i].load() == a_source) {
					return i;
				}
			}
			return -1;
		}

		// Called on the threads that update the graphs: only atomics and logging here
		class Sink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
			{
				if (!a_event || !watching.load() || !a_event->tag.c_str()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view tag = a_event->tag.c_str();
				if (Is(tag, "HeadTrackingOn")) {
					leftShout = true;
				}
				if (!Noise(tag)) {
					const std::string_view payload = a_event->payload.c_str() ? a_event->payload.c_str() : "";
					logs::debug("[anim] {:5.2f}s graph {}: {}{}{}", Elapsed(), GraphNumber(a_source), tag, payload.empty() ? "" : " ", payload);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		Sink sink;

		void Register()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			if (!player || !player->GetAnimationGraphManager(manager) || !manager) {
				return;
			}
			int i = 0;
			for (const auto& graph : manager->graphs) {
				if (!graph) {
					continue;
				}
				const auto source = static_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(graph.get());
				source->AddEventSink(&sink);  // no-op if already there
				if (i < kMaxGraphs) {
					sources[i++] = source;
				}
			}
		}
	}

	void Begin(std::string_view a_what)
	{
		Register();
		start = Clock::now().time_since_epoch().count();
		leftShout = false;
		tail = kTail;
		watching = true;
		Note(a_what);
	}

	void Note(std::string_view a_what)
	{
		if (watching.load()) {
			logs::debug("[anim] {:5.2f}s hotbar: {}", Elapsed(), a_what);
		}
	}

	void Update(float a_delta, bool a_casting)
	{
		if (a_casting) {
			tail = kTail;
			return;
		}
		if (watching.load()) {
			tail -= a_delta;
			if (tail <= 0.0f) {
				logs::debug("[anim] {:5.2f}s end of log", Elapsed());
				watching = false;
			}
		}
	}

	bool TakeLeftShout()
	{
		return leftShout.exchange(false);
	}
}
