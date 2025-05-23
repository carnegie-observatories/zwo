/**************************************************
this is the ZWO filter wheel EFW SDK
any question feel free contact us:software@zwoptical.com

here is the suggested procedure.

--> EFWGetNum
--> EFWGetID for each filter wheel

--> EFWOpen
--> EFWGetProperty
--> EFWGetPosition
--> EFWSetPosition
	...
--> EFWClose

***************************************************/
#ifndef EFW_FILTER_H
#define EFW_FILTER_H


#ifdef _WINDOWS
#define EFW_API __declspec(dllexport)
#else
#define EFW_API 
#endif

#define EFW_ID_MAX 128

typedef struct _EFW_INFO
{
	int ID;
	char Name[64];
	int slotNum;
} EFW_INFO;


typedef enum _EFW_ERROR_CODE{
	EFW_SUCCESS = 0,
	EFW_ERROR_INVALID_INDEX,
	EFW_ERROR_INVALID_ID,
	EFW_ERROR_INVALID_VALUE,
	EFW_ERROR_REMOVED, //failed to find the filter wheel, maybe the filter wheel has been removed
	EFW_ERROR_MOVING,//filter wheel is moving
	EFW_ERROR_ERROR_STATE,//filter wheel is in error state
	EFW_ERROR_GENERAL_ERROR,//other error
	EFW_ERROR_NOT_SUPPORTED,
	EFW_ERROR_CLOSED,
	EFW_ERROR_END = -1
}EFW_ERROR_CODE;

typedef struct _EFW_ID{
	unsigned char id[8];
}EFW_ID;

typedef EFW_ID EFW_SN;

#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************************
Descriptions:
this should be the first API to be called
get number of connected EFW filter wheel, call this API to refresh device list if EFW is connected
or disconnected

Return: number of connected EFW filter wheel. 1 means 1 filter wheel is connected.
***************************************************************************/
EFW_API int EFWGetNum();

/***************************************************************************
Descriptions:
get the product ID of each wheel, at first set pPIDs as 0 and get length and then malloc a buffer to load the PIDs

Paras:
int* pPIDs: pointer to array of PIDs

Return: length of the array.

Note: This api will be deprecated. Please use EFWCheck instead
***************************************************************************/
EFW_API int EFWGetProductIDs(int* pPIDs);

/***************************************************************************
Descriptions:
Check if the device is EFW

Paras:
int iVID: VID is 0x03C3 for EFW
int iPID: PID of the device

Return: If the device is EFW, return 1, otherwise return 0
***************************************************************************/
EFW_API int EFWCheck(int iVID, int iPID);

/***************************************************************************
Descriptions:
get ID of filter wheel

Paras:
int index: the index of filter wheel, from 0 to N - 1, N is returned by GetNum()

int* ID: pointer to ID. the ID is a unique integer, between 0 to EFW_ID_MAX - 1, after opened,
all the operation is base on this ID, the ID will not change.


Return: 
EFW_ERROR_INVALID_INDEX: index value is invalid
EFW_SUCCESS:  operation succeeds

***************************************************************************/
EFW_API EFW_ERROR_CODE EFWGetID(int index, int* ID);

/***************************************************************************
Descriptions:
open filter wheel

Paras:
int ID: the ID of filter wheel

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_GENERAL_ERROR: number of opened filter wheel reaches the maximum value.
EFW_ERROR_REMOVED: the filter wheel is removed.
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWOpen(int ID);

/***************************************************************************
Descriptions:
get property of filter wheel. SlotNum is 0 if not opened.

Paras:
int ID: the ID of filter wheel

EFW_INFO *pInfo:  pointer to structure containing the property of EFW

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_MOVING: slot number detection is in progress, generally this error will happen soon after filter wheel is connected.
EFW_SUCCESS: operation succeeds
EFW_ERROR_REMOVED: filter wheel is removed

***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWGetProperty(int ID, EFW_INFO *pInfo); 

/***************************************************************************
Descriptions:
get position of slot

Paras:
int ID: the ID of filter wheel

int *pPosition:  pointer to slot position, this value is between 0 to M - 1, M is slot number
this value is -1 if filter wheel is moving

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
EFW_ERROR_ERROR_STATE: filter wheel is in error state
EFW_ERROR_REMOVED: filter wheel is removed

***************************************************************************/	
EFW_API	EFW_ERROR_CODE EFWGetPosition(int ID, int *pPosition);
	
/***************************************************************************
Descriptions:
set position of slot

Paras:
int ID: the ID of filter wheel

int Position:  slot position, this value is between 0 to M - 1, M is slot number

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
EFW_ERROR_INVALID_VALUE: Position value is invalid
EFW_ERROR_MOVING: filter wheel is moving, should wait until idle
EFW_ERROR_ERROR_STATE: filter wheel is in error state
EFW_ERROR_REMOVED: filter wheel is removed

***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWSetPosition(int ID, int Position);

/***************************************************************************
Descriptions:
set unidirection of filter wheel

Paras:
int ID: the ID of filter wheel

bool bUnidirectional: if set as true, the filter wheel will rotate along one direction

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWSetDirection(int ID, bool bUnidirectional);

/***************************************************************************
Descriptions:
get unidirection of filter wheel

Paras:
int ID: the ID of filter wheel

bool *bUnidirectional: pointer to unidirection value .

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWGetDirection(int ID, bool *bUnidirectional);

/***************************************************************************
Descriptions:
calibrate filter wheel

Paras:
int ID: the ID of filter wheel

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
EFW_ERROR_MOVING: filter wheel is moving, should wait until idle
EFW_ERROR_ERROR_STATE: filter wheel is in error state
EFW_ERROR_REMOVED: filter wheel is removed
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWCalibrate(int ID);

/***************************************************************************
Descriptions:
close filter wheel

Paras:
int ID: the ID of filter wheel

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWClose(int ID);

/***************************************************************************
Descriptions:
get version string, like "0, 4, 0824"
***************************************************************************/
EFW_API char* EFWGetSDKVersion();


/***************************************************************************
Descriptions:
get hardware error code of filter wheel

Paras:
int ID: the ID of filter wheel

bool *pErrCode: pointer to error code .

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API EFW_ERROR_CODE EFWGetHWErrorCode(int ID, int *pErrCode);

/***************************************************************************
Descriptions:
Get firmware version of filter wheel

Paras:
int ID: the ID of filter wheel

int *major, int *minor, int *build: pointer to value.

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API	EFW_ERROR_CODE EFWGetFirmwareVersion(int ID, unsigned char *major, unsigned char *minor, unsigned char *build);

/***************************************************************************
Descriptions:
Get the serial number from a EFW

Paras:
int ID: the ID of focuser

EFW_SN* pSN: pointer to SN

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_ERROR_NOT_SUPPORTED: the firmware does not support serial number
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API EFW_ERROR_CODE EFWGetSerialNumber(int ID, EFW_SN* pSN);

/***************************************************************************
Descriptions:
Set the alias to a EFW

Paras:
int ID: the ID of filter

EFW_ID alias: the struct which contains the alias

Return: 
EFW_ERROR_INVALID_ID: invalid ID value
EFW_ERROR_CLOSED: not opened
EFW_ERROR_NOT_SUPPORTED: the firmware does not support setting alias
EFW_SUCCESS: operation succeeds
***************************************************************************/
EFW_API EFW_ERROR_CODE EFWSetID(int ID, EFW_ID alias);

#ifdef __cplusplus
}
#endif

#endif