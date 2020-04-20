#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "MvCameraControl.h"

bool g_bExit = false;
bool g_bSave = false;
unsigned int g_nTotalHeight = 0;
unsigned int g_nPayloadSize = 0;

// ch:等待按键输入 | en:Wait for key press
void WaitForKeyPress(void)
{
    int c;
    while ( (c = getchar()) != '\n' && c != EOF );

    g_bExit = true;
    sleep(1);
}

void SkipEnter()
{
    int c;
    while ( (c = getchar()) != '\n' && c != EOF );
}

bool PrintDeviceInfo(MV_CC_DEVICE_INFO* pstMVDevInfo)
{
    if (NULL == pstMVDevInfo)
    {
        printf("The Pointer of pstMVDevInfo is NULL!\n");
        return false;
    }
    if (pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE)
    {
        int nIp1 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
        int nIp2 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
        int nIp3 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
        int nIp4 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);

        // ch:打印当前相机ip和用户自定义名字 | en:print current ip and user defined name
        printf("CurrentIp: %d.%d.%d.%d\n" , nIp1, nIp2, nIp3, nIp4);
        printf("UserDefinedName: %s\n\n" , pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName);
    }
    else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE)
    {
        printf("UserDefinedName: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName);
        printf("Serial Number: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
        printf("Device Number: %d\n\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.nDeviceNumber);
    }
    else
    {
        printf("Not support.\n");
    }

    return true;
}

static  void* WorkThread(void* pUser)
{
    int nRet = MV_OK;

    MV_FRAME_OUT stOutFrame = {0};
    memset(&stOutFrame, 0, sizeof(MV_FRAME_OUT));
    MVCC_INTVALUE stWidth = {0};
    MVCC_INTVALUE stHeight = {0};
    MVCC_ENUMVALUE stPixelType = {0};
    
    nRet = MV_CC_GetIntValue(pUser, "Width", &stWidth);
    if(MV_OK != nRet)
    {
        printf("Get Image Width Failed, nRet[%x]\n", nRet);
    }
    nRet = MV_CC_GetIntValue(pUser, "Height", &stHeight);
    if(MV_OK != nRet)
    {
        printf("Get Image Height Failed, nRet[%x]\n", nRet);
    }
    nRet = MV_CC_GetEnumValue(pUser, "PixelFormat", &stPixelType);
    if(MV_OK != nRet)
    {
        printf("Get Image PixelFormat Failed, nRet[%x]\n", nRet);
    }
    
    //计算实际的拼图图像高度，可以被整除则取60000，不能被整除则高度为60000+
    unsigned int nNeedHeight = 0;
    if(0 == g_nTotalHeight%stHeight.nCurValue)
    {
        nNeedHeight = g_nTotalHeight;
    }
    else
    {
        nNeedHeight = g_nTotalHeight / stHeight.nCurValue * stHeight.nCurValue + stHeight.nCurValue;
    }
    unsigned int nStride = (((stPixelType.nCurValue) >> 16) & 0x000000ff) >> 3;
    unsigned int nNeedSize = stWidth.nCurValue * nNeedHeight * nStride;
    unsigned char* pData = (unsigned char*)malloc(nNeedSize);
    if(NULL == pData)
    {
        printf("ImageBuffer Alloc Failed, please Press Any Key To Exit\n");
        return NULL;
    }
    
    unsigned int nDataSize = nNeedSize;
    
    unsigned char* pJpegData = (unsigned char*)malloc(10 * 1024 *1024); // 10M
    if(NULL == pData)
    {
        printf("pJpegData Alloc Failed, please Press Any Key To Exit\n");
        return NULL;
    }
    unsigned int nJpegSize = 10 * 1024 *1024;
    unsigned int nCurOffset = 0;
    unsigned int nCurHeight = 0;
    unsigned int nTotalLen = 0;
    unsigned int nTotalHeight = 0;
    bool bGetAllImage = false;
    while(1)
    {
        
        nRet = MV_CC_GetImageBuffer(pUser, &stOutFrame, 1000);
        if (nRet == MV_OK)
        {
            //图像拼接
            memcpy(pData + nCurOffset, stOutFrame.pBufAddr, stOutFrame.stFrameInfo.nFrameLen);
            nCurOffset += stOutFrame.stFrameInfo.nFrameLen;
            nCurHeight += stOutFrame.stFrameInfo.nHeight;
            if(nCurHeight >= g_nTotalHeight)
            {
                nTotalLen = nCurOffset;
                nTotalHeight = nCurHeight;
                bGetAllImage = true;
                nCurOffset = 0;
                nCurHeight = 0;
            }
           
        }
        else
        {
            printf("No data[0x%x]\n", nRet);
        }
        if(NULL != stOutFrame.pBufAddr)
        {
            nRet = MV_CC_FreeImageBuffer(pUser, &stOutFrame);
            if(nRet != MV_OK)
            {
                printf("Free Image Buffer fail! nRet [0x%x]\n", nRet);
            }
        }
        
        if(true == bGetAllImage)
        {
            bGetAllImage = false;
            MV_SAVE_IMAGE_PARAM_EX stSaveParam = {0};
            stSaveParam.pData = pData;
            stSaveParam.nDataLen = nTotalLen;
            stSaveParam.enPixelType = stOutFrame.stFrameInfo.enPixelType;
            stSaveParam.nWidth = stOutFrame.stFrameInfo.nWidth;
            stSaveParam.nHeight = nTotalHeight;
            stSaveParam.pImageBuffer = pJpegData;
            stSaveParam.nBufferSize = nJpegSize;
            stSaveParam.enImageType = MV_Image_Jpeg;
            stSaveParam.nJpgQuality = 20;// 压缩质量，值越大，图像越大，图像质量越高
            nRet = MV_CC_SaveImageEx2(pUser, &stSaveParam);
            if(MV_OK == nRet)
            {
                if(true == g_bSave)
                {
                    char strFilename[64] = {0};
                    sprintf(strFilename, "Image_%d.jpg", stOutFrame.stFrameInfo.nFrameNum);
                    FILE* fd = fopen(strFilename, "wb+");
                    fwrite(pJpegData, stSaveParam.nImageLen, 1, fd);
                    fclose(fd);
                }
                printf("Get One Frame: Width[%d], Height[%d], nFrameLen[%d]\n",
            stOutFrame.stFrameInfo.nWidth, nTotalHeight, stSaveParam.nImageLen);
            }
            else
            {
                printf("Frame Compress Failed, nRet[%x]\n", nRet);
            }
        }
        if(g_bExit)
        {
            break;
        }
    }
    
    if(pData)
    {
        free(pData);
    }
    if(pJpegData)
    {
        free(pJpegData);
    }

    return 0;
}

int main()
{
    int nRet = MV_OK;
    void* handle = NULL;

    do 
    {
        // ch:枚举设备 | en:Enum device
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (MV_OK != nRet)
        {
            printf("Enum Devices fail! nRet [0x%x]\n", nRet);
            break;
        }

        if (stDeviceList.nDeviceNum > 0)
        {
            for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++)
            {
                printf("[device %d]:\n", i);
                MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
                if (NULL == pDeviceInfo)
                {
                    break;
                } 
                PrintDeviceInfo(pDeviceInfo);            
            }  
        } 
        else
        {
            printf("Find No Devices!\n");
            break;
        }

        printf("Please Intput camera index:");
        unsigned int nIndex = 0;
        scanf("%d", &nIndex);
        SkipEnter();
        if (nIndex >= stDeviceList.nDeviceNum)
        {
            printf("Intput error!\n");
            break;
        }

        // ch:选择设备并创建句柄 | en:Select device and create handle
        nRet = MV_CC_CreateHandle(&handle, stDeviceList.pDeviceInfo[nIndex]);
        if (MV_OK != nRet)
        {
            printf("Create Handle fail! nRet [0x%x]\n", nRet);
            break;
        }

        // ch:打开设备 | en:Open device
        nRet = MV_CC_OpenDevice(handle);
        if (MV_OK != nRet)
        {
            printf("Open Device fail! nRet [0x%x]\n", nRet);
            break;
        }

        // ch:探测网络最佳包大小(只对GigE相机有效) | en:Detection network optimal package size(It only works for the GigE camera)
        if (stDeviceList.pDeviceInfo[nIndex]->nTLayerType == MV_GIGE_DEVICE)
        {
            int nPacketSize = MV_CC_GetOptimalPacketSize(handle);
            if (nPacketSize > 0)
            {
                nRet = MV_CC_SetIntValue(handle,"GevSCPSPacketSize",nPacketSize);
                if(nRet != MV_OK)
                {
                    printf("Warning: Set Packet Size fail nRet [0x%x]!\n", nRet);
                }
            }
            else
            {
                printf("Warning: Get Packet Size fail nRet [0x%x]!\n", nPacketSize);
            }
        }

        // ch:设置触发模式为off | en:Set trigger mode as off
        nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 0);
        if (MV_OK != nRet)
        {
            printf("Set Trigger Mode fail! nRet [0x%x]\n", nRet);
            break;
        }

        // ch:获取数据包大小 | en:Get payload size
        MVCC_INTVALUE stParam;
        memset(&stParam, 0, sizeof(MVCC_INTVALUE));
        nRet = MV_CC_GetIntValue(handle, "PayloadSize", &stParam);
        if (MV_OK != nRet)
        {
            printf("Get PayloadSize fail! nRet [0x%x]\n", nRet);
            break;
        }
        g_nPayloadSize = stParam.nCurValue;
        
        printf("Do You Want To Set To Trigger Mode? [y/N]\n");
        char chTriggerMode = 0;
        chTriggerMode = getchar();
        SkipEnter();
        
        if('Y' == chTriggerMode || 'y' == chTriggerMode)
        {
            /*********************设置触发模式，触发源*************************/
            //MVCC_ENUMVALUE stParam;
            nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 1); // 开启触发模式
            if (MV_OK != nRet)
            {
                printf("Set Trigger Mode Failed! nRet [0x%x]\n", nRet);
                break;
            }
            
            printf("Please Choose Trigger Source:\n");
            printf("0: Line0; 1: Line1; 2: Line2;\n\n");
			printf("Input:");
            unsigned int nTriggerSource = 0;
            scanf("%d", &nTriggerSource);
            SkipEnter();
            if(nTriggerSource > 2)
            {
                printf("Invalid Input, Set defalut Value 0\n");
                nTriggerSource = 0;
            }
            //根据实际需要设置触发源，根据实际情况设置源
            nRet = MV_CC_SetEnumValue(handle, "TriggerSource", nTriggerSource); //设置触发源，0：Line0; 1: Line1; 2: Line2; 7: Software
            if (MV_OK != nRet)
            {
                printf("Set Trigger Source Failed! nRet [0x%x]\n", nRet);
            }
            
            MV_CC_SetEnumValue(handle, "TriggerActivation", 2); // 设置为高电平触发
			if (MV_OK != nRet)
            {
                printf("Set Trigger Activation Failed! nRet [0x%x]\n", nRet);
                break;
            }
            /*********************************************************************/
        }
        else if('n' != chTriggerMode && 'N' != chTriggerMode)
        {
            printf("Set Default Mode: Continue Mode\n");
        }
        
        printf("Input Needed Picture Lines, Input 0 means set default 60000 :");
		
		scanf("%d", &g_nTotalHeight);
		SkipEnter();
		if(0 == g_nTotalHeight)
		{
			g_nTotalHeight = 60000;
		}
		
        
        printf("Do You Want To Save Picture? [Y/n]\n");
        char chSaveImage = 0;
        chSaveImage = getchar();
        SkipEnter();
        if('Y' == chSaveImage || 'y' == chSaveImage)
        {
            g_bSave = true;
        }
        else if('n' == chSaveImage || 'N' == chSaveImage)
        {
            g_bSave = false;
        }
        else
        {
            printf("Input is Not Valid, defalt save picture\n");
            g_bSave = true;
        }
        // ch:开始取流 | en:Start grab image
        nRet = MV_CC_StartGrabbing(handle);
        if (MV_OK != nRet)
        {
            printf("Start Grabbing fail! nRet [0x%x]\n", nRet);
            break;
        }

        pthread_t nThreadID;
        nRet = pthread_create(&nThreadID, NULL ,WorkThread , handle);
        if (nRet != 0)
        {
            printf("thread create failed.ret = %d\n",nRet);
            break;
        }

        printf("Press a key to stop grabbing.\n");
        WaitForKeyPress();


        // ch:停止取流 | en:Stop grab image
        nRet = MV_CC_StopGrabbing(handle);
        if (MV_OK != nRet)
        {
            printf("Stop Grabbing fail! nRet [0x%x]\n", nRet);
            break;
        }

        // ch:关闭设备 | Close device
        nRet = MV_CC_CloseDevice(handle);
        if (MV_OK != nRet)
        {
            printf("ClosDevice fail! nRet [0x%x]\n", nRet);
            break;
        }

        // ch:销毁句柄 | Destroy handle
        nRet = MV_CC_DestroyHandle(handle);
        if (MV_OK != nRet)
        {
            printf("Destroy Handle fail! nRet [0x%x]\n", nRet);
            break;
        }
    } while (0);
    

    if (nRet != MV_OK)
    {
        if (handle != NULL)
        {
            MV_CC_DestroyHandle(handle);
            handle = NULL;
        }
    }

    printf("Press a key to exit.\n");
    WaitForKeyPress();

    return 0;
}
