import os
import sys
from datetime import datetime

# 30 0    * * *   root    python /home/sabvi/image_transfer.py

image_root = '/home/docker/sabvi/imageserver/upload/'
image_root = image_root + os.listdir(image_root)[0] + '/'
dest_root = '/mnt/newdisk/image_backup/'
image_root_sub_list = os.listdir(image_root)
print 'Transfering images from ' + image_root

today = datetime.today()
cur_date = datetime.strftime(today,'%Y%m%d')
print 'Today is ' + cur_date

dir_list = []

for file in image_root_sub_list:
	if os.path.isdir(image_root + file) and len(file) == 8 and file != str(cur_date):
		sys_status = os.system('cp -r ' + image_root + file + ' ' + dest_root)
		if sys_status == 0:
			print 'cp -r ' + image_root + file + ' ' + dest_root + ' succeed'
			dir_list.append(image_root + file)
		else:
			print 'Cannot copy ' + image_root + file
			break

for dir in dir_list:
	sys_status = os.system('rm -rf ' + dir)
	if sys_status == 0:
		print 'succeed to remove ' + dir
	else:
		print 'Cannot remove ' + dir
		sys.exit() 

print 'Finished transfering'
