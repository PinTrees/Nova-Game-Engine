#include "pch.h"
#include "Debug.h"
#include "MissingComponent.h"
#include "PackageManager.h"
#include "UnityGUI.h"

void MissingComponent::OnInspectorGUI()
{
	const std::string pkg = PackageManager::PackageForComponent(m_MissingType);
	std::string text = "The component '" + m_MissingType + "' is not available.";
	if (!pkg.empty())
	{
		const PackageInfo* info = PackageManager::Find(pkg);
		text += " It comes from the package '" + (info ? info->DisplayName : pkg) + "' (" + pkg + ").";
		const std::string error = PackageManager::LoadError(pkg);
		if (!error.empty())
			text += " The package failed to load: " + error;
		else if (!PackageManager::IsInProject(pkg))
			text += " Install it from Window > Package Manager.";
	}
	text += " Its data is kept and saved as is.";
	UnityGUI::HelpBox(text.c_str(), true);
	if (!pkg.empty() && !PackageManager::IsInProject(pkg))
	{
		if (ImGui::Button(("Install " + pkg).c_str()))
		{
			std::string error;
			if (!PackageManager::Add(pkg, error))
				Debug::LogError(("Package Manager: " + error).c_str());
		}
	}
}
