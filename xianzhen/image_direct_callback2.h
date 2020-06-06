#ifndef __IMAGE_DIRECT_CALLBACK_H
#define __IMAGE_DIRECT_CALLBACK_H

// Library for timestamp generation
#include <time.h>
#include <sys/time.h>
#include<cmath>
#include <string>
#include <array>
#include <vector>
using namespace std;

// Library for Redis Connection
#include <stddef.h> 
#include <stdarg.h> 
#include <assert.h> 
#include <hiredis/hiredis.h>
#include <sstream>
#include <fstream> // Using for logging
#include <time.h>

// 使用jpg数据流
#include "Image.h"


clock_t start_getimage_one,start_getimage_two,finish_getimage_one,finish_getimage_two,start_saveredis,finish_saveredis;
// Structure for Camera Parameter
typedef struct _Parameter_ {
    int roi_width;
    int roi_height;
    int roi_offsetx;
    int roi_offsety;
    float exposure_time;
	int height;
} Parameter;

// Structure for Work Order
typedef struct _Work_Order_ {
    string order_num;
    string zipper_num;
    string zipper_length;
    string zipper_color;
} Work_Order;

// camera num in the device list
int camera_one = 0;
int camera_two = 1;

int exposurelevel_opt = 0;

// handles for cameras
void* handle_one = NULL;
void* handle_two = NULL;

mutex t1,t2; // lock for threads

MV_FRAME_OUT_INFO_EX* img_info;

// camera log
string logger_name;
ofstream logger_file;

int img_count = 0; // img counter for both camera.
int top_count; // pic num for current type of zipper = zipper length (cm) / vision width (cm)
Work_Order work_order; // work order information for current type of zipper
redisContext* listener;

bool first_off = true;

int last_camera_one_position_code = 1;
int last_camera_two_position_code = 1;
int camera_one_img_count = 0;
int camera_two_img_count = 0;
unsigned int camera_one_last_frame_num = 0;
unsigned int camera_two_last_frame_num = 0;

string last_machine_status = "off";

/*
Function to get timestamp with year-month-day hour:minute:second:milisecond format
Parameter:
time_t nowtime: current time
*/
string get_current_date_time(time_t nowtime){
	struct timeval tv;

	struct tm *nowtm;
	char tmbuf[64], buf[64];

	gettimeofday(&tv, NULL);
	nowtime = tv.tv_sec;
	nowtm = localtime(&nowtime);

	//strftime info: https://en.cppreference.com/w/c/chrono/strftime
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d-%H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s:%06ld", tmbuf, tv.tv_usec);

	return buf;
}

// Function to generate logging message line by line with current time and level
string logging_generation(string level, string msg){
    return get_current_date_time(time(0)) + "-" + level + ": " + msg + "\n";
}

/*
Function to get timestamp with year-month-day hour:minute:second:milisecond format
Parameter:
time_t nowtime: current time
*/
string get_current_date_time_without_symbol(time_t nowtime){
	struct timeval tv;

	struct tm *nowtm;
	char tmbuf[64], buf[64];

	gettimeofday(&tv, NULL);
	nowtime = tv.tv_sec;
	nowtm = localtime(&nowtime);

	//strftime info: https://en.cppreference.com/w/c/chrono/strftime
	strftime(tmbuf, sizeof tmbuf, "%Y%m%d%H%M%S", nowtm);
	snprintf(buf, sizeof buf, "%s%06ld", tmbuf, tv.tv_usec);

	return buf;
}

/*
Function used to connect Redis Server and create a listener
Parameter: 
const char* ip_address: ip address for Redis Server
int port: port for Redis Server
Return:
redisContext* listener: a listener created after connecting to Redis Server
hiredis
opencv-dev
boost
*/
redisContext* listener_creator(const char* ip_address, int port) {
    redisContext* listener = redisConnect(ip_address,port);
    if (listener == NULL || listener->err) {
        if (listener) {
            printf("Failed to connect to Redis Server.\nError: %s\n", listener->errstr);
            logger_file << logging_generation("ERROR","Failed to connect to Redis Server.");
            // handle error
        } 
        else {
            printf("Can't allocate redis context\n");
            logger_file << logging_generation("ERROR","Can't allocate redis context");
        }
    }
    printf("Succeed to connect to Redis Server.\n");
    logger_file << logging_generation("INFO","Succeed to connect to Redis Server.");
    return listener;
}

/*
Function used to execute command
Parameter: 
redisContext* listener: a listener created after connecting to Redis Server
const char* command: a setting command
int reply_type: use to compare with reply type to check success/failure of execution
Return:
redisReply* reply: execution reply
*/
redisReply* redis_command_execution(redisContext* listener,string name,const char* valueData,unsigned int in_len){
    redisReply* reply = (redisReply*)redisCommand(listener,"SETEX %s 600 %b",name.c_str(),valueData,in_len);
    if(NULL == reply){
        printf("Failed to execute redis command in byte.\n");
        logger_file << logging_generation("ERROR","Failed to execute redis command in byte.");
        redisFree(listener);
        return NULL;
    }
    return reply;
}

redisReply* redis_command_execution(redisContext* listener, const char* command, int reply_type) {
    redisReply* reply = (redisReply*)redisCommand(listener,command);
    if( NULL == reply) { 
        printf("Failed to execute redis command.\n"); 
        logger_file << logging_generation("ERROR","Failed to execute redis command.");
        redisFree(listener); 
        return NULL; 
    } 
    /*
    if(reply->type != reply_type ) { 
        printf("Failed to execute command[%s]\n",command); 
        freeReplyObject(reply); 
        redisFree(listener); 
        return NULL; 
    } 
    */
    
    //printf("Succeed to execute command[%s]\n", command);
    return reply;
}

string position_code_int_to_string(int position_code){
    string pc = to_string(position_code);
    if(position_code == 0) {
        pc = "00";
    }
    else if(position_code == 1) {
        pc = "01";
    }
    return pc;
}

bool counter_redis_setting(redisContext* listener, string key_word, int counter) 
{
    string command_str = "set " + key_word + " " + to_string(counter);
    char* command = (char *)command_str.c_str();
    redisReply* reply = redis_command_execution(listener,command,REDIS_REPLY_STRING);
    if(reply == NULL) 
    {
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool PrintDeviceInfo(MV_CC_DEVICE_INFO* pstMVDevInfo, int first_camera)
{
    if (NULL == pstMVDevInfo)
    {
        printf("The Pointer of pstMVDevInfo is NULL!\n");
        logger_file << logging_generation("ERROR","The Pointer of pstMVDevInfo is NULL!");
        return false;
    }
    if (pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE)
    {
        int nIp1 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
        int nIp2 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
        int nIp3 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
        int nIp4 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);

        // determine which camera is camera one or two
        if(nIp1==192||nIp1==169)
        {
            if(nIp1==192&&first_camera!=0)
            {
                printf("make sure upper camera ip first is 192\n");
                logger_file << logging_generation("WARNING","make sure upper camera ip first is 192!");
            }
        }
        else
        {
            printf("camera ip first not in 192 or 169\n");
            logger_file << logging_generation("ERROR","camera ip first not in 192 or 169 !");
            return false;
        }
        if(nIp1==192) camera_one=first_camera;
        if(nIp1==169) camera_two=first_camera;

        // ch:打印当前相机ip和用户自定义名字 | en:print current ip and user defined name
        printf("[device %d]:\n", first_camera);
        printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName);
        printf("CurrentIp: %d.%d.%d.%d\n" , nIp1, nIp2, nIp3, nIp4);
        printf("UserDefinedName: %s\n\n" , pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName);

        logger_file << logging_generation("INFO","[device " + to_string(first_camera) + "]:");
        logger_file << logging_generation("INFO","Device Model Name: " + string((char *)pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName));
        logger_file << logging_generation("INFO","CurrentIp: " + to_string(nIp1) + "." + to_string(nIp2) + "." + to_string(nIp3) + "." + to_string(nIp4));
        logger_file << logging_generation("INFO","UserDefinedName: " + string((char *)pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName));
    }
    else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE)
    {
        printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName);
        printf("UserDefinedName: %s\n\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName);
        logger_file << logging_generation("INFO","Device Model Name: " + string((char *)pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName));
        logger_file << logging_generation("INFO","UserDefinedName: " + string((char *)pstMVDevInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName));
    }
    else
    {
        printf("Not support.\n");
        logger_file << logging_generation("ERROR","Not support.");
    }

    return true;
}

array<int, 2> lose_frame_or_lose_packet_detection(int camera_num, unsigned int last_frame_num, unsigned int current_frame_num,unsigned int current_lose_packet){
    array<int, 2> lost_frame_packet_array;
    lost_frame_packet_array[0] = 0;
    lost_frame_packet_array[1] = 0;
    
    // determine lose frame
    unsigned int frame_lost = current_frame_num - last_frame_num - 1;
    if(frame_lost > 0){
        logger_file << logging_generation("ERROR","LOST " + to_string(frame_lost) + " FRAME(S) for camera " + to_string(camera_num));
        cout << "LOST " + to_string(frame_lost) + " FRAME(S) for camera " + to_string(camera_num) << endl;
        lost_frame_packet_array[0] = frame_lost;
    }

    if(current_lose_packet > 20){
        logger_file << logging_generation("ERROR","LOST PACKETS for camera " + to_string(camera_num));
        cout << "LOST PACKETS for camera " + to_string(camera_num) << endl;
        lost_frame_packet_array[1] = current_lose_packet;
    }

    if(current_lose_packet > 0 && current_lose_packet <= 20){
        logger_file << logging_generation("WARNING","ENDURED LOST PACKETS for camera " + to_string(camera_num) + "(" + to_string(current_lose_packet) + " packets)");
        cout << "WARNING: ENDURED LOST PACKETS for camera " + to_string(camera_num) + "(" + to_string(current_lose_packet) + " packets)" << endl;
    }

    return lost_frame_packet_array;
}

int position_code_generator(int &camera_image_count, int &last_position_code){
    /*int current_position_code = -1;

    ++camera_image_count;

    if(top_count == 2) {
        return 11;
    }

    if(last_position_code == 1){
        if(camera_image_count == 1){
            current_position_code = 10;
        }
        else{
            logger_file << logging_generation("ERROR","WRONG ORDER");
        }
    }
    else if(last_position_code == 10){
        if(camera_image_count == 2){
            if(top_count == 4) {
                current_position_code = 1;
		camera_image_count = 0;
            }
            else {
                current_position_code = 0;
            }
        }
        else{
            logger_file << logging_generation("ERROR","WRONG ORDER");
        }
    }
    else if(last_position_code == 0){
        if(camera_image_count < top_count / 2){
            current_position_code = 0;
        }
        else if(camera_image_count == top_count / 2){
            current_position_code = 1;
            camera_image_count = 0;
        }
        else {
            logger_file << logging_generation("ERROR","WRONG ORDER");
        }
    }
    last_position_code = current_position_code;
    return current_position_code;*/
    return 01;
}

void top_count_updater(){
    string top_count_command_str = "get topcount";
    char* top_count_command = (char*)top_count_command_str.c_str();
    redisReply* top_count_reply = redis_command_execution(listener,top_count_command,REDIS_REPLY_STRING);
    if(top_count_reply == NULL){
        logger_file << logging_generation("ERROR","Failed to get top count");
    }
    string cur_top_count = top_count_reply->str;
    freeReplyObject(top_count_reply);

    top_count = stod(cur_top_count);

    cout << "Current top count: " << top_count << endl;
    logger_file << logging_generation("INFO","Current top count: " + to_string(top_count));
}

void lost_frame_position_code_generator(int lost_frame_num, vector<string> &lv, int &camera_image_count, int &last_position_code){
    for(int i = 0; i < lost_frame_num; i++){
        lv.push_back(position_code_int_to_string(position_code_generator(camera_image_count,last_position_code)));
    }
}

class line
{ 
public: 
    line()
    {
        ban_height=0;      //每次采集的行高
        ban_height_num=0;  //采集的次数
        ban_index=0;       //哪个高开始
        g_nTotalHeight=0;
		nTotalWidth = 0;
	    nNeedSize=0;
        pData=NULL;
        handle=NULL;
    }
    void destory()
    {
        if(pData)
        {
            free(pData);
            pData=NULL;
        }
    }

    void clear()
    {
        if(pData) memset(pData,0,nNeedSize);
        ban_index=0;

    }
    void set(int g_nTotalHeight,void* handle)
    {
        this->handle=handle;
        this->g_nTotalHeight=g_nTotalHeight;
        int nRet = MV_OK;
        MVCC_INTVALUE stWidth = {0};
        MVCC_INTVALUE stHeight = {0};
        MVCC_ENUMVALUE stPixelType = {0};
        nRet = MV_CC_GetIntValue(handle, "Width", &stWidth);
        nRet = MV_CC_GetIntValue(handle, "Height", &stHeight);
        nRet = MV_CC_GetEnumValue(handle, "PixelFormat", &stPixelType);
        if(0 == g_nTotalHeight%stHeight.nCurValue)
        {
            ban_height=stHeight.nCurValue;
            ban_height_num=g_nTotalHeight/stHeight.nCurValue;
        }    
        else
        {
            ban_height=stHeight.nCurValue;
            ban_height_num=int(g_nTotalHeight/stHeight.nCurValue)+1;
        }
        unsigned int nStride = (((stPixelType.nCurValue) >> 16) & 0x000000ff) >> 3;
        std::cout<<"g_nTotalHeight,ban_height,ban_height_num:"<<g_nTotalHeight<<","<<ban_height<<","<<ban_height_num<<std::endl;
        nNeedSize = stWidth.nCurValue*ban_height*ban_height_num* nStride; 
        nTotalWidth=stWidth.nCurValue*ban_height_num;
        pData = (unsigned char*)malloc(nNeedSize);
        if(NULL == pData) printf("set ImageBuffer Alloc Failed!");
    }
    int g_nTotalHeight;  //输入高度
    int ban_height;      //每次采集的行高
    int ban_height_num;  //采集的次数
    int ban_index;       //哪个高开始
    void* handle;        //相机句柄
    int nNeedSize;           //需要开辟的内存区间
    unsigned char* pData;    //原始相机图像数据流
	int nTotalWidth;
};

//线阵相机
line line_one;
line line_two;
string image_naming(string order_num, string timestamp, string timestamp_without_symbol, int top_down_code, int cam_num,line camera_line)
{
    string td_code = position_code_int_to_string(top_down_code);
    string image_name_prefix = timestamp_without_symbol + td_code + to_string(cam_num);
    string image_name = image_name_prefix + "##" + order_num + "##" + timestamp + "##" + td_code + "##" + to_string(cam_num);
    image_name +="##" + to_string(camera_line.ban_height) + "##" + to_string(camera_line.nTotalWidth);
	return image_name;
}

// Change Raw Data into Image Data
MV_SAVE_IMAGE_PARAM_EX raw_to_image(line& li,MV_FRAME_OUT_INFO_EX* img_inf, void* pUser) 
{
    unsigned int nJpegSize = li.nNeedSize*2; //10 * 1024 *1024;
    // fill in the parameters of save image
    MV_SAVE_IMAGE_PARAM_EX stSaveParam;
    memset(&stSaveParam, 0, sizeof(MV_SAVE_IMAGE_PARAM_EX));
    // 从上到下依次是：输出图片格式，输入数据的像素格式，提供的输出缓冲区大小，图像宽，
    // 图像高，输入数据缓存，输出图片缓存，JPG编码质量
    // Top to bottom are：
    stSaveParam.enImageType = MV_Image_Jpeg; 
    stSaveParam.enPixelType = img_inf->enPixelType; 
    stSaveParam.nBufferSize = nJpegSize;
    stSaveParam.nWidth      = li.nTotalWidth; 
    stSaveParam.nHeight     = img_inf->nHeight; 
    stSaveParam.pData       = li.pData;
    stSaveParam.nDataLen    = img_inf->nWidth*li.ban_height_num*img_inf->nHeight;
    stSaveParam.pImageBuffer= (unsigned char*)malloc(nJpegSize);//pDataForSaveImage;
    stSaveParam.nJpgQuality = 80;
    int nRet = MV_CC_SaveImageEx2(pUser, &stSaveParam);
    /*
    if(pUser==line_one.handle)
    {
        static int a=30;
        char strFilename[64] = {0};
        sprintf(strFilename, "/home/docker/IBM/camera/Image_%d.jpg", a);
        FILE* fd = fopen(strFilename, "wb+");
        fwrite(stSaveParam.pImageBuffer, stSaveParam.nImageLen, 1, fd);
        fclose(fd);
        a+=1;
    }
    if(pUser==line_one.handle)
    {
        static int a=1;
        char strFilename[64] = {0};
        sprintf(strFilename, "/home/docker/IBM/camera/Image_%d.jpg", a);
        FILE* fd = fopen(strFilename, "wb+");
        fwrite(stSaveParam.pImageBuffer, stSaveParam.nImageLen, 1, fd);
        fclose(fd);
        a+=1;
    }
    */
    return stSaveParam;
}

void saveredis(void* pUser,MV_SAVE_IMAGE_PARAM_EX encoded_img_data,line camera_line)
{
    start_saveredis=clock();
    int cur_camera_num;
    if(pUser==handle_one)
        cur_camera_num = 1;
    if(pUser==handle_two)
        cur_camera_num = 2;
    string lost_or_normal_status = "normal";
    time_t now = time(0);
    string now_time = get_current_date_time(now);
    string now_time_without_symbol = get_current_date_time_without_symbol(now);            
    string key_word = image_naming(work_order.order_num,now_time,now_time_without_symbol,1,cur_camera_num,camera_line);
    logger_file << logging_generation("INFO","store image into redis:"+now_time);
    cout << "store image into redis:"<<now_time<<endl;
    string select_4_command_str = "select 4";
    char* select_4_command = (char*)select_4_command_str.c_str();
    redisReply* select_4_reply = redis_command_execution(listener,select_4_command,REDIS_REPLY_STRING);
    if(select_4_reply == NULL)
    {
        cout << "Cannot select 4" << endl;
        logger_file << logging_generation("ERROR","Cannot select 4");
        return ;
    }
    freeReplyObject(select_4_reply);
    string td_code = "01";
    string image_name = now_time_without_symbol + td_code + to_string(cur_camera_num);
    //string img_command_str = "setex " + image_name + " 600 " + encoded_img_data;
	//string img_command_str = "setex 111 600 33";
    //char* img_command = (char*)img_command_str.c_str();
    redisReply* img_reply = redis_command_execution(listener,image_name,
                                                    (const char*)encoded_img_data.pImageBuffer,
                                                    encoded_img_data.nImageLen);//测试标注
    if(img_reply == NULL) {
        return ;
    }
    freeReplyObject(img_reply);

    //cout << "Key-value pair Time: "<< get_current_date_time(time(0)) << endl;
    logger_file << logging_generation("INFO","Key-value pair Time: " + get_current_date_time(time(0)));

    string select_3_command_str = "select 3";
    char* select_3_command = (char*)select_3_command_str.c_str();
    redisReply* select_3_reply = redis_command_execution(listener,select_3_command,REDIS_REPLY_STRING);
    if(select_3_reply == NULL){
        cout << "Cannot select 3" << endl;
        logger_file << logging_generation("ERROR","Cannot select 3");
        return ;
    }
    freeReplyObject(select_3_reply);

    string command_str = "lpush picture_model_queue " + key_word;
    char* command = (char*)command_str.c_str();
    redisReply* reply = redis_command_execution(listener,command,REDIS_REPLY_STRING);
    if(reply == NULL) {
        return ;
    }
    freeReplyObject(reply);
    logger_file << logging_generation("INFO",key_word + " without image is lpushed to redis");
    cout << key_word + " without image is lpushed to redis" << endl;
    

    //cout << "picture_model_queue Time: "<< get_current_date_time(time(0)) << endl;
    logger_file << logging_generation("INFO","picture_model_queue Time: " + get_current_date_time(time(0)));
    string saved_command_str = "lpush picture_storage_queue " + key_word + "##" + "NULL" + "##" + lost_or_normal_status;
    char* saved_command = (char*)saved_command_str.c_str();
    redisReply* saved_reply = redis_command_execution(listener,saved_command,REDIS_REPLY_STRING);
    if(saved_reply == NULL) {
        return ;
    }
    freeReplyObject(saved_reply);
    logger_file << logging_generation("INFO",key_word + " with image is lpushed to redis");
    cout << key_word + " with image is lpushed to redis" << endl;
	
    string select_0_command_str = "select 0";
    char* select_0_command = (char*)select_0_command_str.c_str();
    redisReply* select_0_reply = redis_command_execution(listener,select_0_command,REDIS_REPLY_STRING);
    if(select_0_reply == NULL){
        cout << "Cannot select 0" << endl;
        logger_file << logging_generation("ERROR","Cannot select 0");
        return ;
    }
    freeReplyObject(select_0_reply);    
    finish_saveredis=clock();
    logger_file << logging_generation("INFO", "picture_storage_queue Time: " + get_current_date_time(time(0))); 
}

void ImageCallBackEx(unsigned char * e_pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) 
{
    if (pFrameInfo) 
    {
        lock(t1,t2);
	    int nRet = MV_OK;
        if(pUser==handle_one)
            printf("Camera [%d] Get One Frame: Width[%d], Height[%d], nFrameNum[%d]\n", 
                    1,pFrameInfo->nWidth, pFrameInfo->nHeight, pFrameInfo->nFrameNum);
        if(pUser==handle_two)
            printf("Camera [%d] Get One Frame: Width[%d], Height[%d], nFrameNum[%d]\n", 
                    2,pFrameInfo->nWidth, pFrameInfo->nHeight, pFrameInfo->nFrameNum);
        
        logger_file.open(logger_name,ios_base::app);
        logger_file << logging_generation("INFO","Frame Num: " + to_string(pFrameInfo->nFrameNum));
        cout << "Enter Lock Time: "<< get_current_date_time(time(0)) << endl;
        logger_file << logging_generation("INFO","Enter Lock Time: " + get_current_date_time(time(0)));
        top_count_updater();
        unsigned int current_frame_num = pFrameInfo->nFrameNum;
        unsigned int current_lose_packet = pFrameInfo->nLostPacket;
        MV_SAVE_IMAGE_PARAM_EX encoded_img_data;
        if(pUser==line_one.handle)
        {
            start_getimage_one=clock();
            if(line_one.ban_index<line_one.ban_height_num)
            {
                int perline=pFrameInfo->nWidth*line_one.ban_height_num;  //目标图像每行的像素  
                int perlineInBan=line_one.ban_index*pFrameInfo->nWidth;  //指定ban的每行起始像素
                for(int i=0;i<pFrameInfo->nHeight;i++)
                {
                    unsigned char* dst=line_one.pData+i*perline+perlineInBan;
                    unsigned char* src=e_pData+i*pFrameInfo->nWidth;
                    memcpy(dst,src,pFrameInfo->nWidth);
                }
                std::cout<<"line_one.ban_index:"<<line_one.ban_index<<std::endl;
                line_one.ban_index+=1;
                std::cout<<"line_one.ban_index:"<<line_one.ban_index<<std::endl;
            }
     
            if(line_one.ban_index==line_one.ban_height_num)
            {
                cout<<"saveredis success"<<get_current_date_time(time(0)) << endl;
                //raw_to_image(line_one);
                encoded_img_data=raw_to_image(line_one,pFrameInfo,pUser);
                saveredis(pUser,encoded_img_data,line_one);
                memset(line_one.pData,0,line_one.nNeedSize);
                line_one.ban_index=0;
                free(encoded_img_data.pImageBuffer);
                cout<<"saveredis success"<<get_current_date_time(time(0)) << endl;
            }
        }
        else if(pUser==line_two.handle)
        {
            start_getimage_one=clock();
            if(line_two.ban_index<line_two.ban_height_num)
            {
                int perline=pFrameInfo->nWidth*line_two.ban_height_num;  //目标图像每行的像素  
                int perlineInBan=line_two.ban_index*pFrameInfo->nWidth;  //指定ban的每行起始像素
                for(int i=0;i<pFrameInfo->nHeight;i++)
                {
                    unsigned char* dst=line_two.pData+i*perline+perlineInBan;
                    unsigned char* src=e_pData+i*pFrameInfo->nWidth;
                    memcpy(dst,src,pFrameInfo->nWidth);
                }
                std::cout<<"line_two.ban_index:"<<line_two.ban_index<<std::endl;
                line_two.ban_index+=1;
                std::cout<<"line_two.ban_index:"<<line_two.ban_index<<std::endl;
            }
            if(line_two.ban_index==line_two.ban_height_num)
            {
                cout<<"saveredis success"<<get_current_date_time(time(0)) << endl;
		        encoded_img_data=raw_to_image(line_two,pFrameInfo,pUser);
                saveredis(pUser,encoded_img_data,line_two);
                memset(line_two.pData,0,line_two.nNeedSize);
                line_two.ban_index=0;
                free(encoded_img_data.pImageBuffer);
                cout<<"saveredis success"<<get_current_date_time(time(0)) << endl;
            }
        }
        logger_file.close();
        t1.unlock();
        t2.unlock();
        cout << "End Time: " << get_current_date_time(time(0)) << endl;
    }   
}

Work_Order work_order_info_grabber(redisContext* listener){
    Work_Order wrong_order = {"-2","-2","-2","-2"};
    string result[4];

    ////string command_str = "mget currentTicket currentZipperWidth currentTotalLength currentZipperColor";
    string command_str = "mget currentTicket currentZipperWidth topcount currentZipperColor";
    char* command = (char*)command_str.c_str();

    redisReply* reply = redis_command_execution(listener,command,REDIS_REPLY_STRING);
    if(reply == NULL) {
        return wrong_order;
    }

    int result_size = (int)reply->elements;

    if(reply->element == NULL && result_size == 0){
        return wrong_order;
    }

    for(int i = 0; i < result_size; i++) {
        redisReply* cur_reply = reply->element[i];
        if(cur_reply->str == NULL) {
            result[i] = "-1";
        }
        else {
            string result_str = (string)cur_reply->str;
            result[i] = result_str;
        }
        cur_reply = NULL;
    }
    freeReplyObject(reply);

    Work_Order good_order = {result[0],result[1],result[2],result[3]};
    return good_order;
}

void* open_camera(int camera_num, MV_CC_DEVICE_INFO_LIST stDeviceList) {
    int nRet = MV_OK;
    void* handle = NULL;

    // select device and create handle
    nRet = MV_CC_CreateHandle(&handle, stDeviceList.pDeviceInfo[camera_num]);
    if (MV_OK != nRet) {
        printf("MV_CC_CreateHandle fail for camera %x! nRet [%x]\n", camera_num, nRet);
        logger_file << logging_generation("ERROR","MV_CC_CreateHandle fail for camera " + to_string(camera_num) + "! nRet: " + to_string(nRet));
        return NULL;
    }

    // open device
    nRet = MV_CC_OpenDevice(handle);
    if (MV_OK != nRet) {
        printf("MV_CC_OpenDevice fail for camera %x! nRet [%x]\n", camera_num, nRet);
        logger_file << logging_generation("ERROR","MV_CC_CreateHandle fail for camera " + to_string(camera_num) + "! nRet: " + to_string(nRet));
        return NULL;
    }

    // Detection network optimal package size(It only works for the GigE camera)
    if (stDeviceList.pDeviceInfo[camera_num]->nTLayerType == MV_GIGE_DEVICE) {
        int nPacketSize = MV_CC_GetOptimalPacketSize(handle);
        cout << "nPacketSize: " << nPacketSize << endl;
        if (nPacketSize > 0) {
            nRet = MV_CC_SetIntValue(handle,"GevSCPSPacketSize",nPacketSize);
            if(nRet != MV_OK) {
                printf("Warning: Set Packet Size fail nRet [0x%x]!", nRet);
                logger_file << logging_generation("WARNING","Set Packet Size fail nRet: " + to_string(nRet));
            }
        }
        else {
            printf("Warning: Get Packet Size fail nRet [0x%x]!", nPacketSize);
            logger_file << logging_generation("WARNING","Get Packet Size fail nRet: " + to_string(nPacketSize));
        }
    }

    return handle;
}

void close_camera(void* handle) {
    // close device
    int nRet = MV_CC_CloseDevice(handle);
    if (MV_OK != nRet) {
        printf("MV_CC_CloseDevice fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_CloseDevice fail! nRet: " + to_string(nRet));
        return;
    }

    // destroy handle
    nRet = MV_CC_DestroyHandle(handle);
    if (MV_OK != nRet) {
        printf("MV_CC_DestroyHandle fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_DestroyHandle fail! nRet: " + to_string(nRet));
        return;
    }
}

//根据行高进行相机ban设置
int lineban(int input,int limit)
{
    int ban=0;
    int no=1;
    while (true)
    {
        int ban_size=ceil(((float)input)/no);
        if(ban_size>limit)
            no++;
        else if(ban_size%2!=0)
        {
            ban_size++;
            ban=ban_size;
            break;
        }
        else
        {
            ban=ban_size;
            break;
        }
        
    }
    return ban;
}


bool parameter_setting(int nRet, void* handle, Parameter cp, int* camera_num)
{
	//塑钢半检根据约定，上面相机为192 故camer_one始终为0 camera_two始终为1
	if(cp.height<=20200)//48以下
	{
		nRet = MV_CC_SetHeight(handle,cp.height);
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetHeight fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetHeight fail! nRet: " + to_string(nRet));
			return false;
		}
	}
    else
    {
        //动态设置每次相机行高
        int ban_size=0;
        ban_size=lineban(cp.height,20000);
        printf("set camera height: [%d]\n", ban_size);
		logger_file << logging_generation("INFO","set camera height: "+to_string(ban_size));
        
		nRet = MV_CC_SetHeight(handle,ban_size);
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetHeight fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetHeight fail! nRet: " + to_string(nRet));
			return false;
		}
	}
    
	cout<<"2"<<endl;	
    //相机注册
    if(handle==handle_one) line_one.set(cp.height,handle_one);
    cout<<"3"<<endl;
    if(handle==handle_two) line_two.set(cp.height,handle_two);
	cout<<"4"<<endl;
	nRet = MV_CC_SetFloatValue(handle, "ExposureTime", cp.exposure_time);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetExposureTime fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetExposureTime fail! nRet: " + to_string(nRet));
        return false;
    }
	cout << "ExposureTime:" << cp.exposure_time << endl;
    nRet = MV_CC_SetBoolValue(handle,"GammaEnable",1);
    if (MV_OK != nRet)
	{
		printf("MV_CC_GammaEnable fail! nRet [%x]\n", nRet);
		logger_file << logging_generation("ERROR","MV_CC_GammaEnable fail! nRet: " + to_string(nRet));
		return false;
	}
	if(exposurelevel_opt == 1 || exposurelevel_opt == 2)
	{
		nRet = MV_CC_SetFloatValue(handle, "Gamma", 1.0);
		cout << "Gamma:1.0" << endl;
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetGamma fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetGamma fail! nRet: " + to_string(nRet));
			return false;
		}
	}else
	{
		nRet = MV_CC_SetFloatValue(handle, "Gamma", 0.7);
		cout << "Gamma:0.7" << endl;
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetGamma fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetGamma fail! nRet: " + to_string(nRet));
			return false;
		}
	}
	//FrequencyConverterControl
	nRet = MV_CC_SetIntValue(handle,"PreDivider",3);
	if (MV_OK != nRet)
	{
		printf("MV_CC_SetPreDivider fail! nRet [%x]\n", nRet);
		logger_file << logging_generation("ERROR","MV_CC_SetPreDivider fail! nRet: " + to_string(nRet));
		return false;
	}
	nRet = MV_CC_SetIntValue(handle,"PostDivider",1);
	if (MV_OK != nRet)
	{
		printf("MV_CC_SetPostDivider fail! nRet [%x]\n", nRet);
		logger_file << logging_generation("ERROR","MV_CC_SetPostDivider fail! nRet: " + to_string(nRet));
		return false;
	}
	if(work_order.zipper_num == "3" || work_order.zipper_num == "5")
	{
		nRet = MV_CC_SetIntValue(handle,"Multiplier",14);
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetMultiplier fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetMultiplier fail! nRet: " + to_string(nRet));
			return false;
		}
	}else if(work_order.zipper_num == "8")
	{
		nRet = MV_CC_SetIntValue(handle,"Multiplier",18);
		if (MV_OK != nRet)
		{
			printf("MV_CC_SetMultiplier fail! nRet [%x]\n", nRet);
			logger_file << logging_generation("ERROR","MV_CC_SetMultiplier fail! nRet: " + to_string(nRet));
			return false;
		}
	}
	// set trigger selector	as LineStart
    nRet = MV_CC_SetEnumValue(handle, "TriggerSelector", 9);
    if (MV_OK != nRet) {
        printf("ERROR: set TriggerSelector fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","set TriggerSelector fail! nRet: " + to_string(nRet));
        return false;
    }
	nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 1);
    if (MV_OK != nRet) {
        printf("ERROR: set TriggerSelector fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","set TriggerSelector fail! nRet: " + to_string(nRet));
        return false;
    }
	//set trigger sourc	e as FrequencyConverter
	nRet = MV_CC_SetEnumValue(handle, "TriggerSource", 8);
    if (MV_OK != nRet)
    {
        printf("ERROR: set TriggerSource fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set TriggerSource fail! nRet: " + to_string(nRet));
        return false;
    }

	//set trigger actication as RisingEdge 
	nRet = MV_CC_SetEnumValue(handle, "TriggerActivation", 0);
    if (MV_OK != nRet)
    {
        printf("ERROR: set TriggerActivation fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set TriggerActivation fail! nRet: " + to_string(nRet));
        return false;
    }




    //set trigger selector as  frameburststart
    nRet = MV_CC_SetEnumValue(handle, "TriggerSelector", 6);
    if (MV_OK != nRet) {
        printf("ERROR: set TriggerSelector fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","set TriggerSelector fail! nRet: " + to_string(nRet));
        return false;
    }
    nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 1);
    if (MV_OK != nRet) {
        printf("ERROR: set TriggerSelector fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","set TriggerSelector fail! nRet: " + to_string(nRet));
        return false;
    }
    //set trigger sourc	e as line2
	nRet = MV_CC_SetEnumValue(handle, "TriggerSource", 2);
    if (MV_OK != nRet)
    {
        printf("ERROR: set TriggerSource fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set TriggerSource fail! nRet: " + to_string(nRet));
        return false;
    }
    nRet = MV_CC_SetEnumValue(handle, "TriggerActivation", 2);
    if (MV_OK != nRet)
    {
        printf("ERROR: set TriggerActivation fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set TriggerActivation fail! nRet: " + to_string(nRet));
        return false;
    }
    nRet = MV_CC_SetEnumValue(handle, "TriggerPartialClose", 1);
    if (MV_OK != nRet)
    {
        printf("ERROR: set TriggerPartialClose fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set TriggerPartialClose fail! nRet: " + to_string(nRet));
        return false;
    }




	//set InputSource as Encoder Module Out 
	nRet = MV_CC_SetEnumValue(handle, "InputSource", 7);
    if (MV_OK != nRet)
    {
        printf("ERROR: set InputSource fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set InputSource fail! nRet: " + to_string(nRet));
        return false;
    }


	//set Signal Alignment as RisingEdge
	nRet = MV_CC_SetEnumValue(handle, "SignalAlignment", 0);
    if (MV_OK != nRet)
    {
        printf("ERROR: set SignalAlignment fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set SignalAlignment fail! nRet: " + to_string(nRet));
        return false;
    }
	
	//set Signal EncoderSourceA as Line0 
	nRet = MV_CC_SetEnumValue(handle, "EncoderSourceA", 0);
    if (MV_OK != nRet)
    {
        printf("ERROR: set EncoderSourceA fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set EncoderSourceA fail! nRet: " + to_string(nRet));
        return false;
    }	
	
	//set Signal EncoderSourceB as Line3
	nRet = MV_CC_SetEnumValue(handle, "EncoderSourceB", 3);
    if (MV_OK != nRet)
    {
        printf("ERROR: set EncoderSourceB fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set EncoderSourceB fail! nRet: " + to_string(nRet));
        return false;
    }	

	//set Signal EncoderTriggerMode as AnyDirection 
	nRet = MV_CC_SetEnumValue(handle, "EncoderOutputMode", 0);
    if (MV_OK != nRet)
    {
        printf("ERROR: set EncoderTriggerMode fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set EncoderTriggerMode fail! nRet: " + to_string(nRet));
        return false;
    }	
	//set Signal EncoderCounterMode as IgnoreDirection  
	nRet = MV_CC_SetEnumValue(handle, "EncoderCounterMode", 0);
    if (MV_OK != nRet)
    {
        printf("ERROR: set EncoderCounterMode fail [%x]\n", nRet);
		logger_file << logging_generation("ERROR","set EncoderCounterMode fail! nRet: " + to_string(nRet));
        return false;
    }	 	
	//MVCC_ENUMVALUE struEnumValue = {0}; 
	//nRet = MV_CC_GetEnumValue(handle, "PixelSize", &struEnumValue);
    // set pixel format as mono8
    /*nRet = MV_CC_SetEnumValue(handle, "PixelFormat", 0x01080001);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetPixelFormat fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetPixelFormat fail! nRet: " + to_string(nRet));
        return false;
    }

    // set ROI Width
    nRet = MV_CC_SetIntValue(handle, "Width", cp.roi_width);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetROIWidth fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetROIWidth fail! nRet: " + to_string(nRet));
        return false;
    }

    // set ROI Height
    nRet = MV_CC_SetIntValue(handle, "Height", cp.roi_height);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetROIHeight fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetROIHeight fail! nRet: " + to_string(nRet));
        return false;
    }

    // set ROI OffsetX
    nRet = MV_CC_SetIntValue(handle, "OffsetX", cp.roi_offsetx);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetROIOffsetX fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetROIOffsetX fail! nRet: " + to_string(nRet));
        return false;
    }

    // set ROI OffsetY
    nRet = MV_CC_SetIntValue(handle, "OffsetY", cp.roi_offsety);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetROIOffsetY fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetROIOffsetY fail! nRet: " + to_string(nRet));
        return false;
    }

    // set exposure time
    nRet = MV_CC_SetFloatValue(handle, "ExposureTime", cp.exposure_time);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetExposureTime fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetExposureTime fail! nRet: " + to_string(nRet));
        return false;
    }

    // set trigger mode as on
    nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 1);
    if (MV_OK != nRet) {
        printf("MV_CC_SetTriggerMode fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetTriggerMode fail! nRet: " + to_string(nRet));
        return false;
    }

    // set trigger source 
    // For real, TriggerSource = MV_TRIGGER_SOURCE_LINE0
    // For Test, TriggerSource = MV_TRIGGER_SOURCE_SOFTWARE
    nRet = MV_CC_SetEnumValue(handle, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);
    if (MV_OK != nRet) {
        printf("MV_CC_SetTriggerSource fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetTriggerSource fail! nRet: " + to_string(nRet));
        return false;
    }

    // set trigger activation
    nRet = MV_CC_SetEnumValue(handle, "TriggerActivation", 0);
    if (MV_OK != nRet) {
        printf("MV_CC_SetTriggerActivation fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetTriggerActivation fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Acquisition Burst Frame Count
    nRet = MV_CC_SetIntValue(handle, "AcquisitionBurstFrameCount", 1);
    if (MV_OK != nRet) {
        printf("MV_CC_SetAcquisitionBurstFrameCount fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetAcquisitionBurstFrameCount fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Line Debouncer Time
    nRet = MV_CC_SetIntValue(handle, "LineDebouncerTime", 2000);
    if (MV_OK != nRet) {
        printf("MV_CC_SetLineDebouncerTime fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetLineDebouncerTime fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Acquisition Mode
    nRet = MV_CC_SetEnumValue(handle, "AcquisitionMode", MV_ACQ_MODE_CONTINUOUS);
    if (MV_OK != nRet) {
        printf("MV_CC_SetAcquisitionMode fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetAcquisitionMode fail! nRet: " + to_string(nRet));
        return false;
    }

    // set SDK Cache nodes
    nRet = MV_CC_SetImageNodeNum(handle, 1);
    if (MV_OK != nRet) {
        printf("MV_CC_SetImageNodeNum fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetImageNodeNum fail! nRet: " + to_string(nRet));
        return false;
    }
    
    // set delay of sending packets
    nRet = MV_CC_SetIntValue(handle, "GevSCPD", 12000);
    if (MV_OK != nRet) {
        printf("MV_CC_SetGevSCPD fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetGevSCPD fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Trigger Cache Enable
    nRet = MV_CC_SetBoolValue(handle, "TriggerCacheEnable", false);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetTriggerCacheEnable fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetTriggerCacheEnable fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Acquisition Frame Rate
    nRet = MV_CC_SetFloatValue(handle, "AcquisitionFrameRate", 25);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetAcquisitionFrameRate fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetAcquisitionFrameRate fail! nRet: " + to_string(nRet));
        return false;
    }

    // set Gev PAUSE Frame Reception
    nRet = MV_CC_SetBoolValue(handle, "GevPAUSEFrameReception", true);
    if (MV_OK != nRet)
    {
        printf("MV_CC_SetGevPAUSEFrameReception fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetGevPAUSEFrameReception fail! nRet: " + to_string(nRet));
        return false;
    }*/

    // set GevSCPSPacketSize
    /*
    nRet = MV_CC_SetIntValue(handle, "GevSCPSPacketSize", 8164);
    if (MV_OK != nRet) {
        printf("MV_CC_SetGevSCPSPacketSize fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_SetGevSCPSPacketSize fail! nRet: " + to_string(nRet));
        return false;
    }
    */

    // register image callback
    //cout << "Current Camera to set: " << *camera_num << endl;
    nRet = MV_CC_RegisterImageCallBackEx(handle, ImageCallBackEx, handle);
    if (MV_OK != nRet) {
        printf("MV_CC_RegisterImageCallBackEx fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_RegisterImageCallBackEx fail! nRet: " + to_string(nRet));
        return false; 
    }

    return true;
}

#endif
