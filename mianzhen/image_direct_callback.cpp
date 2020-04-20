#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "MvCameraControl.h" // Control file for HikVision Camera
#include <iostream>
#include <fstream> // Using for logging
#include <string>
// Library for timestamp generation
#include <time.h>
#include <sys/time.h>
// Library for Redis Connection
#include <stddef.h> 
#include <stdarg.h> 
#include <assert.h> 
#include <hiredis/hiredis.h>
#include <mutex>          // std::mutex, std::lock 

#include <cmath>

#include "image_direct_callback.h"

// g++ -std=c++11 -g -o image_direct_callback image_direct_callback.cpp -I./include -Wl,-rpath=/opt/MVS/lib/64 -L/opt/MVS/lib/64 -lMvCameraControl -lpthread -lhiredis

using namespace std;

// Initialize Device List
MV_CC_DEVICE_INFO_LIST stDeviceList;

double vision_width = 12.0; // unit: cm, vision width for a segment of zipper.

// Parameter for Camera One
//Parameter camera_parameter_one = {1760,560,64,460,60};
Parameter camera_parameter_one;

// Parameter for Camera Two
//Parameter camera_parameter_two = {1760,560,32,288,60};
Parameter camera_parameter_two;

int main(void) {
    cout << "Current Time: " << get_current_date_time(time(0)) << endl;
    // Create Logging File
    logger_name = "/home/docker/IBM/camera/camera_log/Camera_Logging_" + get_current_date_time(time(0)) + ".log";
    logger_file.open(logger_name,ios_base::app);
    logger_file << logging_generation("INFO","Camera Program Started");

    int nRet = MV_OK;

    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    listener = listener_creator("127.0.0.1",6379);
    if(listener == NULL) {
        return 0;
    }

    handle_one = NULL;
    handle_two = NULL;
    // enum device
    nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
    if (MV_OK != nRet) {
        printf("MV_CC_EnumDevices fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_EnumDevices fail! nRet " + to_string(nRet));
        return 0;
    }

    if (stDeviceList.nDeviceNum == 2) {
        for (int i = 0; i < stDeviceList.nDeviceNum; i++) {
            printf("[device %d]:\n", i);
            logger_file << logging_generation("INFO","[device " + to_string(i) + "]:");
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if (NULL == pDeviceInfo) {
                return 0;
            } 
            PrintDeviceInfo(pDeviceInfo,i);            
        }  
    } 
    else if (stDeviceList.nDeviceNum == 1){
        for (int i = 0; i < stDeviceList.nDeviceNum; i++) {
            printf("[device %d]:\n", i);
            logger_file << logging_generation("INFO","[device " + to_string(nRet) + "]:");
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if (NULL == pDeviceInfo) {
                return 0;
            } 
            PrintDeviceInfo(pDeviceInfo,i);            
        }  
        printf("Only one device found!");
        logger_file << logging_generation("ERROR","Only one device found!");
        return 0;
    }
    else {
        printf("Find No Devices!\n");
        logger_file << logging_generation("ERROR","Find No Devices!");
        return 0;
    }

    cout << "current camera one: " << camera_one << endl;
    cout << "current camera two: " << camera_two << endl; 

    handle_one = open_camera(camera_one, stDeviceList);
    if (handle_one == NULL) {
        return 0;
    }
    handle_two = open_camera(camera_two, stDeviceList);
    if (handle_two == NULL){
        return 0;
    }

    // Work Order Information Grabbing
    work_order = work_order_info_grabber(listener);
    if(work_order.order_num == "-2"){
        printf("Failed to get work order information.\n");
        logger_file << logging_generation("WARNING","Failed to get work order information.");
        return 0;
    }

    if(work_order.order_num == "-1"     ||
    work_order.zipper_num == "-1"    ||
    work_order.zipper_length == "-1" ||
    work_order.zipper_color == "-1"){
        printf("Failed to get some information from work order. Regrabbing work order information.\n");
        logger_file << logging_generation("WARNING","Failed to get some information from work order. Regrabbing work order information.");
        return 0;
    }
    
    // initialize top count for zippers of this work order and store into redis
    //top_count = ceil( stod(work_order.zipper_length) / vision_width) * 2 + 2;
    top_count = stoi(work_order.zipper_length);
    //cout << stod(work_order.zipper_length) / vision_width << endl;
    cout << "Top count: " << top_count << endl;
    logger_file << logging_generation("INFO","Top count: " + to_string(top_count));

    /*
    bool top_count_flag = counter_redis_setting(listener,"topCount",top_count);
    if(!top_count_flag){
        printf("Failed to update top count to redis");
        logger_file << logging_generation("WARNING","Failed to update top count to redis");
        return 0;
    }
    */

    // grab camera setting parameter from redis based on number and color of current zipper 
    string select_1_command_str = "select 1";
    char* select_1_command = (char*)select_1_command_str.c_str();
    redisReply* select_1_reply = redis_command_execution(listener,select_1_command,REDIS_REPLY_STRING);
    if(select_1_reply == NULL){
        cout << "Cannot select 1" << endl;
        logger_file << logging_generation("ERROR","Cannot select 1");
        return 0;
    }
    freeReplyObject(select_1_reply);
    cout << "OOOOO" <<endl;
    string left_command_str = "get " + work_order.zipper_num + "2";
    char* left_command = (char*)left_command_str.c_str();
    redisReply* left_reply = redis_command_execution(listener,left_command,REDIS_REPLY_STRING);
    if(left_reply == NULL){
        logger_file << logging_generation("ERROR","Cannot get parameter from left camera");
        return 0;
    }
    string left_size = left_reply->str;
    freeReplyObject(left_reply);
    
    string right_command_str = "get " + work_order.zipper_num + "1";
    char* right_command = (char*)right_command_str.c_str();
    redisReply* right_reply = redis_command_execution(listener,right_command,REDIS_REPLY_STRING);
    if(right_reply == NULL){
        logger_file << logging_generation("ERROR","Cannot get parameter from right camera");
        return 0;
    }
    string right_size = right_reply->str;
    freeReplyObject(right_reply);
    cout << "ok!!!!!"<<endl;
    cout << work_order.zipper_color<<endl;
    string color_command_str = "get " + work_order.zipper_color;
    char* color_command = (char*)color_command_str.c_str();
    redisReply* color_reply = redis_command_execution(listener,color_command,REDIS_REPLY_STRING);
    if(color_reply == NULL){
        logger_file << logging_generation("ERROR","Cannot get zipper color");
        return 0;
    }
    string current_exposure = color_reply->str;
    freeReplyObject(color_reply);
    cout << "okiii"<<endl;
    string select_0_command_str = "select 0";
    char* select_0_command = (char*)select_0_command_str.c_str();
    redisReply* select_0_reply = redis_command_execution(listener,select_0_command,REDIS_REPLY_STRING);
    if(select_0_reply == NULL){
        cout << "Cannot select 1" << endl;
        logger_file << logging_generation("ERROR","Cannot select 0");
        return 0;
    }
    freeReplyObject(select_0_reply);
    cout <<"is ok end"<<endl;
    string left_size_array[4];
    string left_delimiter = ",";
    size_t left_pos = 0;
    int left_index = 0;
    while ((left_pos = left_size.find(left_delimiter)) != string::npos) {
        left_size_array[left_index] = left_size.substr(0, left_pos);
        left_size.erase(0, left_pos + left_delimiter.length());
        left_index++;
    }
    left_size_array[left_index] = left_size;

    string right_size_array[4];
    string right_delimiter = ",";
    size_t right_pos = 0;
    int right_index = 0;
    while ((right_pos = right_size.find(right_delimiter)) != string::npos) {
        right_size_array[right_index] = right_size.substr(0, right_pos);
        right_size.erase(0, right_pos + right_delimiter.length());
        right_index++;
    }
    right_size_array[right_index] = right_size;

    int right_width = stoi(right_size_array[0]);
    int right_height = stoi(right_size_array[1]);
    int right_offsetx = stoi(right_size_array[2]);
    int right_offsety = stoi(right_size_array[3]);
    float right_exposure = strtof(current_exposure.c_str(),NULL);

    int left_width = stoi(left_size_array[0]);
    int left_height = stoi(left_size_array[1]);
    int left_offsetx = stoi(left_size_array[2]);
    int left_offsety = stoi(left_size_array[3]);
    float left_exposure = strtof(current_exposure.c_str(),NULL);

    camera_parameter_one = {right_width,
                            right_height,
                            right_offsetx,
                            right_offsety,
                            right_exposure};
    
    camera_parameter_two = {left_width,
                            left_height,
                            left_offsetx,
                            left_offsety,
                            left_exposure};

    int *address_one;
    address_one = &camera_one;
    bool is_set_one = parameter_setting(nRet,handle_one,camera_parameter_one,address_one);
    if(!is_set_one) {
        printf("Parameter setting fail for camera_one!");
        logger_file << logging_generation("ERROR","Parameter setting fail for camera_one!");
        return 0;
    }

    logger_file << logging_generation("INFO","Paramter one setting succeed.");
    logger_file << logging_generation("INFO",string("Parameters are [") + 
                                      string("Width:") + to_string(camera_parameter_one.roi_width) + string(",") + 
                                      string("Height:") + to_string(camera_parameter_one.roi_height) + string(",") + 
                                      string("Offsetx:") + to_string(camera_parameter_one.roi_offsetx) + string(",") + 
                                      string("Offsety:") + to_string(camera_parameter_one.roi_offsety) + string(",") + 
                                      string("Exposure:") + to_string(camera_parameter_one.exposure_time) + string("]"));

    int *address_two;
    address_two = &camera_two;
    bool is_set_two = parameter_setting(nRet,handle_two,camera_parameter_two,address_two);
    if(!is_set_two) {
        printf("Parameter setting fail for camera_two!");
        logger_file << logging_generation("ERROR","Parameter setting fail for camera_two!");
        return 0;
    }

    logger_file << logging_generation("INFO","Paramter two setting succeed.");
    logger_file << logging_generation("INFO",string("Parameters are [") + 
                                      string("Width:") + to_string(camera_parameter_two.roi_width) + string(",") + 
                                      string("Height:") + to_string(camera_parameter_two.roi_height) + string(",") + 
                                      string("Offsetx:") + to_string(camera_parameter_two.roi_offsetx) + string(",") + 
                                      string("Offsety:") + to_string(camera_parameter_two.roi_offsety) + string(",") + 
                                      string("Exposure:") + to_string(camera_parameter_two.exposure_time) + string("]"));
    
    // start grab image
    nRet = MV_CC_StartGrabbing(handle_one);
    if (MV_OK != nRet) {
        printf("MV_CC_StartGrabbing fail for camera_one! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_StartGrabbing fail for camera_one! nRet: " + to_string(nRet));
        return 0;
    }

    // start grab image
    nRet = MV_CC_StartGrabbing(handle_two);
    if (MV_OK != nRet) {
        printf("MV_CC_StartGrabbing fail for camera_two! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_StartGrabbing fail for camera_two! nRet: " + to_string(nRet));
        return 0;
    }

    // set cameraIsReady to tell PLC that camera is ready
    string camera_is_ready_true_command_str = "set cameraIsReady true";
    char* camera_is_ready_true_command = (char*)camera_is_ready_true_command_str.c_str();
    redisReply* camera_is_ready_true_reply = redis_command_execution(listener,camera_is_ready_true_command,REDIS_REPLY_STRING);
    if(camera_is_ready_true_reply == NULL) {
        return 0;
    }
    freeReplyObject(camera_is_ready_true_reply);
    logger_file << logging_generation("INFO","Camera is Ready");

    logger_file.close();

    sleep(1);
    
    do {
        lock(t1,t2);
        // reset camera_one_img_count and camera_two_img_count when machine stop
        string machine_status_command_str = "get machineStatus";
        char* machine_status_command = (char*)machine_status_command_str.c_str();
        redisReply* machine_status_reply = redis_command_execution(listener,machine_status_command,REDIS_REPLY_STRING);
        if(machine_status_reply == NULL){
            return 0;
        }
        string machine_status = machine_status_reply->str;
        freeReplyObject(machine_status_reply);

        if(machine_status == "off"){
            if(first_off) {
                first_off = false;
                logger_file.open(logger_name,ios_base::app);
                logger_file << logging_generation("INFO","Machine stopped and reset camera_one_img_count and camera_two_img_count");
                logger_file.close();
                cout << "Machine stopped and reset camera_one_img_count and camera_two_img_count\n\n\n\n\n\n\n";

                camera_one_img_count = 0;
                camera_two_img_count = 0;

                last_camera_one_position_code = 1;
                last_camera_two_position_code = 1;
            }
        }
        else {
            first_off = true;
        }

        /*
        if(last_machine_status == "off" && machine_status == "on") {
            string top_count_command_str = "get topcount";
            char* top_count_command = (char*)top_count_command_str.c_str();
            redisReply* top_count_reply = redis_command_execution(listener,top_count_command,REDIS_REPLY_STRING);
            if(top_count_reply == NULL){
                return 0;
            }
            string cur_top_count = top_count_reply->str;
            freeReplyObject(top_count_reply);

            top_count = stod(cur_top_count);

            logger_file.open(logger_name,ios_base::app);
            cout << "Current top count: " << top_count << endl;
            logger_file << logging_generation("INFO","Current top count: " + to_string(top_count));
            logger_file.close();
        }
        last_machine_status = machine_status;
        */

        t1.unlock();
        t2.unlock();

        sleep(0.5);
    } while(true);

    logger_file.open(logger_name,ios_base::app);

    // end grab image
    nRet = MV_CC_StopGrabbing(handle_one);
    if (MV_OK != nRet) {
        printf("MV_CC_StopGrabbing fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_StopGrabbing fail! nRet: " + to_string(nRet));
        return 0;
    }

    // end grab image
    nRet = MV_CC_StopGrabbing(handle_two);
    if (MV_OK != nRet) {
        printf("MV_CC_StopGrabbing fail! nRet [%x]\n", nRet);
        logger_file << logging_generation("ERROR","MV_CC_StopGrabbing fail! nRet: " + to_string(nRet));
        return 0;
    }

    close_camera(handle_one);
    close_camera(handle_two);

    logger_file.close();

    return 0;
}
