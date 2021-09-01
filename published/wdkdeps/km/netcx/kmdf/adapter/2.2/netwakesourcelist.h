// Copyright (C) Microsoft Corporation. All rights reserved.

//
// NOTE: This header is generated by stubwork.  Please make any 
//       modifications to the corresponding template files 
//       (.x or .y) and use stubwork to regenerate the header
//

#ifndef _NETWAKESOURCELIST_2_2_H_
#define _NETWAKESOURCELIST_2_2_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START




typedef struct _NET_WAKE_SOURCE_LIST
{
    ULONG Size;
    void * Reserved[4];
} NET_WAKE_SOURCE_LIST;

inline
void
NET_WAKE_SOURCE_LIST_INIT(
    _Out_ NET_WAKE_SOURCE_LIST * List
)
{
    RtlZeroMemory(List, sizeof(*List));
    List->Size = sizeof(*List);
}

//
// NET Function: NetDeviceGetWakeSourceList
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETDEVICEGETWAKESOURCELIST)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _Inout_
    NET_WAKE_SOURCE_LIST* List
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetDeviceGetWakeSourceList(
    _In_
    WDFDEVICE Device,
    _Inout_
    NET_WAKE_SOURCE_LIST* List
    )
{
    ((PFN_NETDEVICEGETWAKESOURCELIST) NetFunctions[NetDeviceGetWakeSourceListTableIndex])(NetDriverGlobals, Device, List);
}

//
// NET Function: NetWakeSourceListGetCount
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
SIZE_T
(NTAPI *PFN_NETWAKESOURCELISTGETCOUNT)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    CONST NET_WAKE_SOURCE_LIST* List
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
SIZE_T
NetWakeSourceListGetCount(
    _In_
    CONST NET_WAKE_SOURCE_LIST* List
    )
{
    return ((PFN_NETWAKESOURCELISTGETCOUNT) NetFunctions[NetWakeSourceListGetCountTableIndex])(NetDriverGlobals, List);
}

//
// NET Function: NetWakeSourceListGetElement
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NETWAKESOURCE
(NTAPI *PFN_NETWAKESOURCELISTGETELEMENT)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    CONST NET_WAKE_SOURCE_LIST* List,
    _In_
    SIZE_T Index
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NETWAKESOURCE
NetWakeSourceListGetElement(
    _In_
    CONST NET_WAKE_SOURCE_LIST* List,
    _In_
    SIZE_T Index
    )
{
    return ((PFN_NETWAKESOURCELISTGETELEMENT) NetFunctions[NetWakeSourceListGetElementTableIndex])(NetDriverGlobals, List, Index);
}



WDF_EXTERN_C_END

#endif // _NETWAKESOURCELIST_2_2_H_

