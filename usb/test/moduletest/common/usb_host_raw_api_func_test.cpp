/*
 * UsbHostRawApiFuncTest.cpp
 *
 * usb serial device function test source file
 *
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <gtest/gtest.h>
#include "securec.h"
#include "usb_utils.h"

using namespace std;
using namespace testing::ext;

namespace {
const string RLOG_FILE = "/data/acm_read_xts";

class UsbHostRawApiFuncTest : public testing::Test {
protected:
    static void SetUpTestCase(void)
    {
        printf("------start UsbHostRawApiFuncTest------\n");
        system("cat /dev/null > /data/acm_write_xts");
        system("cat /dev/null > /data/acm_read_xts");
    }
    static void TearDownTestCase(void)
    {
        printf("------end UsbHostRawApiFuncTest------\n");
    }
};

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_001，H_Lx_H_Sub_usb_IO read_007
 * @tc.name      : USB串口同步数据读写
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 1
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteSync_001, TestSize.Level1)
{
    printf("------start CheckRawApiWriteSync_001------\n");
    const string data = "abc";
    double startTs = GetNowTs();
    string wlog, rlog;
    wlog = "send data[" + data + "] to device";
    rlog = "recv data[" + data + "] from device";
    ASSERT_EQ(system("hostacm_moduletest -RAW -syncRead &"), 0);
    ASSERT_EQ(system(("hostacm_moduletest -RAW -syncWrite '" + data + "'").c_str()), 0);
    sleep(3);
    EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find sync write log";
    EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find sync recv log";
    printf("------end CheckRawApiWriteSync_001------\n");
}

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_001，H_Lx_H_Sub_usb_IO read_007
 * @tc.name      : USB串口同步数据读写
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 1
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteSync_002, TestSize.Level1)
{
    printf("------start CheckRawApiWriteSync_002------\n");
    const string data[] = {
        "0123456789",
        "Z",
        "0!a@1#b$2%c^3&D*4(E)5-F_",
        ""
    };
    double startTs = GetNowTs();
    string wlog, rlog;
    for (int i = 0; data[i].size() > 0; i++) {
        wlog = "send data[" + data[i] + "] to device";
        rlog = "recv data[" + data[i] + "] from device";
        ASSERT_EQ(system("hostacm_moduletest -RAW -syncRead &"), 0);
        ASSERT_EQ(system(("hostacm_moduletest -RAW -syncWrite '" + data[i] + "'").c_str()), 0);
        sleep(3);
        EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find sync write log";
        EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find sync recv log";
    }
    printf("------end CheckRawApiWriteSync_002------\n");
}

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_003, H_Lx_H_Sub_usb_IO read_009
 * @tc.name      : USB串口同步读写1KB数据
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 2
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteSync_003, TestSize.Level2)
{
    printf("------start CheckRawApiWriteSync_003------\n");
    const string s = "0123456789abcdef";
    string data;
    int totalSize = 1024;
    int writeCnt = 8;
    unsigned int n = 0;
    while (n < totalSize / writeCnt / s.size()) {
        data += s;
        n++;
    }
    const string wlog = "send data[" + data + "] to device";
    const string rlog = "recv data[" + data + "] from device";
    double startTs;
    for (int i = 0; i < writeCnt; i++) {
        startTs = GetNowTs();
        ASSERT_EQ(system("hostacm_moduletest -RAW -syncRead &"), 0);
        ASSERT_EQ(system(("hostacm_moduletest -RAW -syncWrite '" + data + "'").c_str()), 0);
        sleep(3);
        EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find sync write log";
        EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find sync recv log";
    }
    printf("------end CheckRawApiWriteSync_003------\n");
}

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_013， H_Lx_H_Sub_usb_IO read_017
 * @tc.name      : USB串口异步数据读写
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 1
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteAsync_001, TestSize.Level1)
{
    printf("------start CheckRawApiWriteAsync_001------\n");
    ASSERT_EQ(system("hostacm_moduletest -RAW -asyncRead &"), 0) << \
    "ErrInfo:  failed to start async read";
    sleep(3);
    const string data = "abc";
    double startTs = GetNowTs();
    string wlog, rlog;
    wlog = "send data[" + data + "] to device";
    rlog = "recv data[" + data + "] from device";
    ASSERT_EQ(system(("hostacm_moduletest -RAW -asyncWrite '" + data + "'").c_str()), 0);
    sleep(3);
    EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find async write log";
    EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find async recv log";
    printf("------end CheckRawApiWriteAsync_001------\n");
}

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_013， H_Lx_H_Sub_usb_IO read_017
 * @tc.name      : USB串口异步数据读写
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 1
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteAsync_002, TestSize.Level1)
{
    printf("------start CheckRawApiWriteAsync_002------\n");
    const string data[] = {
        "0123456789",
        "Z",
        "0!a@1#b$2%c^3&D*4(E)5-F_",
        ""
    };
    double startTs = GetNowTs();
    string wlog, rlog;
    for (int i = 0; data[i].size() > 0; i++) {
        wlog = "send data[" + data[i] + "] to device";
        rlog = "recv data[" + data[i] + "] from device";
        ASSERT_EQ(system(("hostacm_moduletest -RAW -asyncWrite '" + data[i] + "'").c_str()), 0);
        sleep(3);
        EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find async write log";
        EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find async recv log";
    }
    printf("------end CheckRawApiWriteAsync_002------\n");
}

/**
 * @tc.number    : H_Lx_H_Sub_usb_IO read_019
 * @tc.name      : USB串口异步写1KB数据
 * @tc.size      : MEDIUM
 * @tc.type      : FUNC
 * @tc.level     : Level 2
 */
HWTEST_F(UsbHostRawApiFuncTest, CheckRawApiWriteAsync_003, TestSize.Level2)
{
    printf("------start CheckRawApiWriteAsync_003------\n");
    const string s = "0123456789abcdef";
    string data;
    int totalSize = 1024;
    int writeCnt = 8;
    unsigned int n = 0;
    while (n < totalSize / writeCnt / s.size()) {
        data += s;
        n++;
    }
    const string wlog = "send data[" + data + "] to device";
    const string rlog = "recv data[" + data + "] from device";
    double startTs;
    for (int i = 0; i < writeCnt; i++) {
        startTs = GetNowTs();
        ASSERT_EQ(system(("hostacm_moduletest -RAW -asyncWrite '" + data + "'").c_str()), 0);
        sleep(3);
        EXPECT_TRUE(HasLog(wlog, startTs)) << "ErrInfo: cannot find async write log";
        EXPECT_TRUE(HasLog(rlog, startTs, RLOG_FILE)) << "ErrInfo: cannot find async recv log";
    }
    ASSERT_EQ(system("killall hostacm_moduletest"), 0) << "ErrInfo:  failed to kill async read";
    printf("------end CheckRawApiWriteAsync_003------\n");
}
}