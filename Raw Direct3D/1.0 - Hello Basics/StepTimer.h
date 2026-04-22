class StepTimer
{
	LARGE_INTEGER m_qpc_frequency;
	LARGE_INTEGER m_qpc_last_time;
	UINT64 m_qpc_max_delta;

	UINT64 m_elapsed_ticks;
	UINT64 m_total_ticks;
	UINT64 m_left_over_ticks;

	UINT32 m_frame_count;
	UINT32 m_frames_per_second;
	UINT32 m_frames_this_second;
	UINT64 m_qpc_second_counter;

	bool m_is_fixed_time_step;
	UINT64 m_target_elapsed_ticks;

public:	
	static const UINT64 ticks_per_second = 10'000'000;
	static double TicksToSeconds(UINT64 ticks) { return static_cast<double>(ticks) / ticks_per_second; }
	static UINT64 SecondsToTicks(double seconds) { return static_cast<UINT64>(seconds * ticks_per_second); }

	void SetTargetElapsedSeconds(double target_elapsed) { m_target_elapsed_ticks = SecondsToTicks(target_elapsed); }
	void SetTargetElapsedTicks(UINT64 target_elapsed) { m_target_elapsed_ticks = target_elapsed; }

	void SetFixedTimeStep(bool  is_fixed_timestep) { m_is_fixed_time_step = is_fixed_timestep; }
	
	UINT32 GetFramesPerSecond() const { return m_frames_per_second; }
	UINT32 GetFrameCount() const { return m_frame_count; }

	double GetTotalSeconds() const { return TicksToSeconds(m_total_ticks); }
	UINT64 GetTotalTicks() const { return m_total_ticks; }

	double GetEllapsedSeconds() const { return TicksToSeconds(m_elapsed_ticks); }
	UINT64 GetEllapsedTicks() const { return m_elapsed_ticks; }

	StepTimer() :
		m_elapsed_ticks(0),
		m_total_ticks(0),
		m_left_over_ticks(0),
		m_frame_count(0),
		m_frames_per_second(0),
		m_frames_this_second(0),
		m_qpc_second_counter(0),
		m_is_fixed_time_step(false),
		m_target_elapsed_ticks(ticks_per_second / 60)
	{
		QueryPerformanceFrequency(&m_qpc_frequency);
		QueryPerformanceCounter(&m_qpc_last_time);

		m_qpc_max_delta = m_qpc_frequency.QuadPart / 10;
	}

	typedef void(*LUPDATEFUNC)(void);
	void Tick(LUPDATEFUNC update = nullptr)
	{
		LARGE_INTEGER current_time;

		QueryPerformanceCounter(&current_time);

		UINT64 time_delta{ static_cast<UINT64>(current_time.QuadPart - m_qpc_last_time.QuadPart) };
		m_qpc_last_time = current_time;
		m_qpc_second_counter += time_delta;

		if (time_delta > m_qpc_max_delta)
			time_delta = m_qpc_max_delta;

		time_delta *= ticks_per_second;
		time_delta /= m_qpc_frequency.QuadPart;

		UINT32 last_frame_count = m_frame_count;

		if (m_is_fixed_time_step)
		{
			//	Fixed timestep update logic
			//	This prevents tiny and irrelevant errors from accumulating over time.
			//	Without this campling, a game that requests 60 fps fixed update running
			//	with vsync enabled on a 59.94 NTSC diplay would eventually accumulate
			//	enough tiny errors that it would drop a frame. It is better to just round
			//	small deviations down to zero to leave things running smoothly.
			if (abs(static_cast<int>(time_delta - m_target_elapsed_ticks) < ticks_per_second / 4000))
			{
				time_delta = m_target_elapsed_ticks;
			}
			m_left_over_ticks += time_delta;

			while (m_left_over_ticks >= m_target_elapsed_ticks)
			{
				m_elapsed_ticks = m_target_elapsed_ticks;
				m_total_ticks += m_target_elapsed_ticks;
				m_left_over_ticks -= m_elapsed_ticks;
				m_frame_count++;
			}

			if (update)
			{
				update();
			}
		}
		else {
			//	Variable timestep update logic
			m_elapsed_ticks = time_delta;
			m_total_ticks += time_delta;
			m_left_over_ticks = 0;
			m_frame_count++;

			if (update)
			{
				update();
			}
		}

		if (m_frame_count != last_frame_count)
		{
			m_frames_this_second++;
		}

		if (m_qpc_second_counter >= static_cast<UINT64>(m_qpc_frequency.QuadPart))
		{
			m_frames_per_second = m_frames_this_second;
			m_frames_this_second = 0;
			m_qpc_second_counter %= m_qpc_frequency.QuadPart;
		}
	}
};