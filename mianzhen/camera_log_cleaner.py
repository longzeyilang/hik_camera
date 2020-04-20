from datetime import datetime
import os

log_path = '/home/sabvi/IBM/camera/camera_log'
log_root = os.listdir(log_path)

current_time = datetime.now()

for log in log_root:
    if log.split('.')[1] != 'log':
        continue
    
    date = log.split('_')[2].split('.')[0].split(':')[0]
    date = date[0:len(date)-3]
    date_object = datetime.strptime(date, '%Y-%m-%d')
    day_diff = (current_time - date_object).days

    if day_diff > 5:
        os.system('rm ' + log_path + '/' + log)

print 'Clean'





