#pragma once

#include "sys/syscall.h"
#include <execinfo.h>
#include <cxxabi.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

class FunctionName
{
public:
	FunctionName(void* address)
	{
		m_lines = backtrace_symbols(&address, 1);
		// Syntax: PATH/BINARY(SYMBOL+-OFFSET)[ABSOLUTE]
		char* binary = strrchr(m_lines[0], '/');
		char* mfunction = binary ? strchr(binary, '(') : nullptr;
		char* offset = mfunction ? strchr(mfunction, '+') : nullptr;
		offset = (mfunction && !offset) ? strchr(mfunction, '-') : offset;
		char* end = offset ? strchr(offset, ')') : nullptr;

		if (!end)
		{
			// The line doesn't have the syntax we expect. This happens
			// for signal hanglers. In this case just put the original format
			//
			m_binary = nullptr;
			m_function = nullptr;
			m_offset = 0;
			m_dfunction = nullptr;
		}
		else
		{
			m_binary = binary + 1;
			*mfunction++ = 0;
			*end = 0;
			m_offset = 0;
			sscanf(offset, "%llx", &m_offset);
			*offset++ = 0;
			m_dfunction = abi::__cxa_demangle(mfunction, nullptr, nullptr, nullptr);
			m_function = m_dfunction ? m_dfunction : mfunction;
		}
	}

	~FunctionName()
	{
		free(m_lines);
		if (m_dfunction) free(m_dfunction);
	}

	bool IsResolved() { return m_function != nullptr; }
	const char* Encoded() { return m_lines[0]; }

	const char* Binary() { return m_binary; }
	const char* Function() { return m_function; }
	uint64_t Offset() { return m_offset; }

private:
	const char* m_binary;
	const char* m_function;
	uint64_t m_offset;

	char** m_lines; // allocated by backtrace_symbols
	char* m_dfunction; // allocated by abi::__cxa_demangle
};

inline void PrintCallStack()
{
	void* stack[20];
	int size = backtrace(stack, 20);	
	for (int i = 0; i < size; i++)
	{
		FunctionName fn(stack[i]);
		if (!fn.IsResolved())
		{
			fprintf(stderr, "%s\n", fn.Encoded());
		}
		else
		{
			fprintf(stderr, "[%s] %s 0x%llX\n", fn.Binary(), fn.Function(), fn.Offset());
		}
	}
}
