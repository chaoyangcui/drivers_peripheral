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

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "hdf_io_service_if.h"
#include "hdf_log.h"
#include "osal_mem.h"
#include "osal_mutex.h"
#include "osal_thread.h"
#include "osal_time.h"
#include "securec.h"

#define HDF_LOG_TAG hcs_prop
#define OPTION_EDN (-1)
#define ACM_SERVICE_NAME "usbfn_cdcacm"
enum UsbSerialCmd {
    USB_SERIAL_OPEN = 0,
    USB_SERIAL_CLOSE,
    USB_SERIAL_READ,
    USB_SERIAL_WRITE,
    USB_SERIAL_GET_BAUDRATE,
    USB_SERIAL_SET_BAUDRATE,
    USB_SERIAL_SET_PROP,
    USB_SERIAL_GET_PROP,
    USB_SERIAL_REGIST_PROP,
};

static struct HdfSBuf *g_data;
static struct HdfSBuf *g_reply;
static struct HdfIoService *g_acmService;

static void ShowUsage()
{
    HDF_LOGE("Usage options:\n");
    HDF_LOGE("-g : name of getting prop, as: -g idProduct");
    HDF_LOGE("-s : name of setting prop, as: -s idProduct 0xa4b7");
    HDF_LOGE("-r : regist prop, as: -r testa aaaaa");
    HDF_LOGE("-h : show this help message");
}

static int DispatcherInit(void)
{
    g_acmService = HdfIoServiceBind("usbfn_cdcacm");
    if (g_acmService == NULL) {
        HDF_LOGE("%{public}s: GetService err", __func__);
        return HDF_FAILURE;
    }

    g_data = HdfSBufObtainDefaultSize();
    g_reply = HdfSBufObtainDefaultSize();
    if (g_data == NULL || g_reply == NULL) {
        HDF_LOGE("%{public}s: GetService err", __func__);
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

static void DispatcherDeInit(void)
{
    HdfSBufRecycle(g_data);
    HdfSBufRecycle(g_reply);
}

static int TestPropGet(const char *propName)
{
    int status = -1;
    const char *propVal = NULL;
    if (!HdfSbufWriteString(g_data, propName)) {
        HDF_LOGE("%{public}s:failed to write result", __func__);
        goto FAIL;
    }
    status = g_acmService->dispatcher->Dispatch(&g_acmService->object, USB_SERIAL_GET_PROP, g_data, g_reply);
    if (status !=  HDF_SUCCESS) {
        HDF_LOGE("%{public}s: Dispatch USB_SERIAL_GET_PROP failed status = %{public}d", __func__, status);
        goto FAIL;
    }
    propVal = HdfSbufReadString(g_reply);
    if (propVal == NULL) {
        HDF_LOGE("%{public}s:failed to write result", __func__);
        goto FAIL;
    }
    printf("%s: %s = %s\n", __func__, propName, propVal);

FAIL:
    return status;
}

static int TestPropSet(const char *propName, const char *propValue)
{
    int status = -1;
    if (!HdfSbufWriteString(g_data, propName)) {
        HDF_LOGE("%{public}s:failed to write propName : %{public}s", __func__, propName);
        goto FAIL;
    }
    if (!HdfSbufWriteString(g_data, propValue)) {
        HDF_LOGE("%{public}s:failed to write propValue : %{public}s", __func__, propValue);
        goto FAIL;
    }
    status = g_acmService->dispatcher->Dispatch(&g_acmService->object, USB_SERIAL_SET_PROP, g_data, g_reply);
    if (status !=  HDF_SUCCESS) {
        HDF_LOGE("%{public}s: Dispatch USB_SERIAL_SET_PROP failed", __func__);
    }
 FAIL:
    return status;
}

static int TestPropRegist(const char *propName, const char *propValue)
{
    int status;

    status = g_acmService->dispatcher->Dispatch(&g_acmService->object, USB_SERIAL_OPEN, g_data, g_reply);
    if (status) {
        HDF_LOGE("%{public}s: Dispatch USB_SERIAL_OPEN err", __func__);
        return HDF_FAILURE;
    }
    if (!HdfSbufWriteString(g_data, propName)) {
    HDF_LOGE("%{public}s:failed to write propName : %{public}s", __func__, propName);
    goto FAIL;
    }
    if (!HdfSbufWriteString(g_data, propValue)) {
    HDF_LOGE("%{public}s:failed to write propValue : %{public}s", __func__, propValue);
    goto FAIL;
    }
    status = g_acmService->dispatcher->Dispatch(&g_acmService->object, USB_SERIAL_REGIST_PROP, g_data, g_reply);
    if (status !=  HDF_SUCCESS) {
    HDF_LOGE("%{public}s: Dispatch USB_SERIAL_SET_PROP failed status = %{public}d", __func__, status);
    }
 FAIL:
    status = g_acmService->dispatcher->Dispatch(&g_acmService->object, USB_SERIAL_CLOSE, g_data, g_reply);
    if (status) {
        HDF_LOGE("%{public}s: Dispatch USB_SERIAL_CLOSE err", __func__);
        return HDF_FAILURE;
    }

    return status;
}


int main(int argc, char *argv[])
{
    int ch;
    int ret;
    const char *serviceName = NULL;
    const char *propName = NULL;
    const char *propValue = NULL;
    bool setProp = false;
    bool getProp = false;
    bool registProp = false;

    while ((ch = getopt(argc, argv, "S:r:g:s:h?")) != OPTION_EDN) {
        switch (ch) {
            case 'S':
                serviceName = optarg;
                break;
            case 'r':
                propName = optarg;
                propValue = argv[optind];
                registProp = true;
                break;
            case 'g':
                propName = optarg;
                getProp = true;
                break;
            case 's':
                propName = optarg;
                propValue = argv[optind];
                setProp = true;
                break;
            case 'h':
            case '?':
                ShowUsage();
                return 0;
                break;
            default:
                break;
        }
    }

    if (DispatcherInit() != HDF_SUCCESS) {
        return 1;
    }
    if (getProp) {
        ret = TestPropGet(propName);
    } else if (setProp) {
    	ret = TestPropSet(propName, propValue);
    } else if (registProp) {
        ret = TestPropRegist(propName, propValue);
    }
    DispatcherDeInit();

    return 0;
}

