from face_detection.src.test import *
import random
import math
class antispoof:
    def __init__(self):
        self.status = 0
        self.ans = 0
        self.delay = 0
    def get_st(self):
        return self.status 

    def is_0(self,arg_str):
        if arg_str == None:
            return False
        arg_list = extract_arg(arg_str)
        if abs(arg_list[0]) < 12 and abs(arg_list[1]) < 12 and abs(arg_list[2]) < 12: 
            return True
        return False

    def is_1(self,point_list):
        if point_list == None:
            return False
        k1,k2 = mouse_score(point_list)
        if k1 > 0.25 and k2 < 2.2:
            return True
        return False

    def is_2(self,arg_str):
        if arg_str == None:
            return False
        arg_list = extract_arg(arg_str)
        if arg_list[0] < -20:
            return True
        return False
    def up(self):
        if self.status % 4 == 0:  
            self.status = self.status + 4
        if self.status % 4 == 1:
            self.status = self.status + 32
        if self.status % 4 == 2:
            self.status = self.status + 256
    def get_ans(self):
        return self.ans
    
    def add_time(self):
        self.status = self.status + 2048
    
    def down(self):
        if self.status % 4 == 2 or self.status % 4 == 1:
            self.status = 3
            self.ans = -1
        else:
            self.update(None,None)

    def update(self, point_list, arg_str):
        flag = 0
        if self.delay > 0:
            self.delay = self.delay - 1
            return
        if self.status % 4 == 0 and self.is_0(arg_str):
            self.up()
        if self.status % 4 == 1 and self.is_1(point_list):
            self.up()
        if self.status % 4 == 2 and self.is_2(arg_str):
            self.up()
        self.add_time()
        if self.status % 2048 == 20:
            self.status = 1
            flag = 1
        if self.status % 2048 == 161:
            self.status = 2
            flag = 1
        if self.status % 2048 == 1282:
            self.status = 3
            self.ans = 1
        if self.status / 2048 > 500:
            self.status = 3
            self.ans = -1
        if self.status % 4 == 3 and self.status / 2048 > 25:
            self.status = 0
            self.ans = 0
            flag = 1
        if flag == 1:
            self.delay = int(random.random() * 30)


