#pragma once
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "EditorGUI.h"

using json = nlohmann::json;

class MonoBehaviour;

enum class ScriptFieldType
{
	Int,
	Float,
	Bool,
	String,
	Vector2,
	Vector3,
	Vector4
};

class IScriptField
{
public:
	virtual ~IScriptField() = default;
	virtual const std::string& GetName() const = 0;
	virtual ScriptFieldType GetFieldType() const = 0;
	virtual void Serialize(json& j) const = 0;
	virtual void Deserialize(const json& j) = 0;
	virtual bool OnInspectorGUI() = 0;
};

// Forward declaration of registration
void RegisterMonoBehaviourField(MonoBehaviour* owner, IScriptField* field);

// -------------------------------------------------------------
// Field<T>: Smart serialized property wrapper with auto-reflection
// -------------------------------------------------------------
template <typename T>
class Field : public IScriptField
{
private:
	std::string m_Name;
	T m_Value;
	MonoBehaviour* m_Owner = nullptr;

public:
	Field(MonoBehaviour* owner, const std::string& name, const T& defaultValue = T())
		: m_Name(name), m_Value(defaultValue), m_Owner(owner)
	{
		RegisterMonoBehaviourField(owner, this);
	}

	operator T&() { return m_Value; }
	operator const T&() const { return m_Value; }

	Field& operator=(const T& val)
	{
		m_Value = val;
		return *this;
	}

	T* operator->() { return &m_Value; }
	const T* operator->() const { return &m_Value; }

	T& Get() { return m_Value; }
	const T& Get() const { return m_Value; }

	const std::string& GetName() const override { return m_Name; }
	ScriptFieldType GetFieldType() const override;

	void Serialize(json& j) const override;
	void Deserialize(const json& j) override;
	bool OnInspectorGUI() override;
};

// -------------------------------------------------------------
// Specializations for Float
// -------------------------------------------------------------
template <>
inline ScriptFieldType Field<float>::GetFieldType() const { return ScriptFieldType::Float; }

template <>
inline void Field<float>::Serialize(json& j) const { j[m_Name] = m_Value; }

template <>
inline void Field<float>::Deserialize(const json& j)
{
	if (j.contains(m_Name) && j[m_Name].is_number())
		m_Value = j[m_Name].get<float>();
}

template <>
inline bool Field<float>::OnInspectorGUI()
{
	return EditorGUI::FloatField(m_Name, m_Value);
}

// -------------------------------------------------------------
// Specializations for Int
// -------------------------------------------------------------
template <>
inline ScriptFieldType Field<int>::GetFieldType() const { return ScriptFieldType::Int; }

template <>
inline void Field<int>::Serialize(json& j) const { j[m_Name] = m_Value; }

template <>
inline void Field<int>::Deserialize(const json& j)
{
	if (j.contains(m_Name) && j[m_Name].is_number_integer())
		m_Value = j[m_Name].get<int>();
}

template <>
inline bool Field<int>::OnInspectorGUI()
{
	float fVal = static_cast<float>(m_Value);
	if (EditorGUI::FloatField(m_Name, fVal))
	{
		m_Value = static_cast<int>(fVal);
		return true;
	}
	return false;
}

// -------------------------------------------------------------
// Specializations for Bool
// -------------------------------------------------------------
template <>
inline ScriptFieldType Field<bool>::GetFieldType() const { return ScriptFieldType::Bool; }

template <>
inline void Field<bool>::Serialize(json& j) const { j[m_Name] = m_Value; }

template <>
inline void Field<bool>::Deserialize(const json& j)
{
	if (j.contains(m_Name) && j[m_Name].is_boolean())
		m_Value = j[m_Name].get<bool>();
}

template <>
inline bool Field<bool>::OnInspectorGUI()
{
	return EditorGUI::BoolField(m_Name, m_Value);
}

// -------------------------------------------------------------
// Specializations for String
// -------------------------------------------------------------
template <>
inline ScriptFieldType Field<std::string>::GetFieldType() const { return ScriptFieldType::String; }

template <>
inline void Field<std::string>::Serialize(json& j) const { j[m_Name] = m_Value; }

template <>
inline void Field<std::string>::Deserialize(const json& j)
{
	if (j.contains(m_Name) && j[m_Name].is_string())
		m_Value = j[m_Name].get<std::string>();
}

template <>
inline bool Field<std::string>::OnInspectorGUI()
{
	EditorGUI::Label(m_Name);
	ImGui::SameLine();
	return EditorGUI::InputField(m_Value);
}

// -------------------------------------------------------------
// Specializations for Vec3
// -------------------------------------------------------------
template <>
inline ScriptFieldType Field<Vec3>::GetFieldType() const { return ScriptFieldType::Vector3; }

template <>
inline void Field<Vec3>::Serialize(json& j) const
{
	j[m_Name] = { m_Value.x, m_Value.y, m_Value.z };
}

template <>
inline void Field<Vec3>::Deserialize(const json& j)
{
	if (j.contains(m_Name) && j[m_Name].is_array() && j[m_Name].size() == 3)
	{
		m_Value.x = j[m_Name][0].get<float>();
		m_Value.y = j[m_Name][1].get<float>();
		m_Value.z = j[m_Name][2].get<float>();
	}
}

template <>
inline bool Field<Vec3>::OnInspectorGUI()
{
	return EditorGUI::Vector3Field(m_Name, m_Value);
}

// -------------------------------------------------------------
// Raw Variable Field Adapter (for macro registration)
// -------------------------------------------------------------
template <typename T>
class RawScriptField : public IScriptField
{
private:
	std::string m_Name;
	T* m_Ptr;

public:
	RawScriptField(const std::string& name, T* ptr) : m_Name(name), m_Ptr(ptr) {}

	const std::string& GetName() const override { return m_Name; }
	ScriptFieldType GetFieldType() const override;

	void Serialize(json& j) const override;
	void Deserialize(const json& j) override;
	bool OnInspectorGUI() override;
};

template <>
inline ScriptFieldType RawScriptField<float>::GetFieldType() const { return ScriptFieldType::Float; }
template <>
inline void RawScriptField<float>::Serialize(json& j) const { if (m_Ptr) j[m_Name] = *m_Ptr; }
template <>
inline void RawScriptField<float>::Deserialize(const json& j)
{
	if (m_Ptr && j.contains(m_Name) && j[m_Name].is_number())
		*m_Ptr = j[m_Name].get<float>();
}
template <>
inline bool RawScriptField<float>::OnInspectorGUI()
{
	return m_Ptr ? EditorGUI::FloatField(m_Name, *m_Ptr) : false;
}

template <>
inline ScriptFieldType RawScriptField<int>::GetFieldType() const { return ScriptFieldType::Int; }
template <>
inline void RawScriptField<int>::Serialize(json& j) const { if (m_Ptr) j[m_Name] = *m_Ptr; }
template <>
inline void RawScriptField<int>::Deserialize(const json& j)
{
	if (m_Ptr && j.contains(m_Name) && j[m_Name].is_number_integer())
		*m_Ptr = j[m_Name].get<int>();
}
template <>
inline bool RawScriptField<int>::OnInspectorGUI()
{
	if (!m_Ptr) return false;
	float fVal = static_cast<float>(*m_Ptr);
	if (EditorGUI::FloatField(m_Name, fVal))
	{
		*m_Ptr = static_cast<int>(fVal);
		return true;
	}
	return false;
}

template <>
inline ScriptFieldType RawScriptField<bool>::GetFieldType() const { return ScriptFieldType::Bool; }
template <>
inline void RawScriptField<bool>::Serialize(json& j) const { if (m_Ptr) j[m_Name] = *m_Ptr; }
template <>
inline void RawScriptField<bool>::Deserialize(const json& j)
{
	if (m_Ptr && j.contains(m_Name) && j[m_Name].is_boolean())
		*m_Ptr = j[m_Name].get<bool>();
}
template <>
inline bool RawScriptField<bool>::OnInspectorGUI()
{
	return m_Ptr ? EditorGUI::BoolField(m_Name, *m_Ptr) : false;
}

template <>
inline ScriptFieldType RawScriptField<Vec3>::GetFieldType() const { return ScriptFieldType::Vector3; }
template <>
inline void RawScriptField<Vec3>::Serialize(json& j) const
{
	if (m_Ptr) j[m_Name] = { m_Ptr->x, m_Ptr->y, m_Ptr->z };
}
template <>
inline void RawScriptField<Vec3>::Deserialize(const json& j)
{
	if (m_Ptr && j.contains(m_Name) && j[m_Name].is_array() && j[m_Name].size() == 3)
	{
		m_Ptr->x = j[m_Name][0].get<float>();
		m_Ptr->y = j[m_Name][1].get<float>();
		m_Ptr->z = j[m_Name][2].get<float>();
	}
}
template <>
inline bool RawScriptField<Vec3>::OnInspectorGUI()
{
	return m_Ptr ? EditorGUI::Vector3Field(m_Name, *m_Ptr) : false;
}

template <>
inline ScriptFieldType RawScriptField<std::string>::GetFieldType() const { return ScriptFieldType::String; }
template <>
inline void RawScriptField<std::string>::Serialize(json& j) const { if (m_Ptr) j[m_Name] = *m_Ptr; }
template <>
inline void RawScriptField<std::string>::Deserialize(const json& j)
{
	if (m_Ptr && j.contains(m_Name) && j[m_Name].is_string())
		*m_Ptr = j[m_Name].get<std::string>();
}
template <>
inline bool RawScriptField<std::string>::OnInspectorGUI()
{
	if (!m_Ptr) return false;
	EditorGUI::Label(m_Name);
	ImGui::SameLine();
	return EditorGUI::InputField(*m_Ptr);
}
