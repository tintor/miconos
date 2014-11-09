#pragma once

typedef std::pair<const char*, int> Token;

inline bool operator==(Token b, const char* a)
{
	return strlen(a) == b.second && memcmp(a, b.first, b.second) == 0;
}

inline bool is_integer(Token a)
{
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
	}
	if (a.second > 9) return false;
	FOR(i, a.second) if (!isdigit(a.first[i])) return false;
	return true;
}

inline int parse_int(Token a)
{
	bool negative = false;
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
		negative = true;
	}
	int v = 0;
	FOR(i, a.second) v = v * 10 + a.first[i] - '0';
	return negative ? -v : v;
}

inline bool is_real(Token a)
{
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
	}
	FOR(i, a.second) if (!isdigit(a.first[i]) && a.first[i] != '.') return false;
	int dots = 0;
	FOR(i, a.second) if (a.first[i] == '.') dots += 1;
	return dots <= 1;
}

inline double parse_real(Token a)
{
	bool negative = false;
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
		negative = true;
	}
	double v = 0;
	double e = 0.1;
	bool dot = false;
	FOR(i, a.second)
	{
		if (a.first[i] == '.')
		{
			dot = true;
		}
		else if (!dot)
		{
			v = v * 10 + (a.first[i] - '0');
		}
		else
		{
			v += e * (a.first[i] - '0');
			e /= 10;
		}
	}
	return negative ? -v : v;
}

// TODO: support ' and "
inline void tokenize(const char* text, int length, std::vector<Token>& tokens)
{
	FOR(i, length)
	{
		if (text[i] == ' ') continue;
		if (i == 0 || text[i-1] == ' ') tokens.push_back({ text + i, 1 }); else tokens.back().second += 1;
	}
}
