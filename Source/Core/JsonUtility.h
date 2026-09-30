#pragma once
#include <nlohmann/json.hpp>
using json = nlohmann::json;

float SafeGetFloat(const json& j, const std::string& key, size_t index, float defaultValue);

Vector3 SafeGetVector3(const json& j, const std::string& key, const Vector3& defaultValue);


json QuaternionToJSON(const Quaternion& q); 
Quaternion SafeGetQuaternion(const json& j, const std::string& key, const Quaternion& defaultValue);

#define SERIALIZE_QUATERNION(JSON, VALUE) JSON[#VALUE] = QuaternionToJSON(VALUE)   
#define DE_SERIALIZE_QUATERNION(JSON, VALUE) VALUE = SafeGetQuaternion(JSON, #VALUE, Quaternion::Identity)

#define SERIALIZE_VECTOR3(JSON, VALUE) JSON[#VALUE] = { VALUE.x, VALUE.y, VALUE.z };
#define DE_SERIALIZE_VECTOR3(JSON, VALUE) VALUE = SafeGetVector3(JSON, #VALUE, Vector3::Zero);
#define DE_SERIALIZE_VECTOR3_D(JSON, VALUE, Default) VALUE = SafeGetVector3(JSON, #VALUE, Default); 

#define SERIALIZE_BOOL(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = VALUE;
#define DE_SERIALIZE_BOOL(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME) && JSON.at(VALUE_NAME).is_boolean())   \
													    VALUE = j.at(VALUE_NAME).get<bool>();						    \
												    else VALUE = 0;														\

#define SERIALIZE_INT(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = VALUE;
#define DE_SERIALIZE_INT(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME) && JSON.at(VALUE_NAME).is_number_integer()) \
													VALUE = j.at(VALUE_NAME).get<int>();									\
												  else VALUE = 0;															\

#define SERIALIZE_FLOAT(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = VALUE;
#define DE_SERIALIZE_FLOAT(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME) && JSON.at(VALUE_NAME).is_number_float()) \
													  VALUE = j.at(VALUE_NAME).get<float>();								\
												  else VALUE = 0.f;			

#define SERIALIZE_ENUM(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = VALUE;
#define DE_SERIALIZE_ENUM(JSON, VALUE, VALUE_NAME, VALUE_TYPE) if (JSON.contains(VALUE_NAME))                  \
													          VALUE = j.at(VALUE_NAME).get<VALUE_TYPE>();	   \

#define SERIALIZE_TYPE(JSON, CLASS) JSON["type"] = #CLASS;
#define SERIALIZE_STRING(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = VALUE;
#define DE_SERIALIZE_STRING(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME)) {                   \
                                                         VALUE = j.at(VALUE_NAME).get<string>();        \
                                                      }                                                 \

#define SERIALIZE_WSTRING(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = wstring_to_string(VALUE);
#define DE_SERIALIZE_WSTRING(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME)) {                              \
                                                         VALUE = string_to_wstring(j.at(VALUE_NAME).get<string>()); \
                                                      }                                                             \

#define SERIALIZE_WSTRING_ARRAY(JSON, VECTOR, VALUE_NAME)   \
{                                                           \
    std::vector<std::string> stringArray;                   \
    for (const auto& wstr : VECTOR)                         \
    {                                                       \
        stringArray.push_back(wstring_to_string(wstr));     \
    }                                                       \
    JSON[VALUE_NAME] = stringArray;                         \
}
#define DE_SERIALIZE_WSTRING_ARRAY(JSON, VECTOR, VALUE_NAME)                            \
{                                                                                       \
    if (JSON.contains(VALUE_NAME))                                                      \
    {                                                                                   \
        VECTOR.clear();                                                                 \
        const auto& stringArray = JSON.at(VALUE_NAME).get<std::vector<std::string>>();  \
        for (const auto& str : stringArray)                                             \
        {                                                                               \
            VECTOR.push_back(string_to_wstring(str));                                   \
        }                                                                               \
    }                                                                                   \
}


#define SERIALIZE_PosNormalTexTan2_ARRAY(JSON, VECTOR, KEY)                                                         \
{                                                                                                                   \
    json array_json = json::array();                                                                                \
    for (const auto& vertex : VECTOR)                                                                               \
    {                                                                                                               \
        json vertex_json;                                                                                           \
        vertex_json["pos"] = { vertex.pos.x, vertex.pos.y, vertex.pos.z };                                          \
        vertex_json["normal"] = { vertex.normal.x, vertex.normal.y, vertex.normal.z };                              \
        vertex_json["tex"] = { vertex.tex.x, vertex.tex.y };                                                        \
        vertex_json["tangentU"] = { vertex.tangentU.x, vertex.tangentU.y, vertex.tangentU.z, vertex.tangentU.w };   \
        array_json.push_back(vertex_json);                                                                          \
    }                                                                                                               \
    JSON[KEY] = array_json;                                                                                         \
}

#define SERIALIZE_USHORT_ARRAY(JSON, VECTOR, KEY)          \
{                                                          \
    json array_json = json::array();                       \
    for (const auto& val : VECTOR)                         \
    {                                                      \
        array_json.push_back(val);                         \
    }                                                      \
    JSON[KEY] = array_json;                                \
}
 

#define SERIALIZE_FLOAT4(JSON, VALUE, VALUE_NAME) JSON[VALUE_NAME] = { VALUE.x, VALUE.y,VALUE.z, VALUE.w };
#define DE_SERIALIZE_FLOAT4(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME)) {                                                   \
                                                         auto __val = JSON.at(VALUE_NAME).get<std::vector<float>>();                    \
                                                         if (__val.size() == 4) {                                                       \
                                                             VALUE = XMFLOAT4{ __val[0], __val[1], __val[2], __val[3] };                \
                                                         } else {                                                                       \
                                                             VALUE = XMFLOAT4{ 0, 0, 0, 0 };                                            \
                                                         }                                                                              \
                                                     } else {                                                                           \
                                                         VALUE = XMFLOAT4{ 0, 0, 0, 0 };                                                \
                                                     }							


#define DE_SERIALIZE_FLOAT3(JSON, VALUE, VALUE_NAME) if (JSON.contains(VALUE_NAME)) {                                                   \
                                                         auto __val = JSON.at(VALUE_NAME).get<std::vector<float>>();                    \
                                                         if (__val.size() == 3) {                                                       \
                                                             VALUE = XMFLOAT3{ __val[0], __val[1], __val[2] };                \
                                                         } else {                                                                       \
                                                             VALUE = XMFLOAT3{ 0, 0, 0};                                            \
                                                         }                                                                              \
                                                     } else {                                                                           \
                                                         VALUE = XMFLOAT3{ 0, 0, 0 };                                                \
                                                     }				