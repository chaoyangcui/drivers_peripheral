/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "usb_interface_pool.h"
#include "linux_adapter.h"
#include "usb_io_manage.h"
#include "usb_protocol.h"

#define HDF_LOG_TAG USB_INTERFACE_POOL

static int32_t g_usbRequestObjectId = 0;

static void IfPipeObjInit(struct UsbPipe *pipeObj)
{
    if (pipeObj == NULL) {
        HDF_LOGE("%{public}s: invalid param", __func__);
        return;
    }

    (void)pipeObj;
}

static HDF_STATUS IfFreePipeObj(struct UsbPipe *pipeObj)
{
    HDF_STATUS ret = HDF_SUCCESS;

    if (pipeObj == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    pipeObj->object.objectId = 0;

    OsalMemFree(pipeObj);

    return ret;
}

HDF_STATUS IfDestroyPipeObj(struct UsbSdkInterface *interfaceObj, struct UsbPipe *pipeObj)
{
    HDF_STATUS ret = HDF_SUCCESS;
    struct UsbPipe *pipePos = NULL;
    struct UsbPipe *pipeTemp = NULL;
    bool found = false;
    bool destroyFlag = false;

    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfaceObj is NULL ", __func__, __LINE__);
        return HDF_FAILURE;
    }

    if (DListIsEmpty(&interfaceObj->pipeList)) {
        HDF_LOGE("%{public}s:%{public}d pipeList is empty ", __func__, __LINE__);
        return HDF_SUCCESS;
    }

    if (pipeObj == NULL) {
        /* Destroy all pipe object */
        destroyFlag = true;
    } else {
        /* Destroys the specified pipe object */
        destroyFlag = false;
    }

    OsalMutexLock(&interfaceObj->listLock);
    DLIST_FOR_EACH_ENTRY_SAFE(pipePos, pipeTemp, &interfaceObj->pipeList, struct UsbPipe, object.entry) {
        if ((destroyFlag == true) || ((destroyFlag == false)
            && (pipePos->object.objectId == pipeObj->object.objectId))) {
            found = true;
            DListRemove(&pipePos->object.entry);
            ret = IfFreePipeObj(pipePos);
            if (HDF_SUCCESS != ret) {
                HDF_LOGE("%{public}s:%{public}d IfFreePipeObj fail, ret=%{public}d ", __func__, __LINE__, ret);
                break;
            }

            if (destroyFlag == false) {
                break;
            }
        }
    }
    OsalMutexUnlock(&interfaceObj->listLock);

    if (found == false) {
        ret = HDF_FAILURE;
        HDF_LOGE("%{public}s:%{public}d the pipe object to be destroyed does not exist, ret=%{public}d ", \
            __func__, __LINE__, ret);
    }

    return ret;
}

static void IfInterfaceObjInit(struct UsbSdkInterface *interfaceObj)
{
    DListHeadInit(&interfaceObj->pipeList);
    OsalMutexInit(&interfaceObj->listLock);
    OsalAtomicSet(&interfaceObj->refCount, 0);
    interfaceObj->status = USB_INTERFACE_STATUS_NORMAL;
}

static HDF_STATUS IfFreeInterfaceObj(struct UsbSdkInterface *interfaceObj)
{
    HDF_STATUS ret;

    if (interfaceObj == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    interfaceObj->interface.object.objectId = 0;
    interfaceObj->parentObjectId = 0;
    ret = IfDestroyPipeObj(interfaceObj, NULL);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d IfDestroyPipeObj fail, ret=%{public}d ", \
            __func__, __LINE__, ret);
        return ret;
    }
    OsalMutexDestroy(&interfaceObj->listLock);
    interfaceObj->status = USB_INTERFACE_STATUS_NORMAL;

    OsalMemFree(interfaceObj);

    return ret;
}

static HDF_STATUS IfInterfacePoolInit(struct UsbInterfacePool *interfacePool, uint8_t busNum, uint8_t devAddr)
{
    interfacePool->session = NULL;
    OsalMutexInit(&interfacePool->mutex);
    DListHeadInit(&interfacePool->interfaceList);
    OsalMutexInit(&interfacePool->interfaceLock);
    DListHeadInit(&interfacePool->object.entry);
    OsalAtomicSet(&interfacePool->refCount, 0);
    interfacePool->busNum = busNum;
    interfacePool->devAddr = devAddr;
    OsalAtomicSet(&interfacePool->ioRefCount, 0);
    OsalMutexInit(&interfacePool->ioStopLock);
    OsalMutexLock(&interfacePool->ioStopLock);
    interfacePool->ioProcessStopStatus = USB_POOL_PROCESS_RUNNING;
    OsalMutexUnlock(&interfacePool->ioStopLock);
    interfacePool->device = NULL;

    /* create submit queue and wait complete queue */
    return UsbIoCreateQueue(interfacePool);
}

static HDF_STATUS IfFreeInterfacePool(struct UsbInterfacePool *interfacePool)
{
    HDF_STATUS ret;

    if (interfacePool == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    ret = UsbIoDestroyQueue(interfacePool);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d UsbIoDestroyQueue faile, ret=%{public}d ",
                 __func__, __LINE__, ret);
        return ret;
    }

    interfacePool->object.objectId = 0;
    OsalMutexDestroy(&interfacePool->mutex);
    ret = UsbIfDestroyInterfaceObj(interfacePool, NULL);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d UsbIfDestroyInterfaceObj fail, ret=%{public}d ",
            __func__, __LINE__, ret);
        return ret;
    }
    OsalMutexDestroy(&interfacePool->interfaceLock);
    interfacePool->busNum = 0;
    interfacePool->devAddr = 0;

    OsalMemFree(interfacePool);
    interfacePool = NULL;

    return ret;
}

HDF_STATUS IfDestroyInterfacePool(struct UsbInterfacePool *interfacePool)
{
    HDF_STATUS ret = HDF_SUCCESS;
    struct UsbInterfacePool *interfacePoolPos = NULL;
    struct UsbInterfacePool *interfacePoolTemp = NULL;
    struct UsbSession *session = NULL;
    bool found = false;

    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d the interfacePool is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    session = interfacePool->session;
    if (DListIsEmpty(&session->ifacePoolList)) {
        HDF_LOGE("%{public}s:%{public}d interface pool list is empty", __func__, __LINE__);
        return HDF_SUCCESS;
    }

    DLIST_FOR_EACH_ENTRY_SAFE(interfacePoolPos, interfacePoolTemp, &session->ifacePoolList,
        struct UsbInterfacePool, object.entry) {
        if (interfacePoolPos->object.objectId == interfacePool->object.objectId) {
            found = true;
            DListRemove(&interfacePoolPos->object.entry);
            ret = IfFreeInterfacePool(interfacePoolPos);
            if (ret != HDF_SUCCESS) {
                HDF_LOGE("%{public}s:%{public}d IfFreeInterfacePool fail, ret=%{public}d ",
                    __func__, __LINE__, ret);
                break;
            }
            break;
        }
    }

    if (found == false) {
        ret = HDF_FAILURE;
        HDF_LOGE("%{public}s:%{public}d the interfacePool object to be destroyed does not exist",
            __func__, __LINE__);
    }

    return ret;
}

static void IfInterfaceRefCount(struct UsbSdkInterface *interfaceObj,
    uint8_t interfaceIndex, bool refCountFlag, bool *claimFlag)
{
    if ((refCountFlag == true) && (interfaceIndex != USB_CTRL_INTERFACE_ID)) {
        if (OsalAtomicRead(&interfaceObj->refCount) == 0) {
            if (claimFlag != NULL) {
                *claimFlag = true;
            }
        } else {
            if (claimFlag != NULL) {
                *claimFlag = false;
            }
        }

        AdapterAtomicInc(&interfaceObj->refCount);
    }
}

static struct UsbPipe *IfFindPipeObj(struct UsbSdkInterface *interfaceObj, struct UsbPipeQueryPara queryPara)
{
    struct UsbPipe *pipePos = NULL;
    struct UsbPipe *pipeTemp = NULL;
    bool findFlag = false;

    if ((interfaceObj == NULL) || (interfaceObj->status == USB_INTERFACE_STATUS_REMOVE)
        || DListIsEmpty(&interfaceObj->pipeList)) {
        HDF_LOGE("%{public}s:%{public}d interfaceObj is NULL or status is remove or pipe list is empty.",
        __func__, __LINE__);
        return NULL;
    }

    OsalMutexLock(&interfaceObj->listLock);
    DLIST_FOR_EACH_ENTRY_SAFE(pipePos, pipeTemp, &interfaceObj->pipeList, struct UsbPipe, object.entry) {
        switch (queryPara.type) {
            case USB_PIPE_INDEX_TYPE:
                if (pipePos->info.pipeId == queryPara.pipeId) {
                    findFlag = true;
                }
                break;
            case USB_PIPE_DIRECTION_TYPE:
                if (pipePos->info.pipeDirection == queryPara.pipeDirection) {
                    findFlag = true;
                }
                break;
            default:
                break;
        }

        if (findFlag == true) {
            break;
        }
    }
    OsalMutexUnlock(&interfaceObj->listLock);

    if (findFlag == false) {
        HDF_LOGE("%{public}s:%{public}d the pipe object to be find does not exist. ", __func__, __LINE__);
        return NULL;
    } else {
        return pipePos;
    }
}

static struct UsbSdkInterface *IfFindInterfaceObj(struct UsbInterfacePool *interfacePool,
    struct UsbInterfaceQueryPara queryPara, bool refCountFlag, bool *claimFlag, bool statusFlag)
{
    struct UsbSdkInterface *interfacePos = NULL;
    struct UsbSdkInterface *interfaceTemp = NULL;
    bool found = false;

    if ((interfacePool == NULL) || DListIsEmpty(&interfacePool->interfaceList)) {
        HDF_LOGE("%{public}s:%{public}d interfacePool is NULL or interface list is empty.", __func__, __LINE__);
        return NULL;
    }

    OsalMutexLock(&interfacePool->interfaceLock);
    DLIST_FOR_EACH_ENTRY_SAFE(interfacePos, interfaceTemp, &interfacePool->interfaceList, \
        struct UsbSdkInterface, interface.object.entry) {
        switch (queryPara.type) {
            case USB_INTERFACE_INTERFACE_INDEX_TYPE:
                if ((interfacePos->interface.info.interfaceIndex == queryPara.interfaceIndex)
                && (interfacePos->interface.info.curAltSetting == interfacePos->altSettingId)){
                    found = true;
                }
                break;
            case USB_INTERFACE_ALT_SETTINGS_TYPE:
                if ((interfacePos->interface.info.interfaceIndex == queryPara.interfaceIndex)
                && (interfacePos->altSettingId == queryPara.altSettingId)) {
                    found = true;
                }
                break;
            default:
                break;
        }

        if (found == true) {
            IfInterfaceRefCount(interfacePos, queryPara.interfaceIndex, refCountFlag, claimFlag);
            break;
        }
    }
    OsalMutexUnlock(&interfacePool->interfaceLock);

    if (found == false) {
        HDF_LOGE("%{public}s:%{public}d the interface object to be find does not exist.",
                 __func__, __LINE__);
        return NULL;
    }

    if ((statusFlag == true) && (interfacePos->status == USB_INTERFACE_STATUS_REMOVE)) {
        HDF_LOGE("%{public}s:%{public}d status=%{public}d error.",
                 __func__, __LINE__, interfacePos->status);
        return NULL;
    }
    return interfacePos;
}

static struct UsbInterfacePool *IfFindInterfacePool(
    struct UsbSession *session, struct UsbPoolQueryPara queryPara, bool refCountFlag)
{
    struct UsbInterfacePool *interfacePoolPos = NULL;
    struct UsbInterfacePool *interfacePoolTemp = NULL;
    struct DListHead *ifacePoolList = NULL;
    bool found = false;

    if (session == NULL) {
        HDF_LOGE("%{public}s:%{public}d session is NULL", __func__, __LINE__);
        return NULL;
    }

    OsalMutexLock(&session->lock);
    ifacePoolList = &session->ifacePoolList;
    if (DListIsEmpty(ifacePoolList)) {
        OsalMutexUnlock(&session->lock);
        HDF_LOGE("%{public}s:%{public}d interface pool list is empty", __func__, __LINE__);
        return NULL;
    }

    DLIST_FOR_EACH_ENTRY_SAFE(interfacePoolPos, interfacePoolTemp, ifacePoolList, \
        struct UsbInterfacePool, object.entry) {
        switch (queryPara.type) {
            case USB_POOL_NORMAL_TYPE:
                if ((interfacePoolPos->busNum == queryPara.busNum) \
                    && (interfacePoolPos->devAddr == queryPara.usbAddr)) {
                    found = true;
                }
                break;
            case USB_POOL_OBJECT_ID_TYPE:
                if (interfacePoolPos->object.objectId == queryPara.objectId) {
                    found = true;
                }
                break;
            default:
                break;
        }

        if (found == true) {
            if (refCountFlag == true) {
                AdapterAtomicInc(&interfacePoolPos->refCount);
            }

            break;
        }
    }
    OsalMutexUnlock(&session->lock);

    if (found == false) {
        HDF_LOGE("%{public}s:%{public}d the interfacePool object to be find does not exist.",
                 __func__, __LINE__);
        return NULL;
    }

    return interfacePoolPos;
}

static int32_t IfGetRequestPipeType(
    struct UsbDeviceHandle *devHandle, uint8_t interfaceId, uint8_t pipeId, UsbPipeType *pipeType)
{
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbInterfaceQueryPara interfaceQueryPara;
    struct UsbSdkInterface *interfaceObj = NULL;
    struct UsbPipeQueryPara pipeQueryPara;
    struct UsbPipe *pipeObj = NULL;

    if (pipeType == NULL) {
        HDF_LOGE("%{public}s:%{public}d pipeType is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    /* Find interfacePool object */
    interfacePool = (struct UsbInterfacePool *)devHandle->dev->privateObject;
    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d get interfacePool faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    /* Find interface object */
    interfaceQueryPara.type = USB_INTERFACE_INTERFACE_INDEX_TYPE;
    interfaceQueryPara.interfaceIndex = interfaceId;
    interfaceObj = IfFindInterfaceObj(interfacePool, interfaceQueryPara, false, NULL, true);
    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfaceObj faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    /* Find pipe object */
    pipeQueryPara.type = USB_PIPE_INDEX_TYPE;
    pipeQueryPara.pipeId = pipeId;
    pipeObj = IfFindPipeObj(interfaceObj, pipeQueryPara);
    if (pipeObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindPipeObj faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    *pipeType = pipeObj->info.pipeType;

    return HDF_SUCCESS;
}

static int32_t IfFillControlRequest(struct UsbHostRequest *hostRequest,
    struct UsbDeviceHandle *devHandle, struct UsbRequestParams *params)
{
    struct UsbFillRequestData fillRequestData;
    struct UsbControlRequest ctrlReq = params->ctrlReq;
    unsigned char *setup = hostRequest->buffer;
    int32_t ret;

    ret = UsbProtocalFillControlSetup(setup, &ctrlReq);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d UsbProtocalFillControlSetup fail!", __func__, __LINE__);
        return ret;
    }
    if (ctrlReq.directon == USB_REQUEST_DIR_TO_DEVICE) {
        fillRequestData.endPoint = 0;
        if (ctrlReq.length > 0) {
            ret = memcpy_s(hostRequest->buffer + USB_RAW_CONTROL_SETUP_SIZE, ctrlReq.length,
                ctrlReq.buffer, ctrlReq.length);
            if (ret != EOK) {
                HDF_LOGE("%{public}s:%{public}d memcpy_s fail, ctrlReq.length=%{public}d",
                         __func__, __LINE__, ctrlReq.length);
                return ret;
            }
        }
    } else {
        fillRequestData.endPoint = (ctrlReq.directon  << USB_DIR_OFFSET);
    }
    /* fill control request */
    fillRequestData.length = USB_RAW_CONTROL_SETUP_SIZE + ctrlReq.length;
    fillRequestData.userCallback = params->callback;
    fillRequestData.callback     = UsbIoSetRequestCompletionInfo;
    fillRequestData.userData     = params->userData;
    fillRequestData.timeout      = params->timeout;

    return RawFillControlRequest(hostRequest, devHandle, &fillRequestData);
}

static int32_t IfFillIsoRequest(struct UsbHostRequest *hostRequest,
    struct UsbDeviceHandle *devHandle, struct UsbRequestParams *params)
{
    if (devHandle == NULL || params == NULL) {
        HDF_LOGE("%{public}s: invalid param", __func__);
        return HDF_ERR_INVALID_PARAM;
    }

    struct UsbFillRequestData fillRequestData;
    uint8_t pipeAddress = params->pipeAddress;
    struct UsbRequestParamsData requestData = params->dataReq;
    UsbRequestDirection dir = requestData.directon;

    fillRequestData.endPoint      = (dir << USB_DIR_OFFSET) | pipeAddress;
    fillRequestData.buffer        = requestData.buffer;
    fillRequestData.length        = requestData.length;
    fillRequestData.numIsoPackets = requestData.numIsoPackets;
    fillRequestData.userCallback  = params->callback;
    fillRequestData.callback      = UsbIoSetRequestCompletionInfo;
    fillRequestData.userData      = params->userData;
    fillRequestData.timeout       = params->timeout;

    return RawFillIsoRequest(hostRequest, devHandle, &fillRequestData);
}

static int32_t IfFillBulkRequest(struct UsbHostRequest *hostRequest,
    struct UsbDeviceHandle *devHandle, struct UsbRequestParams *params)
{
    struct UsbRequestParamsData requestData = params->dataReq;
    UsbRequestDirection dir = params->dataReq.directon;
    uint8_t pipeAddress = params->pipeAddress;

    if ((params->dataReq.directon == USB_REQUEST_DIR_TO_DEVICE) && (requestData.length > 0)) {
        int ret = memcpy_s(hostRequest->buffer, hostRequest->bufLen, requestData.buffer, requestData.length);
        if (ret != EOK) {
            HDF_LOGE("memcpy_s fail\n");
            return HDF_ERR_IO;
        }
    }
    hostRequest->devHandle    = devHandle;
    hostRequest->endPoint     = (dir << USB_DIR_OFFSET) | pipeAddress;
    hostRequest->requestType  = USB_PIPE_TYPE_BULK;
    hostRequest->timeout      = params->timeout;
    hostRequest->length       = requestData.length;
    hostRequest->userData     = params->userData;
    hostRequest->callback     = UsbIoSetRequestCompletionInfo;
    hostRequest->userCallback = params->callback;

    return HDF_SUCCESS;
}

static int32_t IfFillInterrupteRequest(struct UsbHostRequest *hostRequest,
    struct UsbDeviceHandle *devHandle, struct UsbRequestParams *params)
{
    struct UsbFillRequestData fillRequestData;
    uint8_t pipeAddress = params->pipeAddress;
    struct UsbRequestParamsData requestData = params->dataReq;
    UsbRequestDirection dir = requestData.directon;

    fillRequestData.endPoint = (dir << USB_DIR_OFFSET) | pipeAddress;
    fillRequestData.buffer   = requestData.buffer;
    fillRequestData.length   = requestData.length;
    fillRequestData.userCallback = params->callback;
    fillRequestData.callback = UsbIoSetRequestCompletionInfo;
    fillRequestData.userData = params->userData;
    fillRequestData.timeout  = params->timeout;

    return RawFillInterruptRequest(hostRequest, devHandle, &fillRequestData);
}

static int IfSubmitRequestToQueue(struct UsbIfRequest *requestObj)
{
    int ret;
    struct UsbHostRequest *hostRequest = NULL;
    struct UsbInterfacePool *interfacePool = NULL;

    if (requestObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d requestObj is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    hostRequest = requestObj->hostRequest;
    if (hostRequest == NULL) {
        HDF_LOGE("%{public}s:%{public}d hostRequest is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    interfacePool = (struct UsbInterfacePool *)hostRequest->devHandle->dev->privateObject;
    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d get interfacePool faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    ret = UsbIoSendRequest(&interfacePool->submitRequestQueue, hostRequest);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d UsbIoSendRequest faile, ret=%{public}d ",
                 __func__, __LINE__, ret);
        return ret;
    }

    return ret;
}

static int32_t IfFillRequestByPipeType(struct UsbIfRequest *requestObj, UsbPipeType pipeType,
    struct UsbHostRequest *hostRequest, struct UsbDeviceHandle *devHandle, struct UsbRequestParams *params)
{
    int32_t ret;

    switch (pipeType) {
        case USB_PIPE_TYPE_CONTROL:
            if (params->requestType != USB_REQUEST_PARAMS_CTRL_TYPE) {
                ret = HDF_ERR_INVALID_PARAM;
                HDF_LOGE("%{public}s:%{public}d params is not CTRL_TYPE", __func__, __LINE__);
                break;
            }

            requestObj->request.compInfo.type = USB_REQUEST_TYPE_DEVICE_CONTROL;

            ret = IfFillControlRequest(hostRequest, devHandle, params);
            break;
        case USB_PIPE_TYPE_ISOCHRONOUS:
            if (params->requestType != USB_REQUEST_PARAMS_DATA_TYPE) {
                ret = HDF_ERR_INVALID_PARAM;
                HDF_LOGE("%{public}s:%{public}d params is not DATA_TYPE", __func__, __LINE__);
                break;
            }

            ret = IfFillIsoRequest(hostRequest, devHandle, params);
            break;
        case USB_PIPE_TYPE_BULK:
            if (params->requestType != USB_REQUEST_PARAMS_DATA_TYPE) {
                ret = HDF_ERR_INVALID_PARAM;
                HDF_LOGE("%{public}s:%{public}d params is not DATA_TYPE", __func__, __LINE__);
                break;
            }

            ret = IfFillBulkRequest(hostRequest, devHandle, params);
            break;
        case USB_PIPE_TYPE_INTERRUPT:
            if (params->requestType != USB_REQUEST_PARAMS_DATA_TYPE) {
                ret = HDF_ERR_INVALID_PARAM;
                HDF_LOGE("%{public}s:%{public}d params is not DATA_TYPE", __func__, __LINE__);
                break;
            }

            ret = IfFillInterrupteRequest(hostRequest, devHandle, params);
            break;
        default:
            ret = HDF_FAILURE;
            break;
    }

    return ret;
}

static int IfDestoryDevice(struct UsbSession *session, struct UsbInterfacePool *interfacePool,
    struct UsbDeviceHandle *devHandle, bool refCountFlag)
{
    int ret;

    if ((session == NULL) || (interfacePool == NULL) || (devHandle == NULL)) {
        HDF_LOGE("%{public}s:%{public}d invalid param", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    OsalMutexLock(&session->lock);
    if (refCountFlag == true) {
        AdapterAtomicDec(&interfacePool->refCount);
    }

    if (OsalAtomicRead(&interfacePool->refCount) > 0) {
        OsalMutexUnlock(&session->lock);
        return HDF_SUCCESS;
    }

    ret = IfDestroyInterfacePool(interfacePool);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d destroy interface pool failed", __func__, __LINE__);
        OsalMutexUnlock(&session->lock);
        return ret;
    }
    OsalMutexUnlock(&session->lock);

    ret = RawCloseDevice(devHandle);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d close device failed", __func__, __LINE__);
    }

    return ret;
}

static struct UsbInterfacePool *IfGetInterfacePool(
    struct UsbDeviceHandle **devHandle, struct UsbSession *realSession, uint8_t busNum, uint8_t usbAddr)
{
    struct UsbPoolQueryPara poolQueryPara;
    struct UsbInterfacePool *interfacePool = NULL;
    int ret;

    *devHandle = RawOpenDevice(realSession, busNum, usbAddr);
    if (*devHandle == NULL) {
        HDF_LOGE("%{public}s:%{public}d RawOpenDevice faile", __func__, __LINE__);
        return NULL;
    }

    ret = UsbProtocalParseDescriptor(*devHandle, busNum, usbAddr);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d  faile, ret=%{public}d", __func__, __LINE__, ret);
        (void)RawCloseDevice(*devHandle);
        return NULL;
    }

    poolQueryPara.type = USB_POOL_NORMAL_TYPE;
    poolQueryPara.busNum = busNum;
    poolQueryPara.usbAddr = usbAddr;
    interfacePool = IfFindInterfacePool(realSession, poolQueryPara, true);
    if ((interfacePool == NULL) || (interfacePool->device == NULL)) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfacePool error", __func__, __LINE__);
        interfacePool = (struct UsbInterfacePool *)((*devHandle)->dev->privateObject);
        ret = IfDestoryDevice(realSession, interfacePool, *devHandle, false);
        return NULL;
    }

    return interfacePool;
}

int32_t UsbIfCreatPipeObj(struct UsbSdkInterface *interfaceObj, struct UsbPipe **pipeObj)
{
    struct UsbPipe *pipeObjTemp = NULL;
    static int32_t idNum = 0;

    pipeObjTemp = (struct UsbPipe *)OsalMemCalloc(sizeof(struct UsbPipe));
    if (pipeObjTemp == NULL) {
        return HDF_ERR_MALLOC_FAIL;
    }

    ++idNum;
    idNum %= INTERFACE_POOL_ID_MAX;
    pipeObjTemp->object.objectId = idNum;
    DListHeadInit(&pipeObjTemp->object.entry);

    OsalMutexLock(&interfaceObj->listLock);
    DListInsertTail(&pipeObjTemp->object.entry, &interfaceObj->pipeList);
    OsalMutexUnlock(&interfaceObj->listLock);

    IfPipeObjInit(pipeObjTemp);

    *pipeObj = pipeObjTemp;
    (*pipeObj)->info.interfaceId = interfaceObj->interface.info.interfaceIndex;
    return HDF_SUCCESS;
}

int32_t UsbIfCreatInterfaceObj(struct UsbInterfacePool *interfacePool, struct UsbSdkInterface **interfaceObj)
{
    struct UsbSdkInterface *interfaceObjTemp = NULL;
    static int32_t idNum = 0;

    interfaceObjTemp = (struct UsbSdkInterface *)OsalMemCalloc(sizeof(struct UsbSdkInterface));
    if (interfaceObjTemp == NULL) {
        return HDF_ERR_MALLOC_FAIL;
    }

    ++idNum;
    idNum %= INTERFACE_POOL_ID_MAX;
    interfaceObjTemp->interface.object.objectId = idNum;
    DListHeadInit(&interfaceObjTemp->interface.object.entry);
    IfInterfaceObjInit(interfaceObjTemp);
    interfaceObjTemp->parentObjectId = interfacePool->object.objectId;

    OsalMutexLock(&interfacePool->interfaceLock);
    DListInsertTail(&interfaceObjTemp->interface.object.entry, &interfacePool->interfaceList);
    OsalMutexUnlock(&interfacePool->interfaceLock);

    *interfaceObj = interfaceObjTemp;

    return HDF_SUCCESS;
}

HDF_STATUS UsbIfDestroyInterfaceObj(struct UsbInterfacePool *interfacePool, const struct UsbSdkInterface *interfaceObj)
{
    HDF_STATUS ret = HDF_SUCCESS;
    struct UsbSdkInterface *interfacePos = NULL;
    struct UsbSdkInterface *interfaceTemp = NULL;
    bool found = false;
    bool destroyFlag = false;

    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfacePool is NULL", __func__, __LINE__);
        return HDF_FAILURE;
    }

    if (DListIsEmpty(&interfacePool->interfaceList)) {
        HDF_LOGE("%{public}s:%{public}d interfaceList is empty ", __func__, __LINE__);
        return HDF_SUCCESS;
    }

    if (interfaceObj == NULL) {
        /* Destroy all interface object */
        destroyFlag = true;
    } else {
        /* Destroys the specified interface object */
        destroyFlag = false;
    }

    OsalMutexLock(&interfacePool->interfaceLock);
    DLIST_FOR_EACH_ENTRY_SAFE(interfacePos, interfaceTemp, &interfacePool->interfaceList, \
        struct UsbSdkInterface, interface.object.entry) {
        if ((destroyFlag == true) || ((destroyFlag == false) \
            && (interfacePos->interface.object.objectId == interfaceObj->interface.object.objectId))) {
            found = true;
            DListRemove(&interfacePos->interface.object.entry);
            ret = IfFreeInterfaceObj(interfacePos);
            if (ret != HDF_SUCCESS) {
                HDF_LOGE("%{public}s:%{public}d IfFreeInterfaceObj fail, ret=%{public}d ",
                    __func__, __LINE__, ret);
                break;
            }

            if (destroyFlag == false) {
                break;
            }
        }
    }
    OsalMutexUnlock(&interfacePool->interfaceLock);

    if (found == false) {
        ret = HDF_FAILURE;
        HDF_LOGE("%{public}s:%{public}d the interface object to be destroyed does not exist",
            __func__, __LINE__);
    }

    return ret;
}

int UsbIfCreatInterfacePool(struct UsbSession *session, uint8_t busNum, uint8_t devAddr,
                            struct UsbInterfacePool **interfacePool)
{
    struct UsbInterfacePool *interfacePoolTemp = NULL;
    static int32_t idNum = 0;

    interfacePoolTemp = (struct UsbInterfacePool *)OsalMemAlloc(sizeof(struct UsbInterfacePool));
    if (interfacePoolTemp == NULL) {
        HDF_LOGE("%{public}s:%{public}d OsalMemAlloc faile", __func__, __LINE__);
        *interfacePool = NULL;
        return HDF_ERR_MALLOC_FAIL;
    }

    ++idNum;
    idNum %= INTERFACE_POOL_ID_MAX;
    interfacePoolTemp->object.objectId = idNum;

    if (IfInterfacePoolInit(interfacePoolTemp, busNum, devAddr)) {
        OsalMemFree(interfacePoolTemp);
        *interfacePool = NULL;
        return HDF_ERR_IO;
    }
    OsalMutexLock(&session->lock);
    DListInsertTail(&interfacePoolTemp->object.entry, &session->ifacePoolList);
    OsalMutexUnlock(&session->lock);

    *interfacePool = interfacePoolTemp;

    return HDF_SUCCESS;
}

int32_t UsbInitHostSdk(struct UsbSession **session)
{
    return RawInit(session);
}

int32_t UsbExitHostSdk(struct UsbSession *session)
{
    return RawExit(session);
}

const struct UsbInterface *UsbClaimInterface(
    struct UsbSession *session, uint8_t busNum, uint8_t usbAddr, uint8_t interfaceIndex)
{
    struct UsbPoolQueryPara poolQueryPara;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbInterfaceQueryPara interfaceQueryPara;
    struct UsbSdkInterface *interfaceObj = NULL;
    struct UsbDeviceHandle *devHandle = NULL;
    struct UsbSession *realSession = RawGetSession(session);
    int ret;
    bool claimFlag = false;

    if (realSession == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfacePoolList is empty", __func__, __LINE__);
        return NULL;
    }

    poolQueryPara.type = USB_POOL_NORMAL_TYPE;
    poolQueryPara.busNum = busNum;
    poolQueryPara.usbAddr = usbAddr;
    interfacePool = IfFindInterfacePool(realSession, poolQueryPara, true);
    if ((interfacePool == NULL) || (interfacePool->device == NULL)) {
        interfacePool = IfGetInterfacePool(&devHandle, realSession, busNum, usbAddr);
        if ((interfacePool == NULL) || (interfacePool->device == NULL)) {
            HDF_LOGD("%{public}s:%{public}d interfacePool is NULL", __func__, __LINE__);
            return NULL;
        }
    }

    interfaceQueryPara.type = USB_INTERFACE_INTERFACE_INDEX_TYPE;
    interfaceQueryPara.interfaceIndex = interfaceIndex;
    interfaceObj = IfFindInterfaceObj(interfacePool, interfaceQueryPara, true, &claimFlag, false);
    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfaceObj failed", __func__, __LINE__);
        goto error;
    }

    if ((interfaceIndex != USB_CTRL_INTERFACE_ID) && (claimFlag == true)) {
        OsalMutexLock(&interfacePool->interfaceLock);
        devHandle = interfacePool->device->devHandle;
        ret = RawClaimInterface(devHandle, interfaceIndex);
        if (ret != HDF_SUCCESS) {
            HDF_LOGE("%{public}s:%{public}d RawClaimInterface faile, ret=%{public}d", __func__, __LINE__, ret);
            AdapterAtomicDec(&interfaceObj->refCount);
            OsalMutexUnlock(&interfacePool->interfaceLock);
            goto error;
        }
        OsalMutexUnlock(&interfacePool->interfaceLock);
    }
    interfaceObj->session = realSession;

    return (const struct UsbInterface *)interfaceObj;
error:
    (void)IfDestoryDevice(realSession, interfacePool, devHandle, true);
    return NULL;
}

int UsbReleaseInterface(const struct UsbInterface *interfaceObj)
{
    int ret;
    struct UsbPoolQueryPara queryPara;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbSdkInterface *interfaceSdk = (struct UsbSdkInterface *)interfaceObj;
    struct UsbDeviceHandle *devHandle = NULL;
    uint8_t interfaceIndex;

    if (interfaceSdk == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfaceObj is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    queryPara.type = USB_POOL_OBJECT_ID_TYPE;
    queryPara.objectId = interfaceSdk->parentObjectId;
    interfacePool = IfFindInterfacePool(interfaceSdk->session, queryPara, false);
    if ((interfacePool == NULL) || (interfacePool->session == NULL)) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfacePool faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    devHandle = interfacePool->device->devHandle;
    interfaceIndex = interfaceSdk->interface.info.interfaceIndex;
    OsalMutexLock(&interfacePool->interfaceLock);
    if ((interfaceIndex != USB_CTRL_INTERFACE_ID) && (AdapterAtomicDec(&interfaceSdk->refCount) <= 0)) {
        ret = RawReleaseInterface(devHandle, interfaceIndex);
        if ((ret != HDF_SUCCESS) && (ret != HDF_DEV_ERR_NO_DEVICE)) {
            HDF_LOGE("%{public}s:%{public}d RawReleaseInterface faile, ret=%{public}d", __func__, __LINE__, ret);
            AdapterAtomicInc(&interfaceSdk->refCount);
            OsalMutexUnlock(&interfacePool->interfaceLock);
            return ret;
        }
    }
    OsalMutexUnlock(&interfacePool->interfaceLock);

    return IfDestoryDevice(interfacePool->session, interfacePool, devHandle, true);
}

int UsbAddOrRemoveInterface(UsbInterfaceStatus status, struct UsbInterface *interfaceObj)
{
    int ret;
    struct UsbSdkInterface *interfaceSdk = (struct UsbSdkInterface *)interfaceObj;
    struct UsbPoolQueryPara queryPara;
    struct UsbInterfacePool *interfacePool = NULL;
    enum UsbPnpNotifyServiceCmd cmdType;
    struct UsbPnpAddRemoveInfo infoData;

    if (interfaceSdk == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfaceObj is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    if (interfaceSdk->status == status) {
        HDF_LOGE("%{public}s:%{public}d interfaceSdk->status=%{public}d is error",
                 __func__, __LINE__, interfaceSdk->status);
        return HDF_ERR_INVALID_PARAM;
    }

    /* Find interfacePool object */
    queryPara.type = USB_POOL_OBJECT_ID_TYPE;
    queryPara.objectId = interfaceSdk->parentObjectId;
    interfacePool = IfFindInterfacePool(interfaceSdk->session, queryPara, false);
    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfacePool faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    if (status == USB_INTERFACE_STATUS_ADD) {
        cmdType = USB_PNP_NOTIFY_ADD_INTERFACE;
    } else if (status == USB_INTERFACE_STATUS_REMOVE) {
        cmdType = USB_PNP_NOTIFY_REMOVE_INTERFACE;
    } else {
        HDF_LOGE("%{public}s:%{public}d status=%{public}d is not define",
                 __func__, __LINE__, status);
        return HDF_ERR_INVALID_PARAM;
    }

    infoData.devNum = interfacePool->devAddr;
    infoData.busNum = interfacePool->busNum;
    infoData.interfaceNumber = interfaceObj->info.interfaceIndex;
    infoData.interfaceClass = interfaceObj->info.interfaceClass;
    infoData.interfaceSubClass = interfaceObj->info.interfaceSubClass;
    infoData.interfaceProtocol = interfaceObj->info.interfaceProtocol;
    ret = RawInitPnpService(cmdType, infoData);
    if (ret == HDF_SUCCESS) {
        interfaceSdk->status = status;
    }

    return ret;
}

UsbInterfaceHandle *UsbOpenInterface(struct UsbInterface *interfaceObj)
{
    struct UsbPoolQueryPara poolQueryPara;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbInterfaceHandleEntity *ifaceHdl = NULL;

    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s: invalid param", __func__);
        return NULL;
    }

    struct UsbSdkInterface *interfaceSdk = (struct UsbSdkInterface *)interfaceObj;
    if (interfaceSdk->status == USB_INTERFACE_STATUS_REMOVE) {
        HDF_LOGE("%{public}s:%{public}d interfaceSdk->status=%{public}d is error",
            __func__, __LINE__, interfaceSdk->status);
        return NULL;
    }

    poolQueryPara.type = USB_POOL_OBJECT_ID_TYPE;
    poolQueryPara.objectId = interfaceSdk->parentObjectId;
    interfacePool = IfFindInterfacePool(interfaceSdk->session, poolQueryPara, false);
    if ((interfacePool  == NULL) || (interfacePool->device == NULL)
        || (interfacePool->device->devHandle == NULL)) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfacePool faile", __func__, __LINE__);
        return NULL;
    }

    OsalMutexLock(&interfacePool->interfaceLock);
    ifaceHdl = OsalMemCalloc(sizeof(struct UsbInterfaceHandleEntity));
    if (ifaceHdl == NULL) {
        HDF_LOGE("%{public}s:%{public}d OsalMemAlloc failed", __func__, __LINE__);
        goto out;
    }
    ifaceHdl->devHandle = interfacePool->device->devHandle;
    ifaceHdl->interfaceIndex = interfaceSdk->interface.info.interfaceIndex;

    if (OsalAtomicRead(&interfacePool->ioRefCount) == 0) {
        HDF_STATUS ret = UsbIoStart(interfacePool);
        if (ret != HDF_SUCCESS) {
            HDF_LOGE("%{public}s:%{public}d UsbIoStart faile, ret=%{public}d ", __func__, __LINE__, ret);
            ifaceHdl->devHandle = NULL;
            OsalMemFree(ifaceHdl);
            goto out;
        }
    }
    AdapterAtomicInc(&interfaceSdk->refCount);
    AdapterAtomicInc(&interfacePool->ioRefCount);
    OsalMutexUnlock(&interfacePool->interfaceLock);

    return (UsbInterfaceHandle *)ifaceHdl;

out:
    OsalMutexUnlock(&interfacePool->interfaceLock);
    return NULL;
}

int32_t UsbCloseInterface(UsbInterfaceHandle *interfaceHandle)
{
    HDF_STATUS ret;
    struct UsbInterfaceHandleEntity *ifaceHdl = NULL;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbInterfaceQueryPara interfaceQueryPara;
    struct UsbSdkInterface *interfaceObj = NULL;

    if (interfaceHandle == NULL) {
        HDF_LOGE("%{public}s:%{public}d handle is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;
    if ((ifaceHdl->devHandle == NULL) || (ifaceHdl->devHandle->dev == NULL)
        || (ifaceHdl->devHandle->dev->privateObject == NULL)) {
        HDF_LOGE("%{public}s:%{public}d ifaceHdl is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    interfacePool = (struct UsbInterfacePool *)ifaceHdl->devHandle->dev->privateObject;
    /* Find interface object */
    interfaceQueryPara.type = USB_INTERFACE_INTERFACE_INDEX_TYPE;
    interfaceQueryPara.interfaceIndex = ifaceHdl->interfaceIndex;
    interfaceObj = IfFindInterfaceObj(interfacePool, interfaceQueryPara, false, NULL, false);
    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfaceObj faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    OsalMutexLock(&interfacePool->interfaceLock);
    if (AdapterAtomicDec(&interfacePool->ioRefCount) == 0) {
        ret = UsbIoStop(interfacePool);
        if (ret != HDF_SUCCESS) {
            HDF_LOGE("%{public}s:%{public}d UsbIoStop failed, ret=%{public}d ",
                __func__, __LINE__, ret);
            goto out;
        }
    }
    AdapterAtomicDec(&interfaceObj->refCount);
    ifaceHdl->devHandle = NULL;
    OsalMemFree(ifaceHdl);
    OsalMutexUnlock(&interfacePool->interfaceLock);

    return HDF_SUCCESS;
out:
    AdapterAtomicInc(&interfacePool->ioRefCount);
    OsalMutexUnlock(&interfacePool->interfaceLock);
    return ret;
}

int32_t UsbSelectInterfaceSetting(
    UsbInterfaceHandle *interfaceHandle, uint8_t settingIndex, struct UsbInterface **interfaceObj)
{
    struct UsbInterfaceHandleEntity *ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbSdkInterface *interfacePos = NULL;
    struct UsbSdkInterface *interfaceTemp = NULL;
    struct UsbInterfaceQueryPara interfaceQueryPara;
    int32_t ret;

    if ((interfaceHandle == NULL) || (interfaceObj == NULL)) {
        HDF_LOGE("%{public}s:%{public}d handle is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }
    interfacePool = (struct UsbInterfacePool *)ifaceHdl->devHandle->dev->privateObject;
    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfacePool is NULL", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    ret = RawSetInterfaceAltsetting((struct UsbDeviceHandle *)ifaceHdl->devHandle, ifaceHdl->interfaceIndex, settingIndex);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d RawEnableInterface faile, ret=%{public}d", __func__, __LINE__, ret);
        return ret;
    }

    OsalMutexLock(&interfacePool->interfaceLock);
    DLIST_FOR_EACH_ENTRY_SAFE(interfacePos, interfaceTemp, &interfacePool->interfaceList,
                              struct UsbSdkInterface, interface.object.entry) {
        if (interfacePos->interface.info.interfaceIndex == ifaceHdl->interfaceIndex) {
            interfacePos->interface.info.curAltSetting = settingIndex;
        }
    }
    OsalMutexUnlock(&interfacePool->interfaceLock);

    interfaceQueryPara.type = USB_INTERFACE_INTERFACE_INDEX_TYPE;
    interfaceQueryPara.interfaceIndex = ifaceHdl->interfaceIndex;
    interfaceTemp = IfFindInterfaceObj(interfacePool, interfaceQueryPara, false, NULL, true);
    if (interfaceTemp == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfaceObj failed", __func__, __LINE__);
        return HDF_FAILURE;
    }
    interfaceTemp->session = interfacePool->session;

    *interfaceObj = &interfaceTemp->interface;

    return HDF_SUCCESS;
}

int32_t UsbGetPipeInfo(UsbInterfaceHandle *interfaceHandle, uint8_t altSettingIndex,
    uint8_t pipeId, struct UsbPipeInfo *pipeInfo)
{
    struct UsbInterfaceHandleEntity *ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;
    struct UsbInterfacePool *interfacePool = NULL;
    struct UsbInterfaceQueryPara interfaceQueryPara;
    struct UsbSdkInterface *interfaceObj = NULL;
    struct UsbPipeQueryPara pipeQueryPara;
    struct UsbPipe *pipeObj = NULL;

    if ((interfaceHandle == NULL) || (pipeInfo == NULL) || (ifaceHdl == NULL)
        || (ifaceHdl->devHandle == NULL) || (ifaceHdl->devHandle->dev == NULL)) {
        HDF_LOGE("%{public}s:%{public}d invalid parameter", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    /* Find interfacePool object */
    interfacePool = (struct UsbInterfacePool *)ifaceHdl->devHandle->dev->privateObject;
    if (interfacePool == NULL) {
        HDF_LOGE("%{public}s:%{public}d interfacePool is NULL", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    /* Find interface object */
    interfaceQueryPara.type = USB_INTERFACE_ALT_SETTINGS_TYPE;
    interfaceQueryPara.interfaceIndex = ifaceHdl->interfaceIndex;
    interfaceQueryPara.altSettingId = altSettingIndex;
    interfaceObj = IfFindInterfaceObj(interfacePool, interfaceQueryPara, false, NULL, true);
    if (interfaceObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindInterfaceObj faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    /* Find pipe object */
    pipeQueryPara.type = USB_PIPE_INDEX_TYPE;
    pipeQueryPara.pipeId = pipeId;
    pipeObj = IfFindPipeObj(interfaceObj, pipeQueryPara);
    if (pipeObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d IfFindPipeObj faile", __func__, __LINE__);
        return HDF_ERR_BAD_FD;
    }

    *pipeInfo = pipeObj->info;

    return HDF_SUCCESS;
}

int32_t UsbClearInterfaceHalt(UsbInterfaceHandle *interfaceHandle, uint8_t pipeAddress)
{
    struct UsbInterfaceHandleEntity *ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;

    if (ifaceHdl == NULL) {
        HDF_LOGE("%{public}s:%{public}d handle is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    return RawClearHalt(ifaceHdl->devHandle, pipeAddress);
}

struct UsbRequest *UsbAllocRequest(UsbInterfaceHandle *interfaceHandle, int isoPackets, int length)
{
    struct UsbInterfaceHandleEntity *ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;
    struct UsbIfRequest *requestObj = NULL;
    struct UsbHostRequest *hostRequest = NULL;

    if (ifaceHdl == NULL) {
        HDF_LOGE("%{public}s:%{public}d handle is NULL ", __func__, __LINE__);
        return NULL;
    }

    requestObj = (struct UsbIfRequest *)OsalMemCalloc(sizeof(struct UsbIfRequest));
    if (requestObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d OsalMemCalloc UsbRequest error. ", __func__, __LINE__);
        return NULL;
    }

    hostRequest = RawAllocRequest(ifaceHdl->devHandle, isoPackets, length);
    if (hostRequest == NULL) {
        HDF_LOGE("%{public}s:%{public}d RawAllocRequest UsbHostRequest error. ", __func__, __LINE__);
        OsalMemFree(requestObj);
        return NULL;
    }
    hostRequest->devHandle = ifaceHdl->devHandle;

    ++g_usbRequestObjectId;
    g_usbRequestObjectId %= MAX_OBJECT_ID;
    requestObj->request.object.objectId = g_usbRequestObjectId;
    DListHeadInit(&requestObj->request.object.entry);
    OsalMutexInit(&requestObj->mutex);
    requestObj->request.compInfo.type = USB_REQUEST_TYPE_INVALID;
    requestObj->request.compInfo.buffer = hostRequest->buffer;
    requestObj->request.compInfo.length = hostRequest->length;
    requestObj->hostRequest = hostRequest;
    requestObj->isSyncReq = false;
    hostRequest->privateObj = requestObj;

    /* Init request semaphore */
    if (OsalSemInit(&requestObj->sem, 0) != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d OsalSemInit faile! ", __func__, __LINE__);
        if (UsbFreeRequest((struct UsbRequest *)requestObj) != HDF_SUCCESS) {
            HDF_LOGE("%{public}s:%{public}d UsbFreeRequest faile! ", __func__, __LINE__);
        }
        return NULL;
    }

    return (struct UsbRequest *)requestObj;
}

int UsbFreeRequest(struct UsbRequest *request)
{
    struct UsbHostRequest *hostRequest = NULL;
    struct UsbIfRequest *requestObj = (struct UsbIfRequest *)request;
    int ret;

    if (requestObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d request is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    hostRequest = requestObj->hostRequest;
    if (hostRequest == NULL) {
        HDF_LOGE("%{public}s:%{public}d hostRequest is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    ret = RawFreeRequest(hostRequest);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d RawFreeRequest faile, ret=%{public}d", __func__, __LINE__, ret);
        return ret;
    }

    OsalSemDestroy(&requestObj->sem);
    OsalMemFree(requestObj);

    return ret;
}

int UsbSubmitRequestAsync(struct UsbRequest *request)
{
    if (request == NULL) {
        HDF_LOGE("%{public}s:%{public}d request is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    struct UsbIfRequest *requestObj = (struct UsbIfRequest *)request;
    requestObj->isSyncReq = false;
    return IfSubmitRequestToQueue(requestObj);
}

int32_t UsbFillRequest(struct UsbRequest *request, UsbInterfaceHandle *interfaceHandle,
    struct UsbRequestParams *params)
{
    struct UsbInterfaceHandleEntity *ifaceHdl = (struct UsbInterfaceHandleEntity *)interfaceHandle;
    struct UsbIfRequest *requestObj = (struct UsbIfRequest *)request;
    struct UsbHostRequest *hostRequest = NULL;
    UsbPipeType pipeType;
    UsbRequestDirection directon;
    int32_t ret;

    if ((requestObj == NULL) || (params == NULL)) {
        HDF_LOGE("%{public}s:%{public}d params or request is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    hostRequest = requestObj->hostRequest;
    if (hostRequest == NULL) {
        HDF_LOGE("%{public}s:%{public}d hostRequest is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }
    ret = IfGetRequestPipeType(ifaceHdl->devHandle, params->interfaceId, \
        params->pipeId, &pipeType);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d IfGetRequestPipeType error, ret=%{public}d", __func__, __LINE__, ret);
        return ret;
    }

    ret = IfFillRequestByPipeType(requestObj, pipeType, hostRequest, ifaceHdl->devHandle, params);
    if (params->requestType == USB_REQUEST_PARAMS_DATA_TYPE) {
        directon = (params->pipeAddress >> USB_DIR_OFFSET) & 0x01;
        if (directon == USB_REQUEST_DIR_TO_DEVICE) {
            requestObj->request.compInfo.type = USB_REQUEST_TYPE_PIPE_WRITE;
        } else if (directon == USB_REQUEST_DIR_FROM_DEVICE) {
            requestObj->request.compInfo.type = USB_REQUEST_TYPE_PIPE_READ;
        }
    }

    return ret;
}

int UsbCancelRequest(struct UsbRequest *request)
{
    int ret;
    struct UsbHostRequest *hostRequest = NULL;
    struct UsbIfRequest *requestObj = (struct UsbIfRequest *)request;

    if (requestObj == NULL) {
        HDF_LOGE("%{public}s:%{public}d request is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    hostRequest = requestObj->hostRequest;
    ret = RawCancelRequest(hostRequest);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d RawCancelRequest faile, ret=%{public}d ",
            __func__, __LINE__, ret);
        return ret;
    }

    requestObj->request.compInfo.status = USB_REQUEST_CANCELLED;

    return HDF_SUCCESS;
}

int UsbSubmitRequestSync(struct UsbRequest *request)
{
    int ret;

    if (request == NULL) {
        HDF_LOGE("%{public}s:%{public}d request is NULL", __func__, __LINE__);
        return HDF_ERR_INVALID_PARAM;
    }

    struct UsbIfRequest *requestObj = (struct UsbIfRequest *)request;
    requestObj->isSyncReq = true;

    ret = IfSubmitRequestToQueue(requestObj);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d IfSubmitRequestToQueue faile, ret=%{public}d ",
            __func__, __LINE__, ret);
        return ret;
    }

    ret = OsalSemWait(&requestObj->sem, HDF_WAIT_FOREVER);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%{public}s:%{public}d OsalSemWait faile, ret=%{public}d ", __func__, __LINE__, ret);
    }

    return ret;
}

