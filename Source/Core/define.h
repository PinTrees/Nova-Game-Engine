#pragma once
#include "NovaApi.h"

#define SINGLE_BODY(type) type* type::pInst = nullptr;
#define SINGLE_HEADER(type) private:						\
						static type* pInst;					\
					 public:								\
						static type* GetI()					\
						{									\
							if(pInst == nullptr)			\
								pInst = new type;			\
							return pInst;					\
						}									\
						static void Dispose()				\
						{									\
							if(pInst != nullptr)			\
								 delete pInst;				\
							pInst = nullptr;				\
						}									\
						private:							\
								type();						\
								~type();					\


#define PI 3.14159265359
#define PI_2 1.57079632679
#define DT TimeManager::GetI()->GetDT()

#define INPUT_CHECK(key , state) InputManager::GetI()->GetKeyState(key) == state
#define INPUT_KEY_HOLD(key) INPUT_CHECK(key,KEY_STATE::HOLD)
#define INPUT_KEY_DOWN(key) INPUT_CHECK(key, KEY_STATE::TAP)
#define INPUT_KEY_UP(key)	INPUT_CHECK(key,KEY_STATE::AWAY)
#define INPUT_KEY_NONE(key) INPUT_CHECK(key,KEY_STATE::NONE)

#define	MOUSE_POSITION InputManager::GetI()->GetMousePos()


#define SERIALIZE(CLASS) friend void to_json(json& j, const CLASS& obj);
#define DESERIALIZE(CLASS) friend void from_json(const json& j, CLASS& obj);


#define GENERATE_COMPONENT_BODY(CLASS) public:                            \
    json toJson() const override;                                         \
    void fromJson(const json& j) override;                                \
    friend void to_json(json& j, const CLASS& obj) { j = obj.toJson(); }  \
    friend void from_json(const json& j, CLASS& obj) { obj.fromJson(j); } \
																		  \
public:																	  \
    std::string GetType() const override { return #CLASS; }               \
    static std::shared_ptr<Component> CreateInstance() {                  \
        return std::make_shared<CLASS>();								  \
    }																	  \
																		  \
private:																  \
	static const bool registered;										  \

#define GENERATE_COMPONENT_FUNC_TOJSON(CLASS)	json CLASS::toJson() const
#define GENERATE_COMPONENT_FUNC_FROMJSON(CLASS)	void CLASS::fromJson(const json& j)

// inline 변수: 헤더를 여러 번역 단위(PCH 를 쓰지 않는 파일 포함)에서 포함해도 한 번만 정의/등록된다
//  엔진 컴포넌트는 엔진(NovaCore.dll) 안에서만 등록한다 — 패키지가 엔진 헤더를 포함해도 다시 정의·등록하지 않게
//  패키지 DLL 의 컴포넌트는 REGISTER_PACKAGE_COMPONENT (DLL 을 불러올 때 등록)
// clang (안드로이드) 은 쓰지 않는 inline 변수를 번역 단위에 남기지 않는다 → pch 에 없는 헤더의 컴포넌트 (UI 등) 가 등록되지 않는다. used 로 늘 남긴다
#if defined(__clang__) || defined(__GNUC__)
#define NOVA_KEEP_REGISTRATION __attribute__((used))
#else
#define NOVA_KEEP_REGISTRATION
#endif
#define REGISTER_PACKAGE_COMPONENT(CLASS)											\
inline const bool CLASS::registered NOVA_KEEP_REGISTRATION =								\
    ComponentFactory::Instance().RegisterComponent(#CLASS, CLASS::CreateInstance);	\

#if defined(NOVA_ENGINE_BUILD)
#define REGISTER_COMPONENT(CLASS) REGISTER_PACKAGE_COMPONENT(CLASS)
#else
#define REGISTER_COMPONENT(CLASS)
#endif

//Component Requied Func
//GENERATE_COMPONENT_BODY(Component)
//GENERATE_COMPONENT_FUNC_TOJSON(Component)
//{
//}
//GENERATE_COMPONENT_FUNC_FROMJSON(Component)
//{
//}