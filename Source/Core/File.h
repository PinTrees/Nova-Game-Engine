#pragma once

class File
{
public:
	static string GetExtension(const string& fullPath);
	static string GetFilePath_NoneExtension(const string& fullPath);
};

