import re, json

def get_timeStamp_hostName_eventName(line):
    res = {}
    str_list = re.split(' ', line)
    assert len(str_list) == 4
    time = get_time_stamp(str_list[0])
    hostname = str_list[2]
    eventname = str_list[3]
    res["time"] = time
    res["hostname"] = hostname
    res["eventname"] = eventname
    return res

def get_time_stamp(line):
    p1 = re.compile("[0-9]*:[0-9]*:[0-9]*.[0-9]*", re.S)
    res = re.findall(p1, line)
    assert len(res) == 1
    return res[0]

# cpu_id = 1
# pid = 640
# call_site = 0xFFFFFFFFC08343B9, ptr = 0xFFFF938818002400, bytes_req = 739, bytes_alloc = 1024, gfp_flags = 6291648
def get_attribute(line):
    str_list = re.split(' = ', line)
    assert len(str_list) == 2
    return str_list[0], str_list[1]

def get_attribute_pair(line):
#     p1 = re.compile(", ", re.S)
#     res = re.findall(p1, line)
    result_map = {}
    str_list = re.split(', ', line)
    for i in range(len(str_list)):
        k, v = get_attribute(str_list[i])
        result_map[k] = v
    return result_map
    


def get_attributes_pair(line):
    # 加上一个问号表示进行最短匹配
    result_map = {}
    p1 = re.compile("{ (.*?) }", re.S)
    res = re.findall(p1, line)
#     print(len(res))
    for i in range(len(res)):
        result_map.update(get_attribute_pair(res[i]))
    return result_map
#         print("")

# 读取Lttng Analysis的数据
# 这里只是一个sample，整合的代码后面有
f = open("babeltrace.txt")  # 返回一个文件对象
line = f.readline()  # 调用文件的 readline()方法
res = []
while line:
    try:
        # 用": ""作为分隔符，进行信息的抽取
        str_list = re.split(': ', line)
        length = len(str_list)
        p1 = str_list[0]
        p2 = str_list[1]
        result_map = get_timeStamp_hostName_eventName(p1)
        result_map.update(get_attributes_pair(p2))
        res.append(result_map)
        line = f.readline()  # 调用文件的 readline()方法
    except Exception as e:
        line = f.readline()  # 调用文件的 readline()方法

f.close()
with open('output.json', 'w') as json_file:    
    json.dump(res,json_file, indent=4)
#     json_file.close()
