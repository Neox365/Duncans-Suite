#include "Utils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

bool IsRunningAsAdmin()
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &adminGroup))
        return false;

    BOOL member = FALSE;
    if (!CheckTokenMembership(nullptr, adminGroup, &member))
        member = FALSE;
    FreeSid(adminGroup);
    return member != FALSE;
}

std::wstring CurrentTimeText()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t text[32];
    wsprintfW(text, L"%02u.%02u.%04u %02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    text[31] = 0;
    return text;
}
