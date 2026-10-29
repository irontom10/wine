/*
 * WMI scripting SWbemDateTime implementation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <stdlib.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "oleauto.h"
#include "wbemcli.h"
#include "wbemdisp.h"

#include "wine/debug.h"
#include "wbemdisp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(wbemdisp);

#define TICKS_PER_MINUTE 600000000ULL

struct datetime
{
    ISWbemDateTime ISWbemDateTime_iface;
    LONG refs;
    WCHAR value[26];
};

struct datetime_field
{
    unsigned int offset, length;
    LONG min, max;
};

enum datetime_component
{
    FIELD_YEAR, FIELD_MONTH, FIELD_DAY, FIELD_HOURS,
    FIELD_MINUTES, FIELD_SECONDS, FIELD_MICROSECONDS, FIELD_UTC
};

static const struct datetime_field datetime_fields[] =
{
    { 0, 4, 0, 9999 }, { 4, 2, 1, 12 }, { 6, 2, 1, 31 },
    { 8, 2, 0, 23 }, { 10, 2, 0, 59 }, { 12, 2, 0, 59 },
    { 15, 6, 0, 999999 }, { 22, 3, 0, 999 }
};

static struct datetime *impl_from_ISWbemDateTime( ISWbemDateTime *iface )
{
    return CONTAINING_RECORD( iface, struct datetime, ISWbemDateTime_iface );
}

static LONG read_digits( const WCHAR *value, unsigned int length )
{
    unsigned int i;
    LONG result = 0;
    for (i = 0; i < length; ++i)
        result = result * 10 + (value[i] - '0');
    return result;
}

static void write_digits( WCHAR *value, unsigned int length, LONG number )
{
    while (length--)
    {
        value[length] = '0' + number % 10;
        number /= 10;
    }
}

static BOOL digits_or_wildcards( const WCHAR *value, unsigned int length, BOOL interval )
{
    unsigned int i;
    if (value[0] == '*' && !interval)
    {
        for (i = 1; i < length; ++i)
            if (value[i] != '*') return FALSE;
        return TRUE;
    }
    for (i = 0; i < length; ++i)
        if (value[i] < '0' || value[i] > '9') return FALSE;
    return TRUE;
}

static BOOL validate_value( const WCHAR *value )
{
    unsigned int i;
    BOOL interval;

    if (!value || lstrlenW( value ) != 25 || value[14] != '.') return FALSE;
    interval = (value[21] == ':');
    if (!interval && value[21] != '+' && value[21] != '-') return FALSE;
    if (interval)
    {
        if (!digits_or_wildcards( value, 8, TRUE )) return FALSE;
        if (value[22] != '0' || value[23] != '0' || value[24] != '0') return FALSE;
    }
    else
    {
        for (i = 0; i < 3; ++i)
        {
            const struct datetime_field *f = &datetime_fields[i];
            if (!digits_or_wildcards( value + f->offset, f->length, FALSE )) return FALSE;
            if (value[f->offset] != '*' && i && (read_digits( value + f->offset, f->length ) < f->min ||
                    read_digits( value + f->offset, f->length ) > f->max)) return FALSE;
        }
    }
    for (i = FIELD_HOURS; i <= FIELD_MICROSECONDS; ++i)
    {
        const struct datetime_field *f = &datetime_fields[i];
        if (!digits_or_wildcards( value + f->offset, f->length, interval )) return FALSE;
        if (value[f->offset] != '*' && read_digits( value + f->offset, f->length ) > f->max) return FALSE;
    }
    if (!interval && !digits_or_wildcards( value + 22, 3, FALSE )) return FALSE;
    return TRUE;
}

static HRESULT WINAPI datetime_QueryInterface( ISWbemDateTime *iface, REFIID iid, void **object )
{
    if (!object) return E_POINTER;
    *object = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IDispatch ) &&
        !IsEqualGUID( iid, &IID_ISWbemDateTime )) return E_NOINTERFACE;
    *object = iface;
    ISWbemDateTime_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI datetime_AddRef( ISWbemDateTime *iface )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    return InterlockedIncrement( &date->refs );
}

static ULONG WINAPI datetime_Release( ISWbemDateTime *iface )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    ULONG refs = InterlockedDecrement( &date->refs );
    if (!refs) free( date );
    return refs;
}

static HRESULT datetime_typeinfo( ITypeInfo **info )
{
    ITypeLib *typelib;
    HRESULT hr;

    hr = LoadRegTypeLib( &LIBID_WbemScripting, 1, 2, LOCALE_SYSTEM_DEFAULT, &typelib );
    if (FAILED( hr )) return hr;
    hr = ITypeLib_GetTypeInfoOfGuid( typelib, &IID_ISWbemDateTime, info );
    ITypeLib_Release( typelib );
    return hr;
}

static HRESULT WINAPI datetime_GetTypeInfoCount( ISWbemDateTime *iface, UINT *count )
{
    if (!count) return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI datetime_GetTypeInfo( ISWbemDateTime *iface, UINT index, LCID lcid, ITypeInfo **info )
{
    if (!info) return E_POINTER;
    *info = NULL;
    if (index) return DISP_E_BADINDEX;
    return datetime_typeinfo( info );
}

static HRESULT WINAPI datetime_GetIDsOfNames( ISWbemDateTime *iface, REFIID iid, LPOLESTR *names,
                                               UINT count, LCID lcid, DISPID *ids )
{
    ITypeInfo *info;
    HRESULT hr;
    if (!IsEqualGUID( iid, &IID_NULL )) return DISP_E_UNKNOWNINTERFACE;
    if (!names || !count || !ids) return E_INVALIDARG;
    hr = datetime_typeinfo( &info );
    if (SUCCEEDED( hr ))
    {
        hr = ITypeInfo_GetIDsOfNames( info, names, count, ids );
        ITypeInfo_Release( info );
    }
    return hr;
}

static HRESULT WINAPI datetime_Invoke( ISWbemDateTime *iface, DISPID member, REFIID iid,
                                        LCID lcid, WORD flags, DISPPARAMS *params,
                                        VARIANT *result, EXCEPINFO *excep, UINT *arg_err )
{
    ITypeInfo *info;
    HRESULT hr;
    if (!IsEqualGUID( iid, &IID_NULL )) return DISP_E_UNKNOWNINTERFACE;
    hr = datetime_typeinfo( &info );
    if (SUCCEEDED( hr ))
    {
        hr = ITypeInfo_Invoke( info, iface, member, flags, params, result, excep, arg_err );
        ITypeInfo_Release( info );
    }
    return hr;
}

static HRESULT WINAPI datetime_get_Value( ISWbemDateTime *iface, BSTR *value )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    if (!value) return E_POINTER;
    *value = SysAllocString( date->value );
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI datetime_put_Value( ISWbemDateTime *iface, BSTR value )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    if (!validate_value( value )) return WBEM_E_INVALID_PARAMETER;
    memcpy( date->value, value, sizeof(date->value) );
    return S_OK;
}

static HRESULT datetime_get_numeric( struct datetime *date, enum datetime_component component, LONG *value )
{
    const struct datetime_field *field = &datetime_fields[component];
    if (!value) return E_POINTER;
    if (date->value[21] == ':' && component == FIELD_DAY)
    {
        *value = read_digits( date->value, 8 );
        return S_OK;
    }
    if (date->value[21] == ':' && (component == FIELD_YEAR || component == FIELD_MONTH))
    {
        *value = 0;
        return S_OK;
    }
    *value = date->value[field->offset] == '*' ? 0 :
             read_digits( date->value + field->offset, field->length );
    if (component == FIELD_UTC && date->value[21] == '-') *value = -*value;
    return S_OK;
}

static HRESULT datetime_put_numeric( struct datetime *date, enum datetime_component component, LONG value )
{
    const struct datetime_field *field = &datetime_fields[component];
    if (date->value[21] == ':' && component == FIELD_DAY)
    {
        if (value < 0 || value > 99999999) return WBEM_E_INVALID_PARAMETER;
        write_digits( date->value, 8, value );
        return S_OK;
    }
    if (date->value[21] == ':' && (component == FIELD_YEAR || component == FIELD_MONTH))
        return WBEM_E_INVALID_PARAMETER;
    if (component == FIELD_UTC)
    {
        if (value < -999 || value > 999) return WBEM_E_INVALID_PARAMETER;
        date->value[21] = value < 0 ? '-' : '+';
        if (value < 0) value = -value;
    }
    else if (value < field->min || value > field->max) return WBEM_E_INVALID_PARAMETER;
    write_digits( date->value + field->offset, field->length, value );
    return S_OK;
}

static HRESULT datetime_get_specified( struct datetime *date, enum datetime_component component,
                                       VARIANT_BOOL *value )
{
    if (!value) return E_POINTER;
    *value = date->value[21] == ':' ? VARIANT_TRUE :
             date->value[datetime_fields[component].offset] == '*' ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
}

static HRESULT datetime_put_specified( struct datetime *date, enum datetime_component component,
                                       VARIANT_BOOL value )
{
    const struct datetime_field *field = &datetime_fields[component];
    if (date->value[21] == ':') return value ? S_OK : WBEM_E_INVALID_PARAMETER;
    if (value)
    {
        if (date->value[field->offset] == '*')
            write_digits( date->value + field->offset, field->length, component == FIELD_MONTH ||
                          component == FIELD_DAY ? 1 : 0 );
    }
    else memset( date->value + field->offset, '*', field->length * sizeof(WCHAR) );
    return S_OK;
}

static HRESULT WINAPI datetime_get_Year( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_YEAR, value );
}

static HRESULT WINAPI datetime_put_Year( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_YEAR, value );
}

static HRESULT WINAPI datetime_get_YearSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_YEAR, value );
}

static HRESULT WINAPI datetime_put_YearSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_YEAR, value );
}

static HRESULT WINAPI datetime_get_Month( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MONTH, value );
}

static HRESULT WINAPI datetime_put_Month( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MONTH, value );
}

static HRESULT WINAPI datetime_get_MonthSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_MONTH, value );
}

static HRESULT WINAPI datetime_put_MonthSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_MONTH, value );
}

static HRESULT WINAPI datetime_get_Day( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_DAY, value );
}

static HRESULT WINAPI datetime_put_Day( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_DAY, value );
}

static HRESULT WINAPI datetime_get_DaySpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_DAY, value );
}

static HRESULT WINAPI datetime_put_DaySpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_DAY, value );
}

static HRESULT WINAPI datetime_get_Hours( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_HOURS, value );
}

static HRESULT WINAPI datetime_put_Hours( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_HOURS, value );
}

static HRESULT WINAPI datetime_get_HoursSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_HOURS, value );
}

static HRESULT WINAPI datetime_put_HoursSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_HOURS, value );
}

static HRESULT WINAPI datetime_get_Minutes( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MINUTES, value );
}

static HRESULT WINAPI datetime_put_Minutes( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MINUTES, value );
}

static HRESULT WINAPI datetime_get_MinutesSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_MINUTES, value );
}

static HRESULT WINAPI datetime_put_MinutesSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_MINUTES, value );
}

static HRESULT WINAPI datetime_get_Seconds( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_SECONDS, value );
}

static HRESULT WINAPI datetime_put_Seconds( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_SECONDS, value );
}

static HRESULT WINAPI datetime_get_SecondsSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_SECONDS, value );
}

static HRESULT WINAPI datetime_put_SecondsSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_SECONDS, value );
}

static HRESULT WINAPI datetime_get_Microseconds( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MICROSECONDS, value );
}

static HRESULT WINAPI datetime_put_Microseconds( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_MICROSECONDS, value );
}

static HRESULT WINAPI datetime_get_MicrosecondsSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_MICROSECONDS, value );
}

static HRESULT WINAPI datetime_put_MicrosecondsSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_MICROSECONDS, value );
}

static HRESULT WINAPI datetime_get_UTC( ISWbemDateTime *iface, LONG *value )
{
    return datetime_get_numeric( impl_from_ISWbemDateTime( iface ), FIELD_UTC, value );
}

static HRESULT WINAPI datetime_put_UTC( ISWbemDateTime *iface, LONG value )
{
    return datetime_put_numeric( impl_from_ISWbemDateTime( iface ), FIELD_UTC, value );
}

static HRESULT WINAPI datetime_get_UTCSpecified( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    return datetime_get_specified( impl_from_ISWbemDateTime( iface ), FIELD_UTC, value );
}

static HRESULT WINAPI datetime_put_UTCSpecified( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    return datetime_put_specified( impl_from_ISWbemDateTime( iface ), FIELD_UTC, value );
}

static HRESULT WINAPI datetime_get_IsInterval( ISWbemDateTime *iface, VARIANT_BOOL *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ISWbemDateTime( iface )->value[21] == ':' ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI datetime_put_IsInterval( ISWbemDateTime *iface, VARIANT_BOOL value )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    unsigned int i;

    if (value)
    {
        date->value[21] = ':';
        for (i = 0; i < 21; ++i)
            if (date->value[i] == '*') date->value[i] = '0';
        date->value[22] = date->value[23] = date->value[24] = '0';
    }
    else if (date->value[21] == ':') date->value[21] = '+';
    return S_OK;
}

/* Convert a fully specified CIM datetime to UTC FILETIME. */
static HRESULT datetime_to_filetime( const struct datetime *date, FILETIME *filetime )
{
    SYSTEMTIME system;
    ULARGE_INTEGER ticks;
    LONGLONG result, offset;
    unsigned int i;

    if (date->value[21] == ':') return WBEM_E_FAILED;
    for (i = 0; i < ARRAY_SIZE(datetime_fields); ++i)
        if (date->value[datetime_fields[i].offset] == '*') return WBEM_E_FAILED;

    memset( &system, 0, sizeof(system) );
    system.wYear = read_digits( date->value, 4 );
    system.wMonth = read_digits( date->value + 4, 2 );
    system.wDay = read_digits( date->value + 6, 2 );
    system.wHour = read_digits( date->value + 8, 2 );
    system.wMinute = read_digits( date->value + 10, 2 );
    system.wSecond = read_digits( date->value + 12, 2 );
    system.wMilliseconds = read_digits( date->value + 15, 6 ) / 1000;

    if (!SystemTimeToFileTime( &system, filetime )) return WBEM_E_FAILED;
    ticks.LowPart = filetime->dwLowDateTime;
    ticks.HighPart = filetime->dwHighDateTime;
    offset = read_digits( date->value + 22, 3 );
    if (date->value[21] == '-') offset = -offset;
    result = (LONGLONG)ticks.QuadPart + (read_digits( date->value + 15, 6 ) % 1000) * 10 -
             offset * TICKS_PER_MINUTE;
    if (result < 0) return WBEM_E_FAILED;
    ticks.QuadPart = result;
    filetime->dwLowDateTime = ticks.LowPart;
    filetime->dwHighDateTime = ticks.HighPart;
    return S_OK;
}

static HRESULT datetime_set_systemtime( struct datetime *date, const SYSTEMTIME *time, LONG offset )
{
    if (offset < -999 || offset > 999) return WBEM_E_FAILED;
    write_digits( date->value, 4, time->wYear );
    write_digits( date->value + 4, 2, time->wMonth );
    write_digits( date->value + 6, 2, time->wDay );
    write_digits( date->value + 8, 2, time->wHour );
    write_digits( date->value + 10, 2, time->wMinute );
    write_digits( date->value + 12, 2, time->wSecond );
    date->value[14] = '.';
    write_digits( date->value + 15, 6, time->wMilliseconds * 1000 );
    date->value[21] = offset < 0 ? '-' : '+';
    write_digits( date->value + 22, 3, offset < 0 ? -offset : offset );
    date->value[25] = 0;
    return S_OK;
}

static HRESULT datetime_local_offset( const SYSTEMTIME *local, LONG *offset )
{
    SYSTEMTIME utc;
    FILETIME local_ft, utc_ft;
    ULARGE_INTEGER local_ticks, utc_ticks;

    if (!TzSpecificLocalTimeToSystemTime( NULL, local, &utc )) return WBEM_E_FAILED;
    if (!SystemTimeToFileTime( local, &local_ft ) || !SystemTimeToFileTime( &utc, &utc_ft ))
        return WBEM_E_FAILED;
    local_ticks.LowPart = local_ft.dwLowDateTime;
    local_ticks.HighPart = local_ft.dwHighDateTime;
    utc_ticks.LowPart = utc_ft.dwLowDateTime;
    utc_ticks.HighPart = utc_ft.dwHighDateTime;
    *offset = ((LONGLONG)local_ticks.QuadPart - (LONGLONG)utc_ticks.QuadPart) / TICKS_PER_MINUTE;
    return S_OK;
}

static HRESULT WINAPI datetime_GetVarDate( ISWbemDateTime *iface, VARIANT_BOOL is_local, DATE *value )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    SYSTEMTIME utc, system;
    FILETIME filetime;
    HRESULT hr;

    if (!value) return E_POINTER;
    if (FAILED(hr = datetime_to_filetime( date, &filetime ))) return hr;
    if (!FileTimeToSystemTime( &filetime, &utc )) return WBEM_E_FAILED;
    if (is_local)
    {
        if (!SystemTimeToTzSpecificLocalTime( NULL, &utc, &system )) return WBEM_E_FAILED;
    }
    else system = utc;
    if (!SystemTimeToVariantTime( &system, value )) return WBEM_E_FAILED;
    return S_OK;
}

static HRESULT WINAPI datetime_SetVarDate( ISWbemDateTime *iface, DATE value, VARIANT_BOOL is_local )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    SYSTEMTIME system;
    LONG offset = 0;
    HRESULT hr;

    if (!VariantTimeToSystemTime( value, &system )) return WBEM_E_INVALID_PARAMETER;
    if (is_local && FAILED(hr = datetime_local_offset( &system, &offset ))) return hr;
    return datetime_set_systemtime( date, &system, offset );
}

static HRESULT WINAPI datetime_GetFileTime( ISWbemDateTime *iface, VARIANT_BOOL is_local, BSTR *value )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    FILETIME filetime;
    SYSTEMTIME utc, local;
    ULARGE_INTEGER ticks;
    WCHAR string[22];
    unsigned int i;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (FAILED(hr = datetime_to_filetime( date, &filetime ))) return hr;
    if (is_local)
    {
        if (!FileTimeToSystemTime( &filetime, &utc ) ||
            !SystemTimeToTzSpecificLocalTime( NULL, &utc, &local ) ||
            !SystemTimeToFileTime( &local, &filetime )) return WBEM_E_FAILED;
    }
    ticks.LowPart = filetime.dwLowDateTime;
    ticks.HighPart = filetime.dwHighDateTime;
    for (i = 0; i < 20; ++i)
    {
        string[19 - i] = '0' + ticks.QuadPart % 10;
        ticks.QuadPart /= 10;
    }
    string[20] = 0;
    for (i = 0; i < 19 && string[i] == '0'; ++i) ;
    *value = SysAllocString( string + i );
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI datetime_SetFileTime( ISWbemDateTime *iface, BSTR value, VARIANT_BOOL is_local )
{
    struct datetime *date = impl_from_ISWbemDateTime( iface );
    ULARGE_INTEGER ticks;
    FILETIME filetime;
    SYSTEMTIME utc, local;
    LONG offset = 0;
    unsigned int i;
    HRESULT hr;

    if (!value || !*value) return WBEM_E_INVALID_PARAMETER;
    ticks.QuadPart = 0;
    for (i = 0; value[i]; ++i)
    {
        unsigned int digit;
        if (i >= 20 || value[i] < '0' || value[i] > '9') return WBEM_E_INVALID_PARAMETER;
        digit = value[i] - '0';
        if (ticks.QuadPart > (~(ULONGLONG)0 - digit) / 10) return WBEM_E_INVALID_PARAMETER;
        ticks.QuadPart = ticks.QuadPart * 10 + digit;
    }
    filetime.dwLowDateTime = ticks.LowPart;
    filetime.dwHighDateTime = ticks.HighPart;
    if (!FileTimeToSystemTime( &filetime, &utc )) return WBEM_E_INVALID_PARAMETER;
    if (is_local)
    {
        if (!SystemTimeToTzSpecificLocalTime( NULL, &utc, &local )) return WBEM_E_FAILED;
        if (FAILED(hr = datetime_local_offset( &local, &offset ))) return hr;
    }
    else local = utc;
    return datetime_set_systemtime( date, &local, offset );
}

static const ISWbemDateTimeVtbl datetime_vtbl =
{
    datetime_QueryInterface,
    datetime_AddRef,
    datetime_Release,
    datetime_GetTypeInfoCount,
    datetime_GetTypeInfo,
    datetime_GetIDsOfNames,
    datetime_Invoke,
    datetime_get_Value,
    datetime_put_Value,
    datetime_get_Year,
    datetime_put_Year,
    datetime_get_YearSpecified,
    datetime_put_YearSpecified,
    datetime_get_Month,
    datetime_put_Month,
    datetime_get_MonthSpecified,
    datetime_put_MonthSpecified,
    datetime_get_Day,
    datetime_put_Day,
    datetime_get_DaySpecified,
    datetime_put_DaySpecified,
    datetime_get_Hours,
    datetime_put_Hours,
    datetime_get_HoursSpecified,
    datetime_put_HoursSpecified,
    datetime_get_Minutes,
    datetime_put_Minutes,
    datetime_get_MinutesSpecified,
    datetime_put_MinutesSpecified,
    datetime_get_Seconds,
    datetime_put_Seconds,
    datetime_get_SecondsSpecified,
    datetime_put_SecondsSpecified,
    datetime_get_Microseconds,
    datetime_put_Microseconds,
    datetime_get_MicrosecondsSpecified,
    datetime_put_MicrosecondsSpecified,
    datetime_get_UTC,
    datetime_put_UTC,
    datetime_get_UTCSpecified,
    datetime_put_UTCSpecified,
    datetime_get_IsInterval,
    datetime_put_IsInterval,
    datetime_GetVarDate,
    datetime_SetVarDate,
    datetime_GetFileTime,
    datetime_SetFileTime
};

HRESULT SWbemDateTime_create( void **object )
{
    struct datetime *date;

    if (!object) return E_POINTER;
    *object = NULL;
    if (!(date = calloc( 1, sizeof(*date) ))) return E_OUTOFMEMORY;
    date->ISWbemDateTime_iface.lpVtbl = &datetime_vtbl;
    date->refs = 1;
    memcpy( date->value, L"**************.******+***", sizeof(date->value) );
    *object = &date->ISWbemDateTime_iface;
    return S_OK;
}
