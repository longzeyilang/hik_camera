#ifndef __IMAGE_DIRECT_CALLBACK_H
#define __IMAGE_DIRECT_CALLBACK_H

// Library for timestamp generation
#include <time.h>
#include <sys/time.h>

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

// Structure for Camera Parameter
typedef struct _Parameter_ {
    int roi_width;
    int roi_height;
    int roi_offsetx;
    int roi_offsety;
    float exposure_time;
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

string image_naming(string order_num, string timestamp, string timestamp_without_symbol, int top_down_code, int cam_num){
    string td_code = position_code_int_to_string(top_down_code);

    string image_name_prefix = timestamp_without_symbol + td_code + to_string(cam_num);

    string image_name = image_name_prefix + "##" + order_num + "##" + timestamp + "##" + td_code + "##" + to_string(cam_num);
    return image_name;
}

bool counter_redis_setting(redisContext* listener, string key_word, int counter) {
    string command_str = "set " + key_word + " " + to_string(counter);
    char* command = (char *)command_str.c_str();

    redisReply* reply = redis_command_execution(listener,command,REDIS_REPLY_STRING);
    if(reply == NULL) {
        return false;
    }
    freeReplyObject(reply);

    return true;
}

// Use for encode and decode base64
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string bytes_enstr(unsigned char const* bytes_to_encode, unsigned int in_len)
{
	unsigned char *bytes =NULL;
	bytes = (unsigned char*)malloc(in_len);
	//二进制转字符串
	std::string hexstr;
	int ii=0;
	cout << "实际长度：" << in_len * 8 <<endl;
	for(int i = 0; nullptr!=bytes_to_encode && i<in_len; ++i)
	{
		int value = *(bytes_to_encode++);
		stringstream ss;
		string res;
		ss << value;
		ss >> res;
		hexstr += res;
		/*int r = 0;
		string b8="";
		do{               //循环，直到a等于0跳出 
         r = value%2;       //求每一次的余数，即二进制位上的数值 
         value = value/2;
		 stringstream ss;
		 string res;
		 ss << r;
		 ss >> res;
		 b8 += res;
		 ii++;
		 while(value == 0)
		 {
			 if(ii == 8)
			 {
				 ii=0;
				 if(b8.length() == 8)
				 {
					hexstr += b8;
					b8 = "";
				 }
				 break;
			 }else{
				 b8 = "0" + b8;
				 ii++;
			 }
		 }*/
		 /*if(ii==0)
		 {
			 stringstream ss;
			 string res;
			 ss << r;
			 ss >> res;
			 hexstr += "\x"+res;
		 }
		 else
		 {
			 stringstream ss;
			 string res;
			 ss << r;
			 ss >> res;
			 hexstr += res;
		 }
		 ii++;
		 if(ii==2){ii=0;}*/
		//}
		//while(value != 0);
		
		/*char hex1;
		char hex2;
		int value = *(bytes_to_encode++);//bytes_to_encode[i];
		//cout << "i:" << i <<" in_len:" << in_len << " value:" << value << endl;
		int S = value / 16;
		int Y = value % 16;
		
		if(S >= 0 && S <= 9)
		{
			hex1 = (char)(48 + S);
		}
		else
		{
			hex1 = (char)(55 + S);
		}
		
		if(Y >= 0 && Y <= 9)
		{
			hex2 = (char)(48 + Y);
		}
		else
		{
			hex2 = (char)(55 + Y);
		}
		hexstr += hex1+hex2;*/
	}
	//cout << "bytes_enstr:" << hexstr << endl;
	//cout << "字符总长度：" << hexstr.length() << endl;
	return hexstr;
}

std::string bytes_encode(unsigned char const* bytes_to_encode, unsigned int in_len)
{
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];
  //while(in_len--){
  // 	  cout <<"test:"<<*(bytes_to_encode++)<<endl;  
  //}
  char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
	if (i)
	{
		for(j = i; j < 3; j++)
		  char_array_3[j] = '\0';

		char_array_4[0] = ( char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

		for (j = 0; (j < i + 1); j++)
		  ret += base64_chars[char_array_4[j]];

		while((i++ < 3))
		  ret += '=';

	}
	cout << ret <<endl;
  return "";
}

std::string base64_decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = ( char_array_4[0] << 2       ) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) +   char_array_4[3];
      stringstream ss;
	  string res;
      for (i = 0; (i < 3); i++)
		
		
		ss << char_array_3[i];
		ss >> res;
        ret += res;
      i = 0;
    }
  }

  if (i) {
    for (j = 0; j < i; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    stringstream ss;
	string res;
    //for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
	for (j = 0; (j < i - 1); j++)
	{
		
		
		ss << char_array_3[i];
		ss >> res;
		ret += res;
	} 
  }

  return ret;
}

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = ( char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while((i++ < 3))
      ret += '=';

  }
  cout << "decode out:" << base64_decode(ret) << endl;
  return ret;
}



// Change Raw Data into Image Data
string raw_to_image(unsigned char* pData, MV_FRAME_OUT_INFO_EX* img_inf, void* pUser) {
    unsigned char *pDataForSaveImage = NULL;
    pDataForSaveImage = (unsigned char*)malloc(img_info->nWidth * img_info->nHeight * 4 + 2048);

    // fill in the parameters of save image
    MV_SAVE_IMAGE_PARAM_EX stSaveParam;
    memset(&stSaveParam, 0, sizeof(MV_SAVE_IMAGE_PARAM_EX));
    // 从上到下依次是：输出图片格式，输入数据的像素格式，提供的输出缓冲区大小，图像宽，
    // 图像高，输入数据缓存，输出图片缓存，JPG编码质量
    // Top to bottom are：
    stSaveParam.enImageType = MV_Image_Jpeg; 
    stSaveParam.enPixelType = img_inf->enPixelType; 
    stSaveParam.nBufferSize = img_inf->nWidth * img_inf->nHeight * 4 + 2048;
    stSaveParam.nWidth      = img_inf->nWidth; 
    stSaveParam.nHeight     = img_inf->nHeight; 
    stSaveParam.pData       = pData;
    stSaveParam.nDataLen    = img_inf->nFrameLen;
    stSaveParam.pImageBuffer = pDataForSaveImage;
    stSaveParam.nJpgQuality = 80;

    int nRet = MV_CC_SaveImageEx2(pUser, &stSaveParam);

	//string encoded_image = bytes_enstr(stSaveParam.pImageBuffer,stSaveParam.nImageLen);
    string encoded_image = base64_encode(stSaveParam.pImageBuffer,stSaveParam.nImageLen);
	//string enstr_image = bytes_enstr(stSaveParam.pImageBuffer,stSaveParam.nImageLen);
	
	//string encoded_image = bytes_encode(stSaveParam.pImageBuffer,stSaveParam.nImageLen);
    
    if (stSaveParam.pImageBuffer)
    {
        free(stSaveParam.pImageBuffer);
        stSaveParam.pImageBuffer = NULL;
    }

    return encoded_image;
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

        // ch:打印当前相机ip和用户自定义名字 | en:print current ip and user defined name
        printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName);
        printf("CurrentIp: %d.%d.%d.%d\n" , nIp1, nIp2, nIp3, nIp4);
        printf("UserDefinedName: %s\n\n" , pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName);

        // determine which camera is camera one or two
        if(first_camera == 0) {
            if(nIp1 == 192) {
                camera_one = 0;
                camera_two = 1;
            }
            else{
                camera_one = 1;
                camera_two = 0;
            }
        }
        
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

void ImageCallBackEx(unsigned char * pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (pFrameInfo) {
        /*
        MVCC_STRINGVALUE stStringValue = { 0 };
        char camSerialNumber[256] = { 0 };
        int nRet = MV_CC_GetStringValue(pUser, "DeviceSerialNumber", &stStringValue);
        if (MV_OK == nRet)
        {
            memcpy(camSerialNumber, stStringValue.chCurValue, sizeof(stStringValue.chCurValue));
        }
        else
        {
            printf("Get DeviceUserID Failed! nRet = [%x]\n", nRet);
        }
        */

        //lock(t1,t2);

        cout << "Start Time: "<< get_current_date_time(time(0)) << endl;
        printf("Get One Frame: Width[%d], Height[%d], nFrameNum[%d]\n", 
            pFrameInfo->nWidth, pFrameInfo->nHeight, pFrameInfo->nFrameNum);
        
        lock(t1,t2);
        logger_file.open(logger_name,ios_base::app);
        logger_file << logging_generation("INFO","Frame Num: " + to_string(pFrameInfo->nFrameNum));
        cout << "Enter Lock Time: "<< get_current_date_time(time(0)) << endl;
        logger_file << logging_generation("INFO","Enter Lock Time: " + get_current_date_time(time(0)));

        top_count_updater();
        /*
        printf("Cam Serial Number[%s]:GetOneFrame, Width[%d], Height[%d], nFrameNum[%d]\n",
            camSerialNumber, pFrameInfo->nWidth, pFrameInfo->nHeight, pFrameInfo->nFrameNum);
        */
        
        unsigned int current_frame_num = pFrameInfo->nFrameNum;
        unsigned int current_lose_packet = pFrameInfo->nLostPacket;

        img_info = pFrameInfo;
       
        int top_down_code;
        int cur_camera_num;
        int cur_img_count;
        array<int, 2> lost_frame_packet_array;
        vector<string> lost_frames_position_code_vector;

        // encoding raw image data
        //MV_SAVE_IMAGE_PARAM_EX stSaveParam = raw_to_image(pData,img_info,pUser);
        //string encoded_img_data = base64_encode(stSaveParam.pImageBuffer,stSaveParam.nImageLen);
        string encoded_img_data = raw_to_image(pData,img_info,pUser);
       
        if(pUser == handle_one) {
            cur_camera_num = 1;

            // detect lose frame and lose packet first
            lost_frame_packet_array = lose_frame_or_lose_packet_detection(cur_camera_num,camera_one_last_frame_num,current_frame_num,current_lose_packet);
            camera_one_last_frame_num = current_frame_num;

            // if lose frame, we need buffer the position code of lost frames
            lost_frame_position_code_generator(lost_frame_packet_array[0],lost_frames_position_code_vector,camera_one_img_count,last_camera_one_position_code);

            // getting current position code
            top_down_code = position_code_generator(camera_one_img_count,last_camera_one_position_code);

            cur_img_count = camera_one_img_count;
        }
        else if(pUser == handle_two) {
            cur_camera_num = 2;

            // detect lose frame and lose packet first
            lost_frame_packet_array = lose_frame_or_lose_packet_detection(cur_camera_num,camera_two_last_frame_num,current_frame_num,current_lose_packet);
            camera_two_last_frame_num = current_frame_num;

            // if lose frame, we need buffer the position code of lost frames
            lost_frame_position_code_generator(lost_frame_packet_array[0],lost_frames_position_code_vector,camera_two_img_count,last_camera_two_position_code);

            // getting current position code
            top_down_code = position_code_generator(camera_two_img_count,last_camera_two_position_code);

            cur_img_count = camera_two_img_count;
        }

        //cout << "Check lost frames and lost packet Time: "<< get_current_date_time(time(0)) << endl;
        logger_file << logging_generation("INFO","Check lost frames and lost packet Time: " + get_current_date_time(time(0)));

        logger_file << logging_generation("INFO","Camera " + to_string(cur_camera_num) + " count: " + to_string(cur_img_count));
        cout << "Camera " + to_string(cur_camera_num) + " count: " + to_string(cur_img_count) << endl;

        // normal/lost_packet/lost_frame/lost_both
        string lost_or_normal_status = "normal";

        time_t now = time(0);
        string now_time = get_current_date_time(now);
        string now_time_without_symbol = get_current_date_time_without_symbol(now);            
        string key_word = image_naming(work_order.order_num,now_time,now_time_without_symbol,top_down_code,cur_camera_num);

        // lpush lost-packet frame to redis
        if(lost_frame_packet_array[1] > 20){
            logger_file << logging_generation("WARNING","Deal with lost packets cases!!!!!!!!!");

            if(lost_frames_position_code_vector.size() > 0){
                lost_or_normal_status = "lost_both";
            }
            else {
                lost_or_normal_status = "lost_packet";
            }

            string select_4_command_str = "select 4";
            char* select_4_command = (char*)select_4_command_str.c_str();
            redisReply* select_4_reply = redis_command_execution(listener,select_4_command,REDIS_REPLY_STRING);
            if(select_4_reply == NULL){
                cout << "Cannot select 4" << endl;
                logger_file << logging_generation("ERROR","Cannot select 4");
                return ;
            }
            freeReplyObject(select_4_reply);
			
            string td_code = position_code_int_to_string(top_down_code);
            string image_name = now_time_without_symbol + td_code + to_string(cur_camera_num);
            string img_command_str = "setex " + image_name + " 600 " + encoded_img_data;
			
            char* img_command = (char*)img_command_str.c_str();
            redisReply* img_reply = redis_command_execution(listener,img_command,REDIS_REPLY_STRING);
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

            string picture_model_queue_command_str = "lpush picture_model_queue " + key_word;
            char* picture_model_queue_command = (char*) picture_model_queue_command_str.c_str();
            redisReply* picture_model_queue_reply = redis_command_execution(listener,picture_model_queue_command,REDIS_REPLY_STRING);
            if(picture_model_queue_reply == NULL) {
                return ;
            }
            freeReplyObject(picture_model_queue_reply);
            logger_file << logging_generation("INFO",key_word + " without lost_packet is lpushed to redis");
            cout << key_word + " without lost_packet is lpushed to redis" << endl;

            string picture_storage_queue_command_str = "lpush picture_storage_queue " + key_word + "##" + encoded_img_data + "##" + lost_or_normal_status;
            char* picture_storage_queue_command = (char*)picture_storage_queue_command_str.c_str();
            redisReply* picture_storage_queue_reply = redis_command_execution(listener,picture_storage_queue_command,REDIS_REPLY_STRING);
            if(picture_storage_queue_reply == NULL) {
                return ;
            }
            freeReplyObject(picture_storage_queue_reply);
            logger_file << logging_generation("INFO",key_word + " with lost_packet is lpushed to redis");
            cout << key_word + " with lost_packet is lpushed to redis" << endl;

            string select_0_command_str = "select 0";
            char* select_0_command = (char*)select_0_command_str.c_str();
            redisReply* select_0_reply = redis_command_execution(listener,select_0_command,REDIS_REPLY_STRING);
            if(select_0_reply == NULL){
                cout << "Cannot select 0" << endl;
                logger_file << logging_generation("ERROR","Cannot select 0");
                return ;
            }
            freeReplyObject(select_0_reply);
        }
        else {
            if(lost_frames_position_code_vector.size() > 0){
                lost_or_normal_status = "lost_frame";
            }
            else {
                lost_or_normal_status = "normal";
            }

            // store image into redis
            string select_4_command_str = "select 4";
            char* select_4_command = (char*)select_4_command_str.c_str();
            redisReply* select_4_reply = redis_command_execution(listener,select_4_command,REDIS_REPLY_STRING);
            if(select_4_reply == NULL){
                cout << "Cannot select 4" << endl;
                logger_file << logging_generation("ERROR","Cannot select 4");
                return ;
            }
            freeReplyObject(select_4_reply);

            string td_code = position_code_int_to_string(top_down_code);
            string image_name = now_time_without_symbol + td_code + to_string(cur_camera_num);
            string img_command_str = "setex " + image_name + " 600 " + encoded_img_data;
            char* img_command = (char*)img_command_str.c_str();
            redisReply* img_reply = redis_command_execution(listener,img_command,REDIS_REPLY_STRING);
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

            string saved_command_str = "lpush picture_storage_queue " + key_word + "##" + encoded_img_data + "##" + lost_or_normal_status;
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

            //cout << "picture_storage_queue Time: "<< get_current_date_time(time(0)) << endl;
            logger_file << logging_generation("INFO", "picture_storage_queue Time: " + get_current_date_time(time(0)));
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

    //string command_str = "mget currentTicket currentZipperWidth currentTotalLength currentZipperColor";
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

bool parameter_setting(int nRet, void* handle, Parameter cp, int* camera_num){
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
