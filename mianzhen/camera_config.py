import os
import subprocess

os.system('service ufw stop')

last_line = ''

for result in subprocess.Popen('ifconfig',stdout=subprocess.PIPE, stderr=subprocess.PIPE).communicate()[0].split('\n'):
    if '169.254.247.164' in result or '192.168.1.100' in result:
        os.system('ifconfig ' + str(last_line.split(' ')[0]) + ' mtu 9000')
    last_line = result

print 'Camera configuration is ok'