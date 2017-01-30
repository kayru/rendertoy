#pragma once

#include <functional>

namespace FileWatcher {
	typedef std::function<void()> Callback;

	void watchFile(const char* const path, const Callback& callback);
	void update();
	void start();
	void stop();
}
