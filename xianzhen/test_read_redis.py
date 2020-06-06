import redis
import time
r = redis.Redis(host='localhost',port=6379,db=4)
t0 = time.time()
data = r.get("20200310145017079557011")
doc = open('out.jpg','w')
doc.write(data)
doc.close()
t1 = time.time()
print("bytes to image it takes {}".format((t1-t0)))
