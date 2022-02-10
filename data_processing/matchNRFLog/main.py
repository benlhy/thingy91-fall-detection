import re
import csv
from datetime import datetime
# Matches
#   1. x,y,x
#   2. :
#   3. a whitespace
#   4. either a - or not
#   5. a digit
#   6. .
#   7. a digit
#       
    
p_string = re.compile('[xyz][:]\s(-?[\d]\.\d)')
p_timestamp = re.compile(r'\d{2}:\d{2}:\d{2}.\d{3}')

f = open("rest5.txt")
fwrite = open("output_rest5.csv","a+", newline='')
write = csv.writer(fwrite)
write.writerow(['timestamp','x','y','z'])
count = 0
for l in f:
    # print(l)
    aa = re.findall(p_string,l)
    timestamp = re.findall(p_timestamp,l)
    
    if len(aa) == 3 and len(timestamp) == 1:
        #print(aa)
        timestamp_str = timestamp[0]
        dt = datetime.strptime(timestamp_str, '%H:%M:%S.%f')
        dt = (dt - datetime(1900,1,1)).total_seconds()
        timestamp[0] = int(dt*1000)
        # timestamp[0] = count
        # count = count + 60
        timestamp = timestamp+aa
        write.writerow(timestamp)
        print(timestamp)

f.close()
