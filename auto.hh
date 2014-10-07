#pragma once

#define TOKEN_PASTEx(x, y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTEx(x, y)

template <class T>
class AutoCallOnOutOfScope
{
public:
    AutoCallOnOutOfScope(T& destructor) : m_destructor(destructor) { }
    ~AutoCallOnOutOfScope() { m_destructor(); }
private:
    T& m_destructor;
};

#define Auto_INTERNAL(Destructor, counter) \
    auto TOKEN_PASTE(Auto_func_, counter) = [&]() { Destructor; }; \
    AutoCallOnOutOfScope<decltype(TOKEN_PASTE(Auto_func_, counter))> TOKEN_PASTE(Auto_instance_, counter)(TOKEN_PASTE(Auto_func_, counter));

#define Auto(Destructor) Auto_INTERNAL(Destructor, __COUNTER__)

#define Initialize struct TOKEN_PASTE(AutoInit_, __LINE__) { TOKEN_PASTE(AutoInit_, __LINE__)(); } TOKEN_PASTE(auto_init_, __LINE__); TOKEN_PASTE(AutoInit_, __LINE__)::TOKEN_PASTE(AutoInit_, __LINE__)()
