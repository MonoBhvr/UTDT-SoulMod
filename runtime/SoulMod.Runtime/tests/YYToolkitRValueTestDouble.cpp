#include <YYToolkit/YYTK_Shared.hpp>

namespace YYTK {

RValue::RValue() = default;
RValue::RValue(const RValue& other) = default;
RValue& RValue::operator=(const RValue& other) = default;
RValue::~RValue() = default;
RValue::RValue(std::string_view) : RValue() { m_Kind = VALUE_STRING; }
RValue::RValue(bool value) : RValue() { m_i32 = value ? 1 : 0; m_Kind = VALUE_BOOL; }
double RValue::ToDouble() const { return m_Real; }
bool RValue::ToBoolean() const { return m_i32 != 0; }
std::string RValue::ToString() const { return {}; }

}
