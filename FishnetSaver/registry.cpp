#include <Windows.h>
#include <string>
#include <exception>
#include <stdexcept>
#include "registry.h"

// This is code from:
// https://docs.microsoft.com/en-us/archive/msdn-magazine/2017/may/c-use-modern-c-to-access-the-windows-registry
// Use Modern C++ to Access the Windows Registry by Giovanni Dicanio

/////////////////////////////////////////////////////////////////////////////////////////
//
// Modern C++ Wrappers for Win32 Registry Access APIs
// 
// Copyright (C) 2017 by Giovanni Dicanio <giovanni.dicanio AT gmail.com>
// 
// Compiler: Visual Studio 2015
// Compiles cleanly at /W4 in both 32-bit and 64-bit builds.
// 
// -----------------------------------------------------------------------------------
// 
// The MIT License(MIT)
//
// Copyright(c) 2017 Giovanni Dicanio
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
/////////////////////////////////////////////////////////////////////////////////////////

DWORD
RegGetDword(HKEY hKey, const std::wstring& subKey, const std::wstring& value)
{
	DWORD data{}, dataSize = sizeof(data);
	LONG retCode = ::RegGetValue(hKey, subKey.c_str(), value.c_str(), RRF_RT_REG_DWORD, nullptr, &data, &dataSize);
	if (retCode != ERROR_SUCCESS)
		throw RegistryError{ "Cannot read DWORD from registry", retCode };
	return data;
}

std::wstring
RegGetString(
	HKEY hKey,
	const std::wstring& subKey,
	const std::wstring& value)
{
	DWORD dataSize{};
	LONG retCode = ::RegGetValue(hKey, subKey.c_str(), value.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &dataSize);
	if (retCode != ERROR_SUCCESS)
		throw RegistryError{ "Cannot read string from registry", retCode };

	std::wstring data;
	data.resize(dataSize / sizeof(wchar_t));

	retCode = ::RegGetValue(hKey, subKey.c_str(), value.c_str(), RRF_RT_REG_SZ, nullptr, &data[0], &dataSize);
	if (retCode != ERROR_SUCCESS)
		throw RegistryError{ "Cannot read string from registry", retCode };

	DWORD stringLengthInWchars = dataSize / sizeof(wchar_t);
	stringLengthInWchars--; // exclude the NUL written by Win32
	data.resize(stringLengthInWchars);

	return data;
}
