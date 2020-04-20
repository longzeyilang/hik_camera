# -*- coding: utf-8 -*-

import logging
from logging import handlers
from datetime import datetime
import redis as rds
import os
import time
import base64

# 日志类
class Logger(object):
    level_relations = {
        'debug':logging.DEBUG,
        'info':logging.INFO,
        'warning':logging.WARNING,
        'error':logging.ERROR,
        'crit':logging.CRITICAL
    }#日志级别关系映射

    def __init__(self,filename,level='info',when='D',backCount=3,fmt='%(asctime)s - %(pathname)s[line:%(lineno)d] - %(levelname)s: %(message)s'):
        self.logger = logging.getLogger(filename)
        format_str = logging.Formatter(fmt)#设置日志格式
        self.logger.setLevel(self.level_relations.get(level))#设置日志级别
        sh = logging.StreamHandler()#往屏幕上输出
        sh.setFormatter(format_str) #设置屏幕上显示的格式
        th = handlers.TimedRotatingFileHandler(filename=filename,when=when,backupCount=backCount,encoding='utf-8')#往文件里写入#指定间隔时间自动生成文件的处理器
        #实例化TimedRotatingFileHandler
        #interval是时间间隔，backupCount是备份文件的个数，如果超过这个个数，就会自动删除，when是间隔的时间单位，单位有以下几种：
        # S 秒
        # M 分
        # H 小时、
        # D 天、
        # W 每星期（interval==0时代表星期一）
        # midnight 每天凌晨
        th.setFormatter(format_str)#设置文件里写入的格式
        self.logger.addHandler(sh) #把对象加到logger里
        self.logger.addHandler(th)

def timestamp_generator(cur_time):
    current_time = cur_time.strftime('20%y-%m-%d-%H:%M:%S:%f')
    return current_time

def timestamp_generator_without_splitor(cur_time):
    current_time = cur_time.strftime('20%y%m%d%H%M%S%f')
    return current_time

test_logger = Logger('/home/sabvi/lixl/camera/test_log/Camera_Test_Logging_' + str(timestamp_generator(datetime.now())) + '.log',level='debug')
test_logger.logger.info("Camera Program Started")

redis_cache_zero = rds.StrictRedis(host='localhost', port=6379, db=0)
redis_cache_three = rds.StrictRedis(host='localhost', port=6379, db=3)
redis_cache_four = rds.StrictRedis(host='localhost', port=6379, db=4)
test_logger.logger.info('Connected to redis')

top_count = 12
img_count = 0
test_logger.logger.info('Top Count: ' + str(top_count))

#ticket_num = redis_cache_zero.get('currentTicket')
ticket_num = "19460801"

data_path = '/home/sabvi/lixl/camera/pressure_test_images/'
data_root = os.listdir(data_path)

zipper_count = 0

while True:
    while img_count != top_count:
        test_logger.logger.info('Current Image Count for this Zapper: ' + str(img_count + 1))
        img_path = str(os.path.join(data_path, str(img_count+1) + '.jpg'))
        img_data_file = open(img_path,'rb')
        img_data = img_data_file.read()
        encoded_img_data = base64.b64encode(img_data)

        cur_camera_id = '0'
        if (img_count + 1) % 2 == 0:
            cur_camera_id = '2'
        else:
            cur_camera_id = '1'

        position_code = '00'
        if img_count < 2:
            position_code = '10'
        elif img_count >= top_count - 2:
            position_code = '01'
        
        cur_time = datetime.now()
        cur_timestamp = timestamp_generator(cur_time)
        cur_timestamp_without_label = timestamp_generator_without_splitor(cur_time)

        img_name = cur_timestamp_without_label + position_code + cur_camera_id
        test_logger.logger.info('Current Image Name: ' + img_name)

        redis_cache_four.set(img_name,encoded_img_data,ex=600)
        test_logger.logger.info('Image ' + img_name + ' key-value pair is set in the redis')

        publish_content = img_name + '##' + ticket_num + '##' + cur_timestamp + '##' + position_code + '##' + cur_camera_id
        redis_cache_three.lpush('picture_model_queue',publish_content)
        test_logger.logger.info(publish_content + ' without image is lpushed to redis')

        lpush_content = publish_content + '##' + encoded_img_data
        redis_cache_three.lpush('picture_storage_queue',lpush_content)
        test_logger.logger.info(publish_content + ' with image is lpushed to redis')

        time.sleep(0.5)    
        img_count = img_count + 1

        if img_count == top_count:
            img_count = 0
            #test_logger.logger.info('Finished one zipper')
            zipper_count = zipper_count + 1
            test_logger.logger.info('Finished ' + str(zipper_count) + ' zippers')