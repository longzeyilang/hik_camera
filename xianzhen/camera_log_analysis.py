import time
from datetime import datetime
from datetime import timedelta

log_file_path = "Camera_Logging.log"
log_file = open(log_file_path,"r")

camera_one_file = open('camera_one.txt','w')
camera_two_file = open('camera_two.txt','w')

camera_one_time = ''
camera_two_time = ''

end_flag = False
while end_flag != True:
	log_line = str(log_file.readline())
	if log_line == '':
		end_flag = True

	if 'without image is lpushed to redis' in log_line:
		split_list = log_line.split(' ')
		img_tag = split_list[len(split_list)-7]
		camera_num = img_tag.split('#')[8]
		timestamp = img_tag.split('#')[4]
		if camera_one_time != '' and camera_num == '1':
			old_time = datetime.strptime(camera_one_time, "%Y-%m-%d-%H:%M:%S:%f")
			new_time = datetime.strptime(timestamp, "%Y-%m-%d-%H:%M:%S:%f")
			interval = new_time - old_time
			camera_one_file.write(img_tag.split('#')[0] + " " + str(interval) + "\n")
			camera_one_time = timestamp
		elif camera_one_time == '' and camera_num == '1':
			camera_one_time = timestamp

		if camera_two_time != '' and camera_num == '2':
			old_time = datetime.strptime(camera_one_time, "%Y-%m-%d-%H:%M:%S:%f")
			new_time = datetime.strptime(timestamp, "%Y-%m-%d-%H:%M:%S:%f")
			interval = new_time - old_time
			camera_two_file.write(img_tag.split('#')[0] + " " + str(interval) + "\n")
			camera_two_time = timestamp
		elif camera_two_time == '' and camera_num == '2':
			camera_two_time = timestamp

camera_one_file.close()
camera_two_file.close()
