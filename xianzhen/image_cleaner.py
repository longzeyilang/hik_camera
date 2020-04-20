import os
import sys
from datetime import datetime
import time

# python /home/docker/IBM/camera/image_cleaner.py > /home/docker/IBM/camera/image_cleaner.txt
# 50 0    * * *   root    python /home/docker/IBM/camera/image_cleaner.py > /home/docker/IBM/camera/image_cleaner.txt

image_path = '/mnt/newdisk/image_backup/'
current_time = datetime.now()

image_root = os.listdir(image_path)
print 'Cleanering images from ' + image_path


for img in image_root:
  date_object = datetime.strptime(img, '%Y%m%d')
  day_diff = (current_time - date_object).days
  # print day_diff , img , current_time

  if day_diff > 50:
    sys_status = os.system('rm -rf ' + image_path + img)
  
    if sys_status == 0:
      print 'rm -rf ' + image_path + img + ' succeed'

          
    else:
      print 'Cannot remove ' + image_path + img

print 'Finished Clean'