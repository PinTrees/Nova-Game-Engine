#include "pch.h"
#include "File.h"

string File::GetExtension(const string& fullPath)
{
	size_t dotPos = fullPath.find_last_of(L'.');
	if (dotPos != string::npos)
	{
		return fullPath.substr(dotPos + 1);
	}
	return "";
}

string File::GetFilePath_NoneExtension(const string& fullPath)
{ 
	size_t dotPos = fullPath.find_last_of(L'.');
	if (dotPos != string::npos)
	{
		return fullPath.substr(dotPos + 1);
	}
	return "";
}
