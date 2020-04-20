import os
import sys
import time
import redis as rds

import subprocess
from subprocess import check_output

c_file_name = 'image_direct_callback'

def get_pid(name):
    return check_output(["pidof",name])

action = str(sys.argv[1])

redis_cache = rds.StrictRedis(host='localhost', port=6379, db=0)
camera_is_ready_flag = redis_cache.set('cameraIsReady','false')

if action == 'start':
    os.system('nohup /home/docker/IBM/camera/' + c_file_name + ' > /home/docker/IBM/camera/nohup_result.txt &')
    time.sleep(5)
    try:
        camera_is_ready_flag = redis_cache.get('cameraIsReady')
        if str(camera_is_ready_flag) == 'true':
            print 'OK'
        else:
            print 'NOT OK'
    except ValueError:
        print 'NOT OK'

if action == 'stop':
    os.system('pkill -f ' + c_file_name)
    try:
        pid = get_pid(c_file_name)
        print 'NOT OK'
    except subprocess.CalledProcessError:
        camera_is_ready_flag = redis_cache.set('cameraIsReady','false')
        print 'OK'
