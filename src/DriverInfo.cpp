#include "DriverInfo.h"

const wchar_t* DriverStatusText(DriverStatus status)
{
    switch (status)
    {
    case DriverStatus::Ok:              return L"Treiber OK";
    case DriverStatus::Problem:         return L"Problem erkannt";
    case DriverStatus::UpdateAvailable: return L"Update verfügbar";
    case DriverStatus::Pending:
    default:                            return L"Scan ausstehend";
    }
}
