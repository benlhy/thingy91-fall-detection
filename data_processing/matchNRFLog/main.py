from cmath import sqrt
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

file_name = "walk (9)"

p_string = re.compile('[xyz][:]\s(-?[\d]\.\d)')
p_timestamp = re.compile(r'\d{2}:\d{2}:\d{2}.\d{3}')

f = open(file_name+".txt")
fwrite = open("output_"+file_name+".csv","a+", newline='')
write = csv.writer(fwrite)
write.writerow(['timestamp','x','y','z','v'])


count = 0
first_val = 0
for l in f:
    # print(l)
    aa = re.findall(p_string,l)
    timestamp = re.findall(p_timestamp,l)
    if len(aa) == 3 and len(timestamp) == 1:
        
        #print(aa)
        v = sqrt(float(aa[0])**2 + float(aa[1])**2 + float(aa[2])**2)
        aa.append(f'{v.real:.1f}')
        timestamp_str = timestamp[0]
        dt = datetime.strptime(timestamp_str, '%H:%M:%S.%f')
        dt = (dt - datetime(1900,1,1)).total_seconds()
        if count==0:
            first_val = int(dt*1000)
        if count==1:
            timestamp[0] = first_val+60
        else:
            timestamp[0] = int(dt*1000)
        # timestamp[0] = count
        # count = count + 60
        timestamp = timestamp+aa
        write.writerow(timestamp)
        print(timestamp)
        count = count+1
    

f.close()
