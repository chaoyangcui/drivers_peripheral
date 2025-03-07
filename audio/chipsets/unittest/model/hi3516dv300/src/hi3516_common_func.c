/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */


#include "hi3516_common_func.h"

int32_t GetAudioCard(struct AudioCard **card, AudioType *type)
{
    int i;
    const char *audioServiceName[] = {
        "hdf_audio_codec_dev0",
        "hdf_audio_smartpa_dev0",
    };
    HDF_LOGI("%s: enter", __func__);
    if (card == NULL || type == NULL) {
        HDF_LOGE("input param is NULL");
        return HDF_FAILURE;
    }

    for (i = 0; i < sizeof(audioServiceName) / sizeof(audioServiceName[0]); ++i) {
        if (GetCardInstance(audioServiceName[i]) != NULL) {
            HDF_LOGI("%s: get %s success!", __func__, audioServiceName[i]);
            if (i == 0) {
                *type = INNER;
            } else if (i == 1) {
                *type = OUTER;
            }
            *card = GetCardInstance(audioServiceName[i]);
            break;
        }
    }

    if (*card == NULL) {
        HDF_LOGE("get card instance failed");
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}
