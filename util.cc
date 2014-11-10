#include "util.hh"
#include <execinfo.h>

// for make_dir()
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

float noise(glm::vec2 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	FOR(i, octaves - 1)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

float noise(glm::vec3 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	FOR(i, octaves - 1)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

// ==============

void sigsegv_handler(int sig)
{
	fprintf(stderr, "Error: signal %d:\n", sig);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

void __assert_rtn(const char* func, const char* file, int line, const char* cond)
{
	fprintf(stderr, "Assertion: (%s), function %s, file %s, line %d.\n", cond, func, file, line);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

void __assert_rtn_format(const char* func, const char* file, int line, const char* cond, const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "Assertion: (%s), ", cond);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, ", function %s, file %s, line %d.\n", func, file, line);
	va_end(va);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

// =============

bool make_dir(const char* name)
{
	struct stat st = { 0 };
	if (stat(name, &st) == -1)
	{
		CHECK(mkdir(name, 0700) == 0);
	}
	return true;
}
