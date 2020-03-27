import math
import re
def D(x,y):
    return sqrt((x[0]-y[0])*(x[0]+y[0])+(x[1]-y[1])*(x[1]-y[1]))
def mouse_score(point_list):
    k = point_list
    return D(k[62],k[66])/((k[62],k[51])+D(k[66],k[57])),D(k[60],k[64])/D(k[51],k[57])
def eye_score(point_list)
    k = point_list
    return (D(k[37],k[41])+D(k[38],k[40]))/D(k[36],k[39]),(D(k[43],k[47])+D(k[44],k[46]))/D(k[42],k[45])
def extract_arg(_str)
    return re.findall(r"-?d+\.?\d*",_str)
