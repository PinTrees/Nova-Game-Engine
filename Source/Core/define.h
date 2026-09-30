#pragma once

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

#define REGISTER_COMPONENT(CLASS)													\
const bool CLASS::registered =														\
    ComponentFactory::Instance().RegisterComponent(#CLASS, CLASS::CreateInstance);	\

//Component Requied Func
//GENERATE_COMPONENT_BODY(Component)
//GENERATE_COMPONENT_FUNC_TOJSON(Component)
//{
//}
//GENERATE_COMPONENT_FUNC_FROMJSON(Component)
//{
//}