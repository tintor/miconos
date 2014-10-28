#include "util.hh"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

void array_file_init(void*& map, int& fd)
{
	map = MAP_FAILED;
	fd = -1;
}

bool array_file_open(const char* prefix, glm::ivec3 pos, const char* suffix, void*& map, int& fd, size_t size)
{
	char file_name[1024];
	CHECK(snprintf(file_name, sizeof(file_name), "%s.%+d%+d%+d.%s", prefix, pos.x, pos.y, pos.z, suffix) < sizeof(file_name));

	fd = open(file_name, O_RDWR | O_CREAT, 0666);
	CHECK(fd != -1);

	off_t end = lseek(fd, 0, SEEK_END);
	CHECK(end != -1);
	CHECK(end == 0 || end == size);

	CHECK(ftruncate(fd, size) != -1);

	map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, fd, 0);
	CHECK(map != MAP_FAILED);

	if (end == 0)
	{
		char* q = (char*) map;
		FOR(i, size) assert(q[i] == 0);
	}
	return true;
}

void array_file_close(void*& map, int& fd, size_t size)
{
	if (map != MAP_FAILED)
	{
		munmap(map, size);
		map = MAP_FAILED;
	}
	if (fd != -1)
	{
		close(fd);
		fd = -1;
	}
}

void array_file_save(int fd)
{
	 fsync(fd);
}

// ==============================

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
